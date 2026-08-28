/*
 * mod-gauntlet - run state, persistence and effect aggregation
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMgr.h"
#include "GauntletGenerator.h"
#include "GauntletLegacy.h"
#include "GauntletMechanic.h"
#include "GauntletRegistry.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "World.h"
#include "WorldSessionMgr.h"
#include "GameTime.h"
#include "WorldSession.h"
#include <algorithm>
#include <memory>
#include <utility>

namespace Gauntlet
{
    // =====================================================================
    // RunState: the owner of every IMechanic in the run.
    //
    // Declared in Gauntlet.h and defined here because this is the only
    // translation unit in which IMechanic is a complete type, which `delete`
    // needs. Everything that creates or destroys an implementation goes
    // through these four functions and nothing else in the module calls
    // MakeMechanic.
    // =====================================================================
    RunState::~RunState()
    {
        Clear();
    }

    RunState::RunState(RunState&& other) noexcept
    {
        *this = std::move(other);
    }

    RunState& RunState::operator=(RunState&& other) noexcept
    {
        if (this == &other)
            return *this;

        // Whatever this run held dies here, exactly once, before it is
        // overwritten by the other run's pointers.
        Clear();

        seed         = other.seed;
        tier         = other.tier;
        dead         = other.dead;
        genVersion   = other.genVersion;
        playerClass  = other.playerClass;
        pendingTier  = other.pendingTier;
        pendingDeath = other.pendingDeath;
        deathTimerMs = other.deathTimerMs;
        dirty        = other.dirty;

        affixes = std::move(other.affixes);
        pending = std::move(other.pending);

        // A moved-from vector is valid but unspecified. Making it definitely
        // empty is what guarantees the source's destructor frees nothing.
        other.affixes.clear();
        other.pending.clear();
        other.pendingDeath = false;
        other.deathTimerMs = 0;
        return *this;
    }

    void RunState::Clear()
    {
        for (AffixInstance& a : affixes)
        {
            delete a.impl;
            a.impl = nullptr;
        }
        affixes.clear();
    }

    AffixInstance& RunState::Attach(AffixInstance instance)
    {
        // Null for anything this build does not implement -- a family switched
        // off, or a run migrated from a newer registry. That is a normal
        // answer, not an error; Aggregate skips such an instance whole.
        instance.impl = MakeMechanic(instance.mechanic);
        affixes.push_back(instance);
        return affixes.back();
    }

    bool RunState::DetachSlot(uint8 slot)
    {
        for (auto it = affixes.begin(); it != affixes.end(); ++it)
        {
            if (it->slot != slot)
                continue;

            delete it->impl;
            affixes.erase(it);
            return true;
        }
        return false;
    }

    AffixInstance* RunState::Find(uint16 mechanic)
    {
        for (AffixInstance& a : affixes)
            if (a.mechanic == mechanic)
                return &a;
        return nullptr;
    }

    AffixInstance* RunState::AtSlot(uint8 slot)
    {
        for (AffixInstance& a : affixes)
            if (a.slot == slot)
                return &a;
        return nullptr;
    }

    namespace
    {
        // The generator, the registry and the aggregate maths never see a
        // Player (CONTRACT section 7): this is the one adapter that does, and
        // it lives beside the only code that owns a Player and needs an offer.
        class LivePlayerView : public IPlayerView
        {
        public:
            explicit LivePlayerView(Player* player) : _player(player) { }

            uint8 GetClass() const override { return _player->getClass(); }
            uint8 GetLevel() const override { return _player->GetLevel(); }
            bool  HasSpell(uint32 spellId) const override { return _player->HasSpell(spellId); }

            // tabpage + 1, with 0 reserved for "no spec yet"; see the comment
            // on IPlayerView::GetTalentTree. Player::GetMostPointsTalentTree
            // cannot express that distinction on its own -- it returns 0 both
            // for the first tab of every class and for a character that has
            // spent nothing -- so the spent-point count decides which it is.
            uint8 GetTalentTree() const override
            {
                uint32 const total = _player->CalculateTalentsPoints();
                uint32 const free  = _player->GetFreeTalentPoints();
                if (total <= free)
                    return 0;

                return static_cast<uint8>(_player->GetMostPointsTalentTree() + 1);
            }

        private:
            Player* _player;
        };

        // The action column of gauntlet_affix_log, one string per OfferKind
        // plus the swap's two halves. The ENUM in the schema is the authority:
        // 'pick', 'rankup', 'swap_out', 'swap_in', 'bargain'.
        char const* LogAction(OfferKind kind)
        {
            switch (kind)
            {
                case OfferKind::RankUp:  return "rankup";
                case OfferKind::Bargain: return "bargain";
                case OfferKind::Swap:    return "swap_in";
                default:                 return "pick";
            }
        }

        std::string RankNumeral(uint8 rank)
        {
            switch (rank)
            {
                case 1:  return "I";
                case 2:  return "II";
                case 3:  return "III";
                default: return std::to_string(static_cast<uint32>(rank));
            }
        }

        // Generator 1 rolled a free percentage; the redesign has three ranks.
        // The exact percentage survives in legacy_mag and is what the mechanic
        // actually uses, so this only decides what the affix is *called* --
        // but it is written to a column and shown to the player, so it is
        // chosen rather than defaulted. Severity is generator 1's own notion
        // of strength and already has six steps, so two fold into each rank.
        uint8 RankFromSeverity(Severity severity)   // TODO(design)
        {
            switch (severity)
            {
                case Severity::Trivial:
                case Severity::Minor:    return 1;
                case Severity::Moderate:
                case Severity::Major:    return 2;
                default:                 return 3;
            }
        }

        // Plan section 3.6 fixes this map, and CONTRACT section 8 the ids:
        // the four effects generator 1 ever rolled are the four mechanics
        // Phase 0 implements. Anything else means the row did not come from
        // LegacyRoll and must not be guessed at.
        uint16 MechanicForEffect(Effect effect)
        {
            switch (effect)
            {
                case Effect::DamageTaken:     return 21;   // A3 Exposed
                case Effect::DamageDone:      return 22;   // A4 Feeble
                case Effect::HealingReceived: return MECHANIC_WITHERING;
                case Effect::ExperienceGain:  return MECHANIC_FORGETFUL;
                default:                      return MECHANIC_NONE;
            }
        }
    }

    Mgr* Mgr::instance()
    {
        static Mgr inst;
        return &inst;
    }

    void Mgr::LoadConfig()
    {
        _enabled  = sConfigMgr->GetOption<bool>("Gauntlet.Enable", true);
        _hardcore = sConfigMgr->GetOption<bool>("Gauntlet.Hardcore", true);
        _interval = sConfigMgr->GetOption<uint32>("Gauntlet.TierInterval", 5);
        _choices  = sConfigMgr->GetOption<uint32>("Gauntlet.ChoicesPerTier", 3);
        _announce = sConfigMgr->GetOption<bool>("Gauntlet.Announce", true);
        _playersOnly = sConfigMgr->GetOption<bool>("Gauntlet.PlayersOnly", true);

        // Plan section 2.5's clamps, overridable per realm. Gauntlet.Caps.* is
        // the conf worker's naming; the defaults here repeat the ones in
        // conf/mod_gauntlet.conf.dist so a realm with no config file still
        // gets the design's numbers. damageTakenMin has no key on purpose:
        // 1.0 means "an affix may never make you take less damage", which is
        // a rule rather than a tuning knob.
        _caps.damageTakenMax = sConfigMgr->GetOption<float>("Gauntlet.Caps.DamageTaken", 2.0f);
        _caps.damageDoneMin  = sConfigMgr->GetOption<float>("Gauntlet.Caps.DamageDone", 0.6f);
        _caps.healTakenMin   = sConfigMgr->GetOption<float>("Gauntlet.Caps.HealTaken", 0.5f);
        _caps.maxHealthMin   = sConfigMgr->GetOption<float>("Gauntlet.Caps.MaxHealth", 0.6f);
        _caps.enemySpeedMax  = sConfigMgr->GetOption<float>("Gauntlet.Caps.EnemySpeed", 1.4f);

        // Gauntlet.Grace.Seconds is the conf worker's key for the Phase 1
        // event grace period; the death timer borrows its 60 s because plan
        // section 6 decision 5 names the same number and Phase 0 has nothing
        // else to hang it on.
        _graceMs = sConfigMgr->GetOption<uint32>("Gauntlet.Grace.Seconds", 60) * 1000;   // TODO(design)

        if (_interval == 0)
            _interval = 5;
        if (_choices == 0)
            _choices = 1;
        if (_graceMs == 0)
            _graceMs = 60000;
    }

    bool Mgr::IsEligible(Player* player) const
    {
        if (!player || !player->GetSession())
            return false;
        if (_playersOnly && player->GetSession()->IsBot())
            return false;
        return true;
    }

    RunState* Mgr::Get(Player* player)
    {
        if (!player)
            return nullptr;
        auto it = _runs.find(player->GetGUID());
        return it == _runs.end() ? nullptr : &it->second;
    }

    std::string Mgr::NameOf(uint16 mechanic, Condition condition, Boon boon) const
    {
        MechanicDef const* def = FindMechanic(mechanic);

        // A stored id this build does not carry still has to be printable:
        // the run is a live character's and refusing to name it is worse than
        // naming it badly.
        std::string name = def ? std::string(def->name) : ("#" + std::to_string(static_cast<uint32>(mechanic)));

        // Affix::Name() built "Wrathful Desperate Exposed" out of the boon,
        // the condition and the effect. The shape is kept because the addon's
        // chat fallback and every screenshot of the module depend on it; only
        // the last word now comes from the registry instead of an Effect.
        name = ConditionName(condition) + " " + name;
        if (boon != Boon::None)
            name = BoonName(boon) + " " + name;
        return name;
    }

    std::string Mgr::DescribeOf(AffixInstance const& instance) const
    {
        if (instance.impl)
            return instance.impl->Describe(instance);

        // No implementation: fall back to the registry's one-line blurb, which
        // every one of the 73 entries carries.
        if (MechanicDef const* def = FindMechanic(instance.mechanic))
            return def->blurb;

        return "An affix this server no longer knows.";
    }

    void Mgr::Load(Player* player)
    {
        if (!IsEligible(player))
            return;

        RunState st;
        std::vector<AffixInstance> loaded;
        uint32 const low = player->GetGUID().GetCounter();

        if (QueryResult r = CharacterDatabase.Query(
                "SELECT `seed`, `tier`, `dead`, `gen_version`, `class` FROM `gauntlet_run` WHERE `guid` = {}", low))
        {
            Field* f       = r->Fetch();
            st.seed        = f[0].Get<uint32>();
            st.tier        = f[1].Get<uint32>();
            st.dead        = f[2].Get<uint8>() != 0;
            st.genVersion  = f[3].Get<uint16>();
            st.playerClass = f[4].Get<uint8>();

            // Every run that predates the redesign has class 0, because the
            // column did not exist when it was created. The generator filters
            // on class, so it has to be right from the first login after the
            // migration rather than from the next new run.
            if (st.playerClass == 0)
            {
                st.playerClass = player->getClass();
                CharacterDatabase.Execute("UPDATE `gauntlet_run` SET `class` = {} WHERE `guid` = {}",
                                          static_cast<uint32>(st.playerClass), low);
            }

            // A run that cannot belong to the character now holding this GUID.
            // The core reuses the GUIDs of deleted characters, so a run whose
            // class does not match, or whose tier is further than this
            // character's level could ever have reached, belonged to a previous
            // occupant. Purging on delete is the real fix; this catches the rows
            // already orphaned before that existed, and any the delete missed.
            bool const wrongClass = st.playerClass != 0 && st.playerClass != player->getClass();
            // Fewer levels than tiers is impossible under any TierInterval of 1
            // or more, so this stays true if an admin ever retunes the interval.
            // A stricter level * interval test would purge every legitimate run
            // the day someone raised it.
            bool const impossibleTier = st.tier > 0 && player->GetLevel() < st.tier;

            if (wrongClass || impossibleTier)
            {
                LOG_INFO("module.gauntlet",
                         "Gauntlet: discarding a stale run on guid {} ({}), it belonged to a character that no "
                         "longer exists; starting a fresh run for {}.",
                         low, wrongClass ? "class does not match" : "tier is beyond this character's level",
                         player->GetName());

                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                PurgeCharacter(low, trans);
                CharacterDatabase.CommitTransaction(trans);

                st = RunState();
                st.seed = static_cast<uint32>(low * 2654435761u)
                        ^ static_cast<uint32>(GameTime::GetGameTime().count());
                st.genVersion  = GeneratorVersion;
                st.playerClass = player->getClass();
                CharacterDatabase.Execute(
                    "INSERT INTO `gauntlet_run` (`guid`, `seed`, `tier`, `dead`, `gen_version`, `class`) "
                    "VALUES ({}, {}, 0, 0, {}, {})",
                    low, st.seed, static_cast<uint32>(st.genVersion), static_cast<uint32>(st.playerClass));

                _runs[player->GetGUID()] = std::move(st);
                return;
            }
        }
        else
        {
            // New run: derive a seed that is stable for this character.
            st.seed = static_cast<uint32>(low * 2654435761u) ^ static_cast<uint32>(GameTime::GetGameTime().count());
            st.genVersion  = GeneratorVersion;
            st.playerClass = player->getClass();
            CharacterDatabase.Execute(
                "INSERT INTO `gauntlet_run` (`guid`, `seed`, `tier`, `dead`, `gen_version`, `class`) "
                "VALUES ({}, {}, 0, 0, {}, {})",
                low, st.seed, static_cast<uint32>(st.genVersion), static_cast<uint32>(st.playerClass));
        }

        // What was picked, read straight out of the columns. Nothing is
        // re-rolled here any more: that was the bug the redesign's schema
        // exists to fix, because it meant a change to the generator rewrote
        // every live run.
        if (QueryResult r = CharacterDatabase.Query(
                "SELECT `slot`, `mechanic`, `rank`, `cond`, `boon`, `boon_mag`, `legacy_mag`, `gen_version` "
                "FROM `gauntlet_affix` WHERE `guid` = {} ORDER BY `slot` ASC", low))
        {
            do
            {
                Field* f = r->Fetch();

                AffixInstance a;
                a.slot       = f[0].Get<uint8>();
                a.mechanic   = f[1].Get<uint16>();
                a.rank       = f[2].Get<uint8>();
                a.condition  = static_cast<Condition>(f[3].Get<uint8>());
                a.boon       = static_cast<Boon>(f[4].Get<uint8>());
                a.boonMag    = f[5].Get<uint8>();
                a.legacyMag  = f[6].Get<uint16>();
                a.genVersion = f[7].Get<uint16>();

                // A row the migration could not convert, or one written by a
                // future generator this build does not understand. Skipping it
                // is the honest answer; inventing a mechanic for it is not.
                if (a.mechanic == MECHANIC_NONE)
                {
                    LOG_ERROR("server.loading",
                              "Gauntlet: character {} carries an unconverted affix in slot {}; skipped.",
                              low, static_cast<uint32>(a.slot));
                    continue;
                }

                if (a.condition >= Condition::MAX)
                    a.condition = Condition::Always;

                loaded.push_back(a);
            } while (r->NextRow());
        }

        _runs[player->GetGUID()] = std::move(st);

        // The affixes are attached only once the run is in the map, because
        // Ctx::run has to point at the run the module will keep rather than at
        // a local about to be moved from -- and because Attach is what creates
        // the implementation, so going through it here is what keeps RunState
        // the single owner.
        RunState* run = Get(player);
        if (!run)
            return;

        // OnAttach is handed a pointer into this vector, so it must not move
        // under the loop.
        run->affixes.reserve(loaded.size());

        for (AffixInstance const& a : loaded)
        {
            AffixInstance& stored = run->Attach(a);
            if (!stored.impl)
                continue;

            Ctx ctx;
            ctx.player = player;
            ctx.run    = run;
            ctx.self   = &stored;
            stored.impl->OnAttach(ctx);
        }
    }

    void Mgr::Save(Player* player)
    {
        RunState* st = Get(player);
        if (!st || !player)
            return;

        // gauntlet_affix rows are written when they are picked and never
        // rewritten, so a save is one row of gauntlet_run and only when
        // something on it moved.
        if (!st->dirty)
            return;

        CharacterDatabase.Execute("UPDATE `gauntlet_run` SET `tier` = {}, `dead` = {} WHERE `guid` = {}",
                                  st->tier, st->dead ? 1 : 0, player->GetGUID().GetCounter());
        st->dirty = false;
    }

    void Mgr::PurgeCharacter(uint32 lowGuid, CharacterDatabaseTransaction trans)
    {
        // The core hands out the GUIDs of deleted characters again. Without
        // this the next character created on the same GUID inherits the run:
        // its retired flag, its tier and every affix it carried.
        for (char const* table : { "gauntlet_run", "gauntlet_affix", "gauntlet_affix_log", "gauntlet_state" })
            trans->Append(Acore::StringFormat("DELETE FROM `{}` WHERE `guid` = {}", table, lowGuid).c_str());
    }

    void Mgr::Forget(ObjectGuid guid)
    {
        auto it = _runs.find(guid);
        if (it == _runs.end())
            return;

        if (it->second.pendingDeath && _pendingDeaths != 0)
            --_pendingDeaths;

        // ~RunState frees every IMechanic the run owned.
        _runs.erase(it);
    }

    void Mgr::OfferTier(Player* player, uint32 tier)
    {
        RunState* st = Get(player);
        if (!st || st->dead)
            return;

        // BuildOffers takes a uint8 tier because AffixInstance::slot is one;
        // a run cannot reach tier 256 with the level cap at 80 and an interval
        // of at least one, but the cast is clamped rather than assumed.
        uint8 const genTier = static_cast<uint8>(std::min<uint32>(tier, 255u));

        LivePlayerView view(player);
        OfferSet const set = BuildOffers(st->seed, genTier, view, st->affixes, _choices);

        st->pending     = set.offers;
        st->pendingTier = tier;

        ChatHandler ch(player->GetSession());
        ch.PSendSysMessage("|cffff2020[Gauntlet]|r Tier {} reached. Choose your affix:", tier);
        for (uint32 i = 0; i < st->pending.size(); ++i)
        {
            Offer const& o = st->pending[i];

            // The generator returns an empty offer when nothing in the table
            // fits this character at this tier. Printing it as a numbered line
            // the player cannot take would be worse than saying so.
            if (o.mechanic == MECHANIC_NONE)
            {
                ch.PSendSysMessage("  |cffffff00{}.|r {} - {}", i + 1, "Nothing",
                                   "No affix is available to you at this tier.");
                continue;
            }

            AffixInstance preview;
            preview.mechanic  = o.mechanic;
            preview.rank      = o.rank;
            preview.condition = o.condition;
            preview.boon      = o.boon;
            preview.boonMag   = o.boonMag;

            // An offer is not carried, so no RunState owns it, but describing
            // it still needs the implementation. It lives for one chat line.
            std::unique_ptr<IMechanic> const impl(MakeMechanic(o.mechanic));
            preview.impl = impl.get();

            ch.PSendSysMessage("  |cffffff00{}.|r {} - {}", i + 1,
                               NameOf(o.mechanic, o.condition, o.boon), DescribeOf(preview));
        }
        ch.PSendSysMessage("Use |cff00ff00.gauntlet pick <number>|r to commit. It cannot be undone.");
    }

    bool Mgr::Pick(Player* player, uint32 index)
    {
        RunState* st = Get(player);
        if (!st || st->dead || st->pending.empty() || index == 0 || index > st->pending.size())
            return false;

        Offer const chosen = st->pending[index - 1];
        if (chosen.mechanic == MECHANIC_NONE)
            return false;

        uint32 const low  = player->GetGUID().GetCounter();
        uint32 const tier = st->pendingTier != 0 ? st->pendingTier : st->tier + 1;
        uint8 const  slot = static_cast<uint8>(std::min<uint32>(tier, 255u));

        st->tier  = tier;
        st->dirty = true;
        st->pending.clear();
        st->pendingTier = 0;

        // A swap deletes a row and writes two log lines; doing that in three
        // separate async statements would leave a window in which the run has
        // lost an affix and not yet gained one. One transaction, so the table
        // never disagrees with itself.
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        AffixInstance* held = st->Find(chosen.mechanic);

        if (chosen.kind == OfferKind::RankUp && held)
        {
            // In place, in the same slot: plan section 3.1. The slot is the
            // tier the affix was first taken at and does not move when it
            // grows, which is what makes gauntlet_affix_log the record of
            // when it grew.
            held->rank = chosen.rank;

            trans->Append("UPDATE `gauntlet_affix` SET `rank` = {} WHERE `guid` = {} AND `slot` = {}",
                          static_cast<uint32>(chosen.rank), low, static_cast<uint32>(held->slot));
            trans->Append(
                "INSERT INTO `gauntlet_affix_log` (`guid`, `tier`, `action`, `mechanic`, `rank`, `gen_version`) "
                "VALUES ({}, {}, 'rankup', {}, {}, {})",
                low, static_cast<uint32>(slot), static_cast<uint32>(chosen.mechanic),
                static_cast<uint32>(chosen.rank), static_cast<uint32>(GeneratorVersion));
        }
        else
        {
            // Swap first: the discarded affix leaves the table, and its
            // departure is logged before the arrival so the log reads in the
            // order the run happened.
            if (chosen.kind == OfferKind::Swap)
            {
                if (AffixInstance* out = st->AtSlot(chosen.swapSlot))
                {
                    trans->Append(
                        "INSERT INTO `gauntlet_affix_log` "
                        "(`guid`, `tier`, `action`, `mechanic`, `rank`, `gen_version`) "
                        "VALUES ({}, {}, 'swap_out', {}, {}, {})",
                        low, static_cast<uint32>(slot), static_cast<uint32>(out->mechanic),
                        static_cast<uint32>(out->rank), static_cast<uint32>(out->genVersion));
                    trans->Append("DELETE FROM `gauntlet_affix` WHERE `guid` = {} AND `slot` = {}",
                                  low, static_cast<uint32>(chosen.swapSlot));

                    // Detach before insert: OnDetach may want the run to still
                    // look the way it did while the affix was carried.
                    if (out->impl)
                    {
                        Ctx ctx;
                        ctx.player = player;
                        ctx.run    = st;
                        ctx.self   = out;
                        out->impl->OnDetach(ctx);
                    }
                    st->DetachSlot(chosen.swapSlot);
                }
            }

            AffixInstance instance;
            instance.mechanic   = chosen.mechanic;
            instance.rank       = chosen.rank;
            instance.condition  = chosen.condition;
            instance.boon       = chosen.boon;
            instance.boonMag    = chosen.boonMag;
            instance.slot       = slot;
            instance.genVersion = GeneratorVersion;
            instance.legacyMag  = 0;   // generator 2: the rank is the strength

            AffixInstance& stored = st->Attach(instance);
            if (stored.impl)
            {
                Ctx ctx;
                ctx.player = player;
                ctx.run    = st;
                ctx.self   = &stored;
                stored.impl->OnAttach(ctx);
            }

            // REPLACE rather than INSERT so a client that manages to pick
            // twice for one tier overwrites its own row instead of failing on
            // the (guid, slot) key and losing the affix entirely.
            trans->Append(
                "REPLACE INTO `gauntlet_affix` "
                "(`guid`, `slot`, `mechanic`, `rank`, `cond`, `boon`, `boon_mag`, `legacy_mag`, `gen_version`) "
                "VALUES ({}, {}, {}, {}, {}, {}, {}, 0, {})",
                low, static_cast<uint32>(slot), static_cast<uint32>(chosen.mechanic),
                static_cast<uint32>(chosen.rank), static_cast<uint32>(chosen.condition),
                static_cast<uint32>(chosen.boon), static_cast<uint32>(chosen.boonMag),
                static_cast<uint32>(GeneratorVersion));
            // A rank-up whose mechanic is not actually carried lands here, as
            // a new affix; the log has to say what happened rather than what
            // was offered.
            char const* const action = chosen.kind == OfferKind::RankUp ? "pick" : LogAction(chosen.kind);

            trans->Append(
                "INSERT INTO `gauntlet_affix_log` (`guid`, `tier`, `action`, `mechanic`, `rank`, `gen_version`) "
                "VALUES ({}, {}, '{}', {}, {}, {})",
                low, static_cast<uint32>(slot), action,
                static_cast<uint32>(chosen.mechanic), static_cast<uint32>(chosen.rank),
                static_cast<uint32>(GeneratorVersion));
        }

        trans->Append("UPDATE `gauntlet_run` SET `tier` = {}, `dead` = {} WHERE `guid` = {}",
                      st->tier, st->dead ? 1 : 0, low);
        CharacterDatabase.CommitTransaction(trans);
        st->dirty = false;

        AffixInstance const* carried = st->Find(chosen.mechanic);
        std::string const describe = carried ? DescribeOf(*carried) : std::string();
        std::string const name     = NameOf(chosen.mechanic, chosen.condition, chosen.boon);

        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cffff2020[Gauntlet]|r You bear |cffffff00{}|r. {}", name, describe);

        if (_announce)
            sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING,
                Acore::StringFormat("[Gauntlet] {} reached tier {} and took {}.",
                                    player->GetName(), st->tier, name));
        return true;
    }

    void Mgr::EndRun(Player* player, std::string const& cause)
    {
        RunState* st = Get(player);
        if (!st || st->dead)
            return;

        st->dead  = true;
        st->dirty = true;
        if (st->pendingDeath)
        {
            st->pendingDeath = false;
            st->deathTimerMs = 0;
            if (_pendingDeaths != 0)
                --_pendingDeaths;
        }
        Save(player);

        // Plan section 3.4: conducts are the class curses the run carried,
        // affixes is everything it carried. Both are read by `.gauntlet top`
        // and by the addon's TOP line.
        //
        // Phase 0 cannot produce a conduct. The Class family is entirely
        // MF_NotImplemented, so the generator never offers one and no carried
        // affix can be Family::Class -- this loop is written for Phase 4 and
        // will hold an empty string until then. It is here now so the column
        // is never left behind when the family lands.
        std::string conducts;
        std::string affixes;
        for (AffixInstance const& a : st->affixes)
        {
            MechanicDef const* def = FindMechanic(a.mechanic);
            std::string const entry = (def ? std::string(def->name)
                                           : "#" + std::to_string(static_cast<uint32>(a.mechanic)))
                                    + " " + RankNumeral(a.rank);

            if (!affixes.empty())
                affixes += ", ";
            affixes += entry;

            if (def && def->family == Family::Class)
            {
                if (!conducts.empty())
                    conducts += ", ";
                conducts += entry;
            }
        }

        // conducts is VARCHAR(255); a value that does not fit is truncated
        // here rather than by MySQL, which in strict mode would abort the
        // whole statement and lose the leaderboard row.
        if (conducts.size() > 255)
            conducts.resize(255);

        std::string name = player->GetName();
        CharacterDatabase.EscapeString(name);
        CharacterDatabase.EscapeString(conducts);
        CharacterDatabase.EscapeString(affixes);
        std::string escapedCause = cause;
        CharacterDatabase.EscapeString(escapedCause);

        uint32 const low = player->GetGUID().GetCounter();
        CharacterDatabase.Execute(
            "REPLACE INTO `gauntlet_leaderboard` "
            "(`guid`, `name`, `tier`, `level`, `cause`, `conducts`, `affixes`, `ended`) "
            "VALUES ({}, '{}', {}, {}, '{}', '{}', '{}', NOW())",
            low, name, st->tier, player->GetLevel(), escapedCause, conducts, affixes);

        if (_announce)
            sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING,
                Acore::StringFormat("[Gauntlet] {} has fallen at level {} on tier {} ({}).",
                                    player->GetName(), player->GetLevel(), st->tier, cause));
    }

    void Mgr::DetachAll(Player* player)
    {
        RunState* st = Get(player);
        if (!st)
            return;

        for (AffixInstance& a : st->affixes)
        {
            if (!a.impl)
                continue;

            Ctx ctx;
            ctx.player = player;
            ctx.run    = st;
            ctx.self   = &a;
            a.impl->OnDetach(ctx);
        }
    }

    // =====================================================================
    // The death sequence.
    // =====================================================================
    void Mgr::BeginPendingDeath(Player* player)
    {
        RunState* st = Get(player);
        if (!st || st->dead || st->pendingDeath)
            return;

        st->pendingDeath = true;
        st->deathTimerMs = _graceMs;
        ++_pendingDeaths;
    }

    bool Mgr::CancelPendingDeath(Player* player)
    {
        RunState* st = Get(player);
        if (!st || !st->pendingDeath)
            return false;

        st->pendingDeath = false;
        st->deathTimerMs = 0;
        if (_pendingDeaths != 0)
            --_pendingDeaths;
        return true;
    }

    bool Mgr::IsPendingDeath(Player* player) const
    {
        if (!player)
            return false;
        auto it = _runs.find(player->GetGUID());
        return it != _runs.end() && it->second.pendingDeath;
    }

    void Mgr::UpdateDeathTimer(Player* player, uint32 diffMs)
    {
        RunState* st = Get(player);
        if (!st || !st->pendingDeath)
            return;

        if (st->deathTimerMs > diffMs)
        {
            st->deathTimerMs -= diffMs;
            return;
        }

        // The timer, not the release, is what ended it; the cause is the same
        // either way because the character is just as dead.
        EndRun(player, "slain");
    }

    // =====================================================================
    // Conditions and the aggregate.
    // =====================================================================
    bool Mgr::ConditionActive(Player* player, Condition c) const
    {
        if (!player)
            return false;

        switch (c)
        {
            case Condition::Always:          return true;
            case Condition::InCombat:        return player->IsInCombat();
            case Condition::OutOfCombat:     return !player->IsInCombat();
            case Condition::BelowHalfHealth: return player->GetHealthPct() < 50.0f;
            case Condition::AboveHalfHealth: return player->GetHealthPct() >= 50.0f;
            case Condition::WhileSolo:       return !player->GetGroup();
            case Condition::WhileGrouped:    return player->GetGroup() != nullptr;
            case Condition::InDungeon:       return player->GetMap() && player->GetMap()->IsDungeon();
            case Condition::InOpenWorld:     return player->GetMap() && !player->GetMap()->IsDungeon();
            case Condition::VersusPlayers:   return player->GetMap() && player->GetMap()->IsBattlegroundOrArena();
            case Condition::WhileMounted:    return player->IsMounted();
            case Condition::WhileMoving:     return player->isMoving();
            case Condition::WhileStationary: return !player->isMoving();
            case Condition::AtNight:
            case Condition::AtDay:
            {
                time_t const t = GameTime::GetGameTime().count();
                tm lt{};
                localtime_r(&t, &lt);
                bool const night = lt.tm_hour >= 20 || lt.tm_hour < 6;
                return c == Condition::AtNight ? night : !night;
            }
            // Evaluated at the damage site where the target is known; treated as
            // inactive for ambient stat queries.
            case Condition::VersusElites:    return false;
            default:                         return false;
        }
    }

    void Mgr::FillConditions(Player* player, AggregateInput& in) const
    {
        for (uint8 c = 0; c < static_cast<uint8>(Condition::MAX); ++c)
            in.conditionActive[c] = ConditionActive(player, static_cast<Condition>(c));
    }

    float Mgr::Aggregate(Player* player, AggregateKind kind) const
    {
        if (!_enabled || !IsEligible(player))
            return 1.0f;

        auto it = _runs.find(player->GetGUID());
        if (it == _runs.end())
            return 1.0f;

        AggregateInput in;
        in.kind = kind;
        FillConditions(player, in);

        // Qualified so the free function in GauntletAggregate.h is called and
        // not this member: unqualified lookup inside a member finds the member.
        return Gauntlet::Aggregate(it->second.affixes, in, _caps);
    }

    // =====================================================================
    // The one-shot migration (plan section 3.6).
    // =====================================================================
    void Mgr::MigrateLegacyRuns()
    {
        // The SQL update adds the redesign's columns and re-keys the table but
        // deliberately leaves `roll` and `tier` behind, because unrolling a
        // legacy affix needs splitmix and SQL cannot run it. The surviving
        // `roll` column is therefore the marker for "this install has not been
        // converted yet", and dropping it at the end is what makes this a
        // no-op on every later start.
        QueryResult marker = CharacterDatabase.Query(
            "SELECT COUNT(*) FROM information_schema.COLUMNS "
            "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_affix' AND COLUMN_NAME = 'roll'");
        if (!marker || marker->Fetch()[0].Get<uint64>() == 0)
            return;

        // A row whose run is gone cannot be unrolled: the seed is half of
        // LegacyRoll's input. Counting those first, rather than carrying an
        // IS NULL through the join, keeps the loop below reading only rows it
        // can actually convert -- and the count is what stops the legacy
        // columns being dropped while anything is still unreadable.
        uint32 unresolved = 0;
        if (QueryResult orphans = CharacterDatabase.Query(
                "SELECT COUNT(*) FROM `gauntlet_affix` a LEFT JOIN `gauntlet_run` r ON r.`guid` = a.`guid` "
                "WHERE a.`mechanic` = 0 AND r.`guid` IS NULL"))
        {
            unresolved = static_cast<uint32>(orphans->Fetch()[0].Get<uint64>());
            if (unresolved != 0)
                LOG_ERROR("server.loading",
                          "Gauntlet: {} legacy affix row(s) have no gauntlet_run and cannot be unrolled.",
                          unresolved);
        }

        QueryResult rows = CharacterDatabase.Query(
            "SELECT a.`guid`, a.`slot`, a.`tier`, a.`roll`, r.`seed` "
            "FROM `gauntlet_affix` a JOIN `gauntlet_run` r ON r.`guid` = a.`guid` "
            "WHERE a.`mechanic` = 0");

        uint32 converted = 0;

        if (rows)
        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            do
            {
                Field* f = rows->Fetch();
                uint32 const guid    = f[0].Get<uint32>();
                uint32 const slot    = f[1].Get<uint32>();
                uint32 const tier    = f[2].Get<uint32>();
                uint32 const roll    = f[3].Get<uint32>();
                uint32 const seed    = f[4].Get<uint32>();

                // The affix this character has actually been playing with,
                // reproduced by the frozen generator-1 roll.
                Affix const a = LegacyRoll(seed, tier, roll);

                uint16 const mechanic = MechanicForEffect(a.effect);
                if (mechanic == MECHANIC_NONE)
                {
                    ++unresolved;
                    LOG_ERROR("server.loading",
                              "Gauntlet: affix row (guid {}, slot {}) unrolled to effect {}, which has no "
                              "registry id; left unconverted.",
                              guid, slot, static_cast<uint32>(a.effect));
                    continue;
                }

                // legacy_mag is the whole point of the migration: generator 1's
                // magnitude is a free percentage in 2..115 and rounding it onto
                // the three-step rank ladder would change a live hardcore run.
                // The mechanics read it in preference to the rank (step 4a), so
                // the number the player has been playing with survives exactly.
                uint32 const legacyMag = std::min<uint32>(a.magnitude, 65535u);
                uint32 const boonMag   = std::min<uint32>(a.boonMagnitude, 255u);

                // Idempotent on its own: the `mechanic` = 0 predicate means a
                // second pass over the same row updates nothing. That is what
                // makes an interrupted migration safe to resume rather than
                // needing the transaction to have committed.
                trans->Append(
                    "UPDATE `gauntlet_affix` SET `mechanic` = {}, `rank` = {}, `cond` = {}, `boon` = {}, "
                    "`boon_mag` = {}, `legacy_mag` = {}, `gen_version` = 1 "
                    "WHERE `guid` = {} AND `slot` = {} AND `mechanic` = 0",
                    static_cast<uint32>(mechanic), static_cast<uint32>(RankFromSeverity(a.severity)),
                    static_cast<uint32>(a.condition), static_cast<uint32>(a.boon),
                    boonMag, legacyMag, guid, slot);
                ++converted;
            } while (rows->NextRow());

            // Synchronous: the ALTER below must not overtake these, and MySQL
            // commits implicitly before DDL anyway.
            CharacterDatabase.DirectCommitTransaction(trans);
        }

        if (unresolved != 0)
        {
            LOG_ERROR("server.loading",
                      "Gauntlet: converted {} legacy affix row(s), but {} could not be unrolled. "
                      "gauntlet_affix.roll and .tier are kept so this can be retried; look at the rows "
                      "with mechanic = 0 by hand.",
                      converted, unresolved);
            return;
        }

        // Only now, with every row carrying a mechanic, do the legacy columns
        // go. Dropping them is what makes the table byte-identical to the one
        // base/gauntlet.sql builds on a fresh install, and what makes this
        // routine a no-op from the next start on.
        CharacterDatabase.DirectExecute("ALTER TABLE `gauntlet_affix` DROP COLUMN `roll`, DROP COLUMN `tier`");

        LOG_INFO("server.loading",
                 "Gauntlet: migrated {} legacy affix row(s) to the redesign schema and dropped "
                 "gauntlet_affix.roll and .tier.", converted);
    }
}
