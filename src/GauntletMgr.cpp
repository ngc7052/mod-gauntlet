/*
 * mod-gauntlet - run state, persistence and effect aggregation
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMgr.h"
#include "GauntletAddon.h"
#include "GauntletGenerator.h"
#include "GauntletMechanic.h"
#include "GauntletRegistry.h"
#include "GauntletSummons.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "World.h"
#include "WorldSessionMgr.h"
#include "GameTime.h"
#include "WorldSession.h"
#include <algorithm>
#include <limits>
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
        graceMs      = other.graceMs;
        lastActor    = other.lastActor;
        lastActorMs  = other.lastActorMs;

        affixes = std::move(other.affixes);
        pending = std::move(other.pending);
        state   = std::move(other.state);

        // A moved-from vector is valid but unspecified. Making it definitely
        // empty is what guarantees the source's destructor frees nothing.
        other.affixes.clear();
        other.pending.clear();
        other.state.Clear();
        other.pendingDeath = false;
        other.deathTimerMs = 0;
        other.lastActor    = MECHANIC_NONE;
        other.lastActorMs  = 0;
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

        // How often the key/value store is written while a character is
        // logged in. CONTRACT-P1 section 5.2 states the sixty seconds.
        constexpr uint32 STATE_SAVE_MS = 60000;

        // Caps wide enough that ClampProduct in GauntletAggregate.cpp is the
        // identity, so the shared -- and unit-tested -- affix maths can be
        // asked for the raw product and the clamp applied later, once, over a
        // number it has not seen.
        AggregateCaps const& UncappedCaps()
        {
            static AggregateCaps const caps = []
            {
                AggregateCaps c;
                c.damageTakenMin = 0.0f;
                c.damageTakenMax = std::numeric_limits<float>::max();
                c.damageDoneMin  = 0.0f;
                c.healTakenMin   = 0.0f;
                c.maxHealthMin   = 0.0f;
                c.enemySpeedMax  = std::numeric_limits<float>::max();
                return c;
            }();
            return caps;
        }

        // Deliberately the same switch as ClampProduct in
        // GauntletAggregate.cpp, and deliberately not shared with it: that one
        // is file-local to the Player-free translation unit, and what has to be
        // clamped here is a product that unit never sees -- one with the three
        // IMechanic Mult callbacks folded in. Plan section 2.5 is the authority
        // for both. Change them together.
        float ClampToCaps(float v, AggregateKind kind, AggregateCaps const& caps)
        {
            switch (kind)
            {
                case AggregateKind::DamageTaken:
                    return std::max(caps.damageTakenMin, std::min(caps.damageTakenMax, v));
                case AggregateKind::DamageDone:  return std::max(caps.damageDoneMin, v);
                case AggregateKind::HealTaken:   return std::max(caps.healTakenMin, v);
                case AggregateKind::MaxHealth:   return std::max(caps.maxHealthMin, v);
                case AggregateKind::EnemySpeed:  return std::min(caps.enemySpeedMax, v);
                case AggregateKind::Experience:  return v;
                default:                         return v;
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

        // Gauntlet.Grace.Seconds: the window after login or a zone change in
        // which no scheduled event fires, which is what the conf file has
        // always documented it as and what Phase 1 finally makes it do.
        _graceMs = sConfigMgr->GetOption<uint32>("Gauntlet.Grace.Seconds", 60) * 1000;

        // Phase 1 gives that key the consumer the conf file documents -- the
        // window after login or a zone change in which no event fires -- and
        // the hardcore death window keeps its own field rather than sharing the
        // variable. They read the same number today because plan section 6
        // decision 5 names the same sixty seconds and there is no second key;
        // they are separate so that a tuning pass which shortens the grace
        // cannot silently shorten the window a hardcore character has to be
        // saved in. See the TODO on _deathWindowMs.
        _deathWindowMs = _graceMs;

        // The event scheduler. MinSpacing is configured in seconds and
        // Scheduler::SetMinSpacingMs takes milliseconds; this multiplication is
        // the only place the two units meet.
        _eventsEnabled = sConfigMgr->GetOption<bool>("Gauntlet.Events.Enable", true);
        _minSpacingMs  = sConfigMgr->GetOption<uint32>("Gauntlet.Events.MinSpacing", 12) * 1000;
        _budgetStep    = sConfigMgr->GetOption<float>("Gauntlet.Events.BudgetStep", 0.25f);

        // A kill on a creature this module put into the world. Clamped to
        // [0, 1] because the key exists to stop summons being farmed, and a
        // value above 1 would turn it into the opposite.
        _summonXpRate = sConfigMgr->GetOption<float>("Gauntlet.Summons.XpRate", 0.5f);
        _summonXpRate = std::max(0.0f, std::min(1.0f, _summonXpRate));

        if (_interval == 0)
            _interval = 5;
        if (_choices == 0)
            _choices = 1;
        if (_graceMs == 0)
            _graceMs = 60000;
        if (_deathWindowMs == 0)
            _deathWindowMs = 60000;
        if (_budgetStep < 0.0f)
            _budgetStep = 0.0f;

        // This runs again on `.reload config`, with runs already loaded, so the
        // schedulers that exist have to be told rather than only the ones
        // created afterwards.
        for (auto& entry : _live)
        {
            entry.second.clock.SetMinSpacingMs(_minSpacingMs);
            entry.second.clock.SetBudgetStep(_budgetStep);
        }

        sGauntletSummons->LoadConfig();
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

    Mgr::Live* Mgr::LiveFor(Player* player)
    {
        if (!player)
            return nullptr;
        auto it = _live.find(player->GetGUID());
        return it == _live.end() ? nullptr : &it->second;
    }

    Scheduler* Mgr::ClockFor(Player* player)
    {
        Live* live = LiveFor(player);
        return live ? &live->clock : nullptr;
    }

    Ctx Mgr::MakeCtx(Player* player, RunState* run, AffixInstance* self)
    {
        Ctx ctx;
        ctx.player = player;
        ctx.run    = run;
        ctx.self   = self;
        ctx.clock  = ClockFor(player);
        ctx.addon  = sGauntletAddon;
        ctx.state  = run ? &run->state : nullptr;
        return ctx;
    }

    template <typename Fn>
    void Mgr::ForEachMechanic(Player* player, RunState* run, Fn&& fn)
    {
        if (!run)
            return;

        // Indexed rather than range-for: a callback may not add or remove a
        // carried affix -- nothing reachable from one does -- but taking the
        // address fresh each time is what makes that assumption cheap to check
        // rather than something the loop silently depends on.
        for (std::size_t i = 0; i < run->affixes.size(); ++i)
        {
            AffixInstance& a = run->affixes[i];
            if (!a.impl)
                continue;

            Ctx ctx = MakeCtx(player, run, &a);
            fn(ctx, a);
        }
    }

    std::string Mgr::NameOf(uint16 mechanic, Condition condition, Boon boon) const
    {
        MechanicDef const* def = FindMechanic(mechanic);

        // A stored id this build does not carry still has to be printable:
        // the run is a live character's and refusing to name it is worse than
        // naming it badly.
        std::string name = def ? std::string(def->name) : ("#" + std::to_string(static_cast<uint32>(mechanic)));

        // Affix::Name() built "Wrathful Desperate Exposed" out of the boon, the
        // condition and the effect, and the shape is kept: the addon's chat
        // fallback matches on it, and the boon adjective is how a player reads
        // at a glance that an offer comes with an upside.
        //
        // Condition::Always no longer contributes a word. It used to -- every
        // affix printed as "Everlasting Something" -- and that was harmless
        // only while the Scalars existed to make the axis mean something. With
        // them deleted every offer carries Always, so the adjective would be on
        // every name in the game and would say nothing at all. A real condition
        // still prints, because the axis itself is kept for a later phase.
        if (condition != Condition::Always)
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

        // One scheduler per loaded run, created here and erased by Forget, so
        // its lifetime is exactly the run's. CancelAll rather than a fresh
        // object because a relog inside the same worldserver session must not
        // inherit a queue, and the config has to be re-applied either way.
        Live& live = _live[player->GetGUID()];
        live.clock.CancelAll();
        live.clock.SetTimedAffixCount(0);
        live.clock.SetMinSpacingMs(_minSpacingMs);
        live.clock.SetBudgetStep(_budgetStep);
        live.tickMs      = 0;
        live.stateSaveMs = 0;

        // The character's own creation date, as a unix timestamp, which is the
        // one fact about a guid that cannot be inherited: a character created
        // into a recycled guid has a new one. Read once here rather than joined
        // into the run query, because a fresh run needs it too.
        uint32 charCreated = 0;
        if (QueryResult r = CharacterDatabase.Query(
                "SELECT UNIX_TIMESTAMP(`creation_date`) FROM `characters` WHERE `guid` = {}", low))
            charCreated = r->Fetch()[0].Get<uint32>();

        if (QueryResult r = CharacterDatabase.Query(
                "SELECT `seed`, `tier`, `dead`, `gen_version`, `class`, `char_created` "
                "FROM `gauntlet_run` WHERE `guid` = {}", low))
        {
            Field* f       = r->Fetch();
            st.seed        = f[0].Get<uint32>();
            st.tier        = f[1].Get<uint32>();
            st.dead        = f[2].Get<uint8>() != 0;
            st.genVersion  = f[3].Get<uint16>();
            st.playerClass = f[4].Get<uint8>();

            uint32 const storedCreated = f[5].Get<uint32>();

            // A run that predates the column, or one whose character row could
            // not be read, has no evidence either way. Backfill it and move on:
            // purging a live hardcore run on no evidence is worse than keeping
            // one that should have gone, and the class test below still stands.
            if (storedCreated == 0 && charCreated != 0)
                CharacterDatabase.Execute("UPDATE `gauntlet_run` SET `char_created` = {} WHERE `guid` = {}",
                                          charCreated, low);

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

            // A run that cannot belong to the character now holding this guid.
            // The core reuses the guids of deleted characters, so a run keyed on
            // the guid alone is inherited by whoever is created next -- retired
            // flag, tier and every affix. Purging on delete is the real fix
            // (OnPlayerDeleteFromDB, below); this catches the rows already
            // orphaned before that existed, and any the delete missed.
            //
            // The test is the character's creation date, and it replaces Phase
            // 0's "fewer levels than tiers" heuristic outright. That heuristic
            // could not tell real guid reuse from deliberate testing: a game
            // master who levels a character *down* to test an affix at level 10
            // produces exactly the shape it purged, and every character built
            // with `.gauntlet debug` was being eaten on its next login. A
            // creation date is evidence rather than an inference -- it is a
            // fact about the character, not about the state of its run -- so a
            // character may now be levelled anywhere at all and keep its run.
            //
            // Both sides have to be known for the comparison to mean anything.
            // Zero on either is "cannot say", and cannot-say is not a mismatch.
            bool const wrongClass   = st.playerClass != 0 && st.playerClass != player->getClass();
            bool const wrongCharacter = storedCreated != 0 && charCreated != 0
                                     && storedCreated != charCreated;

            if (wrongClass || wrongCharacter)
            {
                LOG_INFO("module.gauntlet",
                         "Gauntlet: discarding a stale run on guid {} ({}), it belonged to a character that no "
                         "longer exists; starting a fresh run for {}.",
                         low, wrongCharacter ? "the character was created at a different time"
                                             : "class does not match",
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
                    "INSERT INTO `gauntlet_run` "
                    "(`guid`, `seed`, `tier`, `dead`, `char_created`, `gen_version`, `class`) "
                    "VALUES ({}, {}, 0, 0, {}, {}, {})",
                    low, st.seed, charCreated, static_cast<uint32>(st.genVersion),
                    static_cast<uint32>(st.playerClass));

                st.graceMs = _graceMs;

                // The fresh RunState carries an empty State, and the move
                // assignment below replaces whatever the previous occupant of
                // this guid had left in the map. That matters: PurgeCharacter
                // has just deleted the gauntlet_state rows, and a State object
                // still holding them in memory would write them straight back.
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
                "INSERT INTO `gauntlet_run` "
                "(`guid`, `seed`, `tier`, `dead`, `char_created`, `gen_version`, `class`) "
                "VALUES ({}, {}, 0, 0, {}, {}, {})",
                low, st.seed, charCreated, static_cast<uint32>(st.genVersion),
                static_cast<uint32>(st.playerClass));
        }

        // What was picked, read straight out of the columns. Nothing is
        // re-rolled here any more: that was the bug the redesign's schema
        // exists to fix, because it meant a change to the generator rewrote
        // every live run.
        if (QueryResult r = CharacterDatabase.Query(
                "SELECT `slot`, `mechanic`, `rank`, `cond`, `boon`, `boon_mag`, `gen_version` "
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
                a.genVersion = f[6].Get<uint16>();

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

        // Before OnAttach, and that order is the whole point of doing it here:
        // the Shade reads shade.rank and shade.alive in OnAttach, and a state
        // store still empty at that moment would answer "never left behind" for
        // a nemesis that has been left behind four times. One query.
        run->state.LoadFrom(low);

        // No event fires for the first minute of a session. Design section
        // 4.2: a character is never ambushed before the player has taken
        // control of it.
        run->graceMs     = _graceMs;
        run->lastActor   = MECHANIC_NONE;
        run->lastActorMs = 0;

        // OnAttach is handed a pointer into this vector, so it must not move
        // under the loop below.
        run->affixes.reserve(loaded.size());
        for (AffixInstance const& a : loaded)
            run->Attach(a);

        // Attach first, then the budget, then OnAttach. The order is the point:
        // Budget() is 1 + step x (timed affixes - 1), a mechanic arms its first
        // event from inside OnAttach, and Scheduler::Arm scales the interval
        // once at arming time -- so a count taken afterwards would leave every
        // affix's first event of the session unstretched.
        SyncTimedAffixCount(player);

        for (AffixInstance& stored : run->affixes)
        {
            if (!stored.impl)
                continue;

            Ctx ctx = MakeCtx(player, run, &stored);
            stored.impl->OnAttach(ctx);
        }

        // Once, after all of them, rather than per affix: the recompute is a
        // full stat pass and every carried affix contributes to the same
        // product. Without it a character logs in with a BonusMaxHealth boon
        // that does nothing until the next level or the next aura.
        RefreshStats(player);
    }

    void Mgr::Save(Player* player)
    {
        RunState* st = Get(player);
        if (!st || !player)
            return;

        // The key/value store has its own dirty tracking and issues no query at
        // all when nothing changed, so it is written on every save rather than
        // gated on gauntlet_run's flag: the two move independently, and a
        // Champions counter that advanced without the run's tier moving would
        // otherwise never reach the database.
        st->state.SaveTo(player->GetGUID().GetCounter());

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

        // The scheduler goes with the run it belongs to. Anything still queued
        // on it is dropped, which is correct: the events were for a player who
        // is no longer here.
        _live.erase(guid);

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
                        Ctx ctx = MakeCtx(player, st, out);
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

            AffixInstance& stored = st->Attach(instance);

            // Before OnAttach, for the reason Mgr::Load spells out: the budget
            // has to be right by the time the new affix arms its first event.
            SyncTimedAffixCount(player);

            if (stored.impl)
            {
                Ctx ctx = MakeCtx(player, st, &stored);
                stored.impl->OnAttach(ctx);
            }

            // REPLACE rather than INSERT so a client that manages to pick
            // twice for one tier overwrites its own row instead of failing on
            // the (guid, slot) key and losing the affix entirely.
            trans->Append(
                "REPLACE INTO `gauntlet_affix` "
                "(`guid`, `slot`, `mechanic`, `rank`, `cond`, `boon`, `boon_mag`, `gen_version`) "
                "VALUES ({}, {}, {}, {}, {}, {}, {}, {})",
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

        // The carried set just changed, so anything it contributes to the
        // health pool has to be recomputed now. A rank-up moves the number, a
        // swap can take a contribution away entirely, and a new affix adds one;
        // none of the three is a stat change the core would notice by itself.
        RefreshStats(player);

        // A pick is one of the four moments CONTRACT-P1 section 5.2 names for
        // writing the state store, and the only one where the carried set
        // changed underneath it: an affix that was swapped away has just had
        // its OnDetach, and whatever that wrote must not wait for a logout.
        st->state.SaveTo(low);

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

    void Mgr::RefreshStats(Player* player)
    {
        if (!player || !player->IsInWorld())
            return;

        // UpdateMaxHealth rebuilds the pool from the stat chain and then calls
        // OnPlayerAfterUpdateMaxHealth, which is where this module's own
        // contribution goes on. Nothing here compounds: the value it hands the
        // hook has none of ours in it.
        player->UpdateMaxHealth();
    }

    void Mgr::DetachAll(Player* player)
    {
        RunState* st = Get(player);
        if (!st)
            return;

        ForEachMechanic(player, st, [](Ctx& ctx, AffixInstance& a) { a.impl->OnDetach(ctx); });

        // Whatever the carried set was contributing to the health pool stops
        // here, and the pool has to be told -- after the detaches, so the
        // recompute sees a set that is no longer contributing. Deep Wounds
        // already did this for itself in OnDetach for the same reason; this
        // covers every affix whose contribution goes through the aggregate.
        RefreshStats(player);
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
        st->deathTimerMs = _deathWindowMs;
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

    AggregateCaps Mgr::EffectiveCaps(Player* player, AggregateKind kind) const
    {
        AggregateCaps caps = _caps;

        if (!_enabled || !IsEligible(player))
            return caps;

        auto it = _runs.find(player->GetGUID());
        if (it == _runs.end())
            return caps;

        AggregateInput in;
        in.kind = kind;
        FillConditions(player, in);

        for (AffixInstance const& a : it->second.affixes)
        {
            if (!a.impl || a.condition >= Condition::MAX)
                continue;
            if (!in.conditionActive[static_cast<std::size_t>(a.condition)])
                continue;

            a.impl->RelaxCaps(a, kind, caps);
        }

        return caps;
    }

    float Mgr::RawProduct(Player* player, AggregateKind kind, Unit* other, SpellInfo const* spellInfo)
    {
        if (!_enabled || !IsEligible(player))
            return 1.0f;

        auto it = _runs.find(player->GetGUID());
        if (it == _runs.end())
            return 1.0f;

        RunState& st = it->second;

        AggregateInput in;
        in.kind = kind;
        FillConditions(player, in);

        // The affix maths stay where they are tested. Asking for them with caps
        // that cannot bite is what gets the raw product back out of a function
        // whose whole job is to clamp: the three Mult callbacks below have to
        // be folded into the same product, before section 2.5's ceiling rather
        // than on top of it, and there is no other way to reach the middle of
        // that calculation without duplicating it.
        float product = Gauntlet::Aggregate(st.affixes, in, UncappedCaps());

        for (std::size_t i = 0; i < st.affixes.size(); ++i)
        {
            AffixInstance& a = st.affixes[i];
            if (!a.impl || a.condition >= Condition::MAX)
                continue;

            // The same condition gate the aggregate applies to
            // AggregateFactor. An affix that is not in force does not multiply
            // a blow either.
            if (!in.conditionActive[static_cast<std::size_t>(a.condition)])
                continue;

            Ctx ctx = MakeCtx(player, &st, &a);

            float factor = 1.0f;
            switch (kind)
            {
                case AggregateKind::DamageTaken: factor = a.impl->DamageTakenMult(ctx, other, spellInfo); break;
                case AggregateKind::DamageDone:  factor = a.impl->DamageDoneMult (ctx, other, spellInfo); break;
                case AggregateKind::HealTaken:   factor = a.impl->HealTakenMult  (ctx, other, spellInfo); break;
                default: break;
            }

            if (factor == 1.0f)
                continue;

            product *= factor;

            // Attribution, and the one place it can be read off cheaply: an
            // affix that just made this blow hurt more is an affix that acted.
            // KILLBY needs a name and this is where one is.
            if (kind == AggregateKind::DamageTaken && factor > 1.0f)
                NoteActor(player, a.mechanic);
        }

        return product;
    }

    float Mgr::AggregateAt(Player* player, AggregateKind kind, Unit* other, SpellInfo const* spellInfo)
    {
        return ClampToCaps(RawProduct(player, kind, other, spellInfo), kind,
                           EffectiveCaps(player, kind));
    }

    // =====================================================================
    // The hook fan-outs.
    // =====================================================================

    void Mgr::Tick(Player* player, uint32 diffMs)
    {
        RunState* st = Get(player);
        Live* live   = LiveFor(player);
        if (!st || !live)
            return;

        // Two plain countdowns, and they run whatever else is switched off:
        // the grace window is a promise to the player, and the actor's memory
        // going stale is what stops a death being blamed on an affix that
        // stopped acting a quarter of an hour ago.
        if (st->graceMs != 0)
            st->graceMs = st->graceMs > diffMs ? st->graceMs - diffMs : 0;

        if (st->lastActorMs != 0)
        {
            if (st->lastActorMs > diffMs)
            {
                st->lastActorMs -= diffMs;
            }
            else
            {
                st->lastActorMs = 0;
                st->lastActor   = MECHANIC_NONE;
            }
        }

        // CONTRACT-P1 section 5.2's "every 60 s while dirty". SaveTo issues no
        // query when nothing is dirty, so the test is an optimisation rather
        // than a correctness rule.
        live->stateSaveMs += diffMs;
        if (live->stateSaveMs >= STATE_SAVE_MS)
        {
            live->stateSaveMs = 0;
            if (st->state.Dirty())
                st->state.SaveTo(player->GetGUID().GetCounter());
        }

        if (st->dead)
            return;

        // The 500 ms boundary, and everything past this line runs on it rather
        // than on the world tick. The accumulator is here rather than in each
        // mechanic because this hook fires once per World::Update -- roughly
        // every millisecond -- and the interface documents OnTick as a 500 ms
        // hook; the accumulated figure is what is passed, so a mechanic
        // integrating over diffMs still measures real time.
        //
        // The scheduler is behind the same boundary on purpose. Scheduler::Tick
        // accumulates to the same 500 ms itself, so feeding it whole boundaries
        // is identical arithmetic -- but the Suppression below is a question
        // that is only asked twice a second, and answering it a thousand times
        // a second per player is work for nothing. GetAreaId() is the one part
        // of it that is not a field read: it recomputes the area from the
        // terrain whenever the player has moved since the last call
        // (Object.cpp:3174-3180).
        live->tickMs += diffMs;
        if (live->tickMs < Scheduler::TICK_MS)
            return;

        uint32 const elapsed = live->tickMs;
        live->tickMs = 0;

        // To *every* carried mechanic and not only the MF_Timed ones. MF_Timed
        // means "spends the event budget", which Deep Wounds must not, and its
        // decay, its batched write onto maximum health and its readout all hang
        // off this tick without ever arming an event.
        ForEachMechanic(player, st,
                        [elapsed](Ctx& ctx, AffixInstance& a) { a.impl->OnTick(ctx, elapsed); });

        // Gauntlet.Events.Enable = 0 means no event is armed and none is
        // delivered. The scheduler has no opinion about it; this is the gate.
        if (!_eventsEnabled)
            return;

        // Design section 4.2's suppression list, filled from the live player so
        // that Scheduler::Tick itself stays free of Player.h and testable with
        // a fake clock. Nothing is dropped by any of these -- the same event
        // comes out on the first tick after the flag clears.
        Suppression sup;
        sup.mounted      = player->IsMounted();                 // Unit.h:1887
        sup.inFlight     = player->IsInFlight();                // Unit.h:1709
        sup.dead         = !player->IsAlive();                  // Unit.h:1793
        sup.inGrace      = st->graceMs != 0;
        sup.offerPending = !st->pending.empty();

        // The core's own sanctuary test (PlayerUpdates.cpp:1242) is exactly
        // this pair. pvpInfo.IsInNoPvPArea would need no DBC lookup but is a
        // superset -- it is also true in a friendly capital city -- and design
        // section 4.2 says sanctuary, so the literal reading is what is used.
        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(player->GetAreaId()))
            sup.inSanctuary = area->IsSanctuary();              // DBCStructure.h:533

        // Tick returns a copy, so a mechanic that arms or cancels from inside
        // its own callback cannot move the list being walked.
        for (ScheduledEvent const& ev : live->clock.Tick(elapsed, sup))
        {
            // A Fire can be lethal -- Falling Sky's is -- and the death path
            // empties the queue underneath us. The batch was handed out before
            // that happened, so it is checked here rather than assumed.
            if (st->dead || !player->IsAlive())
                break;

            AffixInstance* inst = st->Find(ev.mechanic);
            if (!inst || !inst->impl)
                continue;   // a mechanic this build has no code for; a normal answer

            Ctx ctx = MakeCtx(player, st, inst);
            if (ev.kind == EventKind::Fire)
            {
                // The affix did something on its own clock, which is the
                // clearest possible reading of "the last mechanic to act".
                NoteActor(player, ev.mechanic);
                inst->impl->OnEvent(ctx, ev.id);
            }
            else
            {
                inst->impl->OnWarn(ctx, ev.id);
            }
        }
    }

    void Mgr::OnEnterCombat(Player* player, Unit* enemy)
    {
        if (!_enabled || !IsEligible(player))
            return;

        RunState* st = Get(player);
        if (!st || st->dead)
            return;

        // wasOutOfCombat is true and not a guess. CombatManager.cpp:423 is the
        // only place in the core that calls OnPlayerEnterCombat, and the
        // function it sits in returns early unless the combat state actually
        // changed (:412-413) -- so the hook *is* the out-of-combat edge and a
        // body-pull into a fight already under way never reaches it. Reading
        // IsInCombat() here instead is not available: UNIT_FLAG_IN_COMBAT is
        // set at :417, six lines earlier.
        ForEachMechanic(player, st,
                        [enemy](Ctx& ctx, AffixInstance& a) { a.impl->OnEnterCombat(ctx, enemy, true); });
    }

    void Mgr::OnLeaveCombat(Player* player)
    {
        if (!_enabled || !IsEligible(player))
            return;

        RunState* st = Get(player);
        if (!st)
            return;

        ForEachMechanic(player, st, [](Ctx& ctx, AffixInstance& a) { a.impl->OnLeaveCombat(ctx); });
    }

    void Mgr::OnCreatureKill(Player* player, Creature* killed, bool byPet)
    {
        if (!_enabled || !IsEligible(player) || !killed)
            return;

        RunState* st = Get(player);
        if (!st)
            return;

        ForEachMechanic(player, st, [killed, byPet](Ctx& ctx, AffixInstance& a)
        {
            if (byPet)
                a.impl->OnPetKill(ctx, killed);
            else
                a.impl->OnKill(ctx, killed);
        });
    }

    uint32 Mgr::OnLethal(Player* player, uint32 damage)
    {
        // The hottest guard in the module: this runs inside Unit::DealDamage
        // for every blow landed on every player on the realm, so the cheap
        // integer test comes before the map lookup and before IsEligible.
        if (damage == 0 || !_enabled)
            return damage;

        if (!player->IsAlive() || damage < player->GetHealth())
            return damage;

        if (!IsEligible(player))
            return damage;

        RunState* st = Get(player);
        if (!st)
            return damage;

        // Self-inflicted damage the module applied itself is not a death a
        // bargain gets to buy back. Blood Magic pays health to cast; a charge
        // spent on its own spell cost would be absurd, and it floors at 1
        // health anyway so it can never arrive here in the first place unless
        // something else has changed.
        if (st->selfDamage)
            return damage;

        // Every carried mechanic gets to reduce, in carried order, and the
        // damage only ever falls: a mechanic may not raise a blow it was
        // handed, which keeps this from becoming a second damage-multiplier
        // path outside the aggregate's clamp.
        ForEachMechanic(player, st, [&damage](Ctx& ctx, AffixInstance& a)
        {
            uint32 const out = a.impl->OnLethal(ctx, damage);
            if (out < damage)
                damage = out;
        });

        return damage;
    }

    void Mgr::OnSpellCast(Player* player, Spell* spell)
    {
        if (!_enabled || !spell || !IsEligible(player))
            return;

        RunState* st = Get(player);
        if (!st)
            return;

        ForEachMechanic(player, st, [spell](Ctx& ctx, AffixInstance& a)
        {
            a.impl->OnSpellCast(ctx, spell);
        });
    }

    // Joining or leaving a group is a stat change for exactly one mechanic --
    // Lone Wolf halves the pool while you are in one -- and Player::
    // UpdateMaxHealth is the only thing that fires OnPlayerAfterUpdateMaxHealth,
    // which the core calls on level and stamina changes and on nothing else.
    // Without this the penalty would appear at the player's next level-up and
    // the affix would look broken, which is the exact fault that cost most of
    // an evening in Phase 2.
    void Mgr::OnGroupChanged(Player* player)
    {
        if (!_enabled || !IsEligible(player))
            return;

        if (!Get(player))
            return;

        RefreshStats(player);
    }

    void Mgr::OnRepair(Player* player, float& discountMod)
    {
        if (!_enabled || !IsEligible(player))
            return;

        RunState* st = Get(player);
        if (!st)
            return;

        ForEachMechanic(player, st, [&discountMod](Ctx& ctx, AffixInstance& a)
        {
            a.impl->OnRepair(ctx, discountMod);
        });
    }

    bool Mgr::Allows(Player* player, Restricted what)
    {
        if (!_enabled || !IsEligible(player))
            return true;

        RunState* st = Get(player);
        if (!st)
            return true;

        bool allowed = true;
        ForEachMechanic(player, st, [&allowed, what](Ctx& ctx, AffixInstance& a)
        {
            if (allowed && !a.impl->Allows(ctx, what))
                allowed = false;
        });

        return allowed;
    }

    void Mgr::OnDamageTaken(Player* player, Unit* attacker, uint32 amount)
    {
        if (!_enabled || !IsEligible(player) || amount == 0)
            return;

        RunState* st = Get(player);
        if (!st)
            return;

        // Damage this module applied to its own player is not damage the world
        // did, and the observers must not treat it as such. Blood Magic's
        // health cost would otherwise become a Deep Wound -- a caster paying
        // for a spell and then having the payment taxed a second time by an
        // unrelated affix, which is a tax on a tax and precisely the pattern
        // design section 3's note on Feeble rejects.
        if (st->selfDamage)
            return;

        // A creature this module put into the world is the clearest attribution
        // there is: the Shade that killed you is named by name rather than by
        // whatever multiplied the last blow.
        if (Creature* creature = attacker ? attacker->ToCreature() : nullptr)
            NoteActor(player, sGauntletSummons->MechanicOf(creature));

        ForEachMechanic(player, st, [attacker, amount](Ctx& ctx, AffixInstance& a)
        {
            a.impl->OnDamageTaken(ctx, attacker, amount);
        });
    }

    void Mgr::OnMaxHealth(Player* player, float& value)
    {
        if (!_enabled || !IsEligible(player) || value <= 0.0f)
            return;

        RunState* st = Get(player);
        if (!st)
            return;

        // What arrives is the pool the stat chain built with nothing of this
        // module in it: Player::UpdateMaxHealth rebuilds `value` from scratch on
        // every call (StatSystem.cpp:313-324) and nothing below is ever written
        // back into that chain, which is what stops any of this compounding.
        float const base = value;

        // Raw, not clamped: the wound below has to come off before the floor is
        // applied, or the two clamps compound into 0.36 x base.
        value *= RawProduct(player, AggregateKind::MaxHealth, nullptr, nullptr);

        ForEachMechanic(player, st,
                        [&value](Ctx& ctx, AffixInstance& a) { a.impl->OnMaxHealth(ctx, value); });

        // Plan section 2.5's floor, applied once over the finished number and
        // never per mechanic. Deep Wounds caps its own wound at 40% of the pool
        // for the same reason -- the two are one rule seen from either end --
        // so this only bites when an aggregate and a wound stack.
        // EffectiveCaps and not _caps: Lone Wolf's grouped half is -50% against
        // a floor of 0.6, and a floor that ate it would deliver -40% behind a
        // blurb that says half.
        float const minimum = base * EffectiveCaps(player, AggregateKind::MaxHealth).maxHealthMin;
        if (value < minimum)
            value = minimum;

        // SetMaxHealth reads zero as one anyway (Unit.cpp:12410) and a pool of
        // zero divides by zero in every health percentage in the core.
        if (value < 1.0f)
            value = 1.0f;
    }

    void Mgr::OnGiveXP(Player* player, uint32& amount, Unit* victim)
    {
        if (!_enabled || !IsEligible(player) || amount == 0)
            return;

        // The mechanics first: Champions doubles a Champion's experience and the
        // Shade's Vindication pays 25% on everything for five minutes, and both
        // are rewards the affix promised.
        ForEachMechanic(player, Get(player), [&amount, victim](Ctx& ctx, AffixInstance& a)
        {
            a.impl->OnXP(ctx, amount, victim);
        });

        // Then the summon rate. A creature this module spawned on demand is not
        // a source of experience: without this, an affix that summons is an
        // affix that can be farmed. CONTRACT-P1 section 5.3.
        if (Creature* creature = victim ? victim->ToCreature() : nullptr)
            if (sGauntletSummons->IsGauntletSummon(creature))
                amount = static_cast<uint32>(static_cast<float>(amount) * _summonXpRate);

        // And last the aggregate, which is where an affix reporting an
        // Experience factor lands -- Hubris's boon among them.
        amount = static_cast<uint32>(static_cast<float>(amount) * Aggregate(player, AggregateKind::Experience));
    }

    void Mgr::OnLootMoney(Player* player, Loot* loot)
    {
        if (!_enabled || !IsEligible(player) || !loot)
            return;

        RunState* st = Get(player);
        if (!st)
            return;

        // OnPlayerBeforeLootMoney carries no loot guid, so a mechanic that
        // needs the source reads Loot::sourceWorldObjectGUID, which
        // Creature::AddToWorld stamps on (Creature.cpp:331).
        ForEachMechanic(player, st, [loot](Ctx& ctx, AffixInstance& a)
        {
            a.impl->OnLootMoney(ctx, loot);
        });
    }

    void Mgr::OnLootWindow(Player* player, ObjectGuid const& lootGuid, Loot* loot)
    {
        if (!_enabled || !IsEligible(player) || !loot)
            return;

        RunState* st = Get(player);
        if (!st || st->dead)
            return;

        ForEachMechanic(player, st, [&lootGuid, loot](Ctx& ctx, AffixInstance& a)
        {
            a.impl->OnLoot(ctx, lootGuid, loot);
        });
    }

    void Mgr::OnItemRoll(Player const* player, float& chance)
    {
        // The const_cast is confined to this line and is what the hook's
        // signature forces: GlobalScript::OnItemRoll is const because the core
        // will not let a script move a player, and everything below only reads
        // the guid to find a run and then hands the mechanics a chance, never
        // the player.
        Player* mutablePlayer = const_cast<Player*>(player);

        if (!_enabled || !IsEligible(mutablePlayer))
            return;

        RunState* st = Get(mutablePlayer);
        if (!st || st->dead)
            return;

        ForEachMechanic(mutablePlayer, st, [&chance](Ctx& ctx, AffixInstance& a)
        {
            a.impl->OnItemRoll(ctx, chance);
        });
    }

    void Mgr::OnCreatureDamaged(Player* player, Creature* victim, uint32 damage)
    {
        if (!_enabled || !IsEligible(player) || !victim || damage == 0)
            return;

        RunState* st = Get(player);
        if (!st || st->dead)
            return;

        ForEachMechanic(player, st, [victim, damage](Ctx& ctx, AffixInstance& a)
        {
            a.impl->OnCreatureDamaged(ctx, victim, damage);
        });
    }

    void Mgr::OpenGrace(Player* player)
    {
        if (RunState* st = Get(player))
            st->graceMs = _graceMs;
    }

    void Mgr::OnZoneChanged(Player* player)
    {
        OpenGrace(player);

        // CONTRACT-P1 section 2.4: a summon despawns on logout, on death, on a
        // zone change and on the leash. AllMapScript::OnPlayerLeaveAll covers
        // every move between maps; this is the one that covers a move inside
        // one, and it is why a Shade cannot follow its owner across a zone
        // line. That is the card's own counterplay -- leaving it behind -- read
        // at its strongest.
        sGauntletSummons->DespawnAll(player);
    }

    void Mgr::NoteActor(Player* player, uint16 mechanic)
    {
        if (mechanic == MECHANIC_NONE)
            return;

        if (RunState* st = Get(player))
            st->NoteActor(mechanic);
    }

    void Mgr::ReportKilledBy(Player* player)
    {
        RunState* st = Get(player);
        if (!st || st->lastActor == MECHANIC_NONE || !player->GetSession())
            return;

        MechanicDef const* def = FindMechanic(st->lastActor);
        std::string const name = def ? std::string(def->name)
                                     : ("#" + std::to_string(static_cast<uint32>(st->lastActor)));

        sGauntletAddon->SendKilledBy(player, st->lastActor, name);

        // Design section 4.8's fourth question -- does the player know which
        // affix acted when they die -- cannot be answered with "install the
        // addon", so the same fact goes out as a chat line.
        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cffff2020[Gauntlet]|r The last affix to act on you was |cffffff00{}|r.", name);
    }

    void Mgr::OnDied(Player* player)
    {
        RunState* st = Get(player);
        if (!st)
            return;

        ReportKilledBy(player);

        // Nothing keeps running past a death. The queue is emptied rather than
        // suppressed because the run is either over or about to be, and
        // Scheduler::CancelAll deliberately leaves the spacing clock alone so a
        // player raised inside the death window gets no free burst.
        if (Scheduler* sched = ClockFor(player))
            sched->CancelAll();

        // CONTRACT-P1 section 2.4's second despawn path.
        sGauntletSummons->DespawnAll(player);

        st->state.SaveTo(player->GetGUID().GetCounter());
    }

    bool Mgr::FireNow(Player* player, uint16 mechanic)
    {
        RunState*  st    = Get(player);
        Scheduler* sched = ClockFor(player);
        if (!st || !sched || st->dead)
            return false;

        AffixInstance* inst = st->Find(mechanic);
        if (!inst || !inst->impl)
            return false;

        // The queue is read rather than drained: Tick() is what releases an
        // event normally, and a cheat that bypassed it would leave the entry
        // behind to fire again on its own time. Cancel takes the pair out --
        // warning included -- and the mechanic is called directly, which is
        // exactly what the plan asks for ("skip the clock, keep the warning").
        uint32 eventId = 0;
        bool   found   = false;
        for (ScheduledEvent const& ev : sched->Queue())
            if (ev.mechanic == mechanic && ev.kind == EventKind::Fire)
            {
                eventId = ev.id;
                found   = true;
                break;
            }

        if (!found)
            return false;

        // "Skip the clock, keep the warning" (plan section 5.2). Cancel takes
        // the whole pair out, so a fire released before its telegraph had gone
        // out would arrive with none at all -- which is precisely the failure
        // the affixes are built to avoid, produced by the tool meant to test
        // them. So the warning is delivered first when it is still owed.
        bool const owesWarning = !sched->WarnIssued(mechanic, eventId);

        sched->Cancel(mechanic);

        Ctx ctx = MakeCtx(player, st, inst);

        if (owesWarning)
            inst->impl->OnWarn(ctx, eventId);

        NoteActor(player, mechanic);
        inst->impl->OnEvent(ctx, eventId);
        return true;
    }

    void Mgr::SetEventsEnabled(bool enabled)
    {
        _eventsEnabled = enabled;

        if (enabled)
            return;

        // Nothing queued may survive the switch being thrown: a player who
        // turns events off and then walks into one has not had them turned off.
        for (auto& [guid, live] : _live)
            live.clock.CancelAll();
    }

    void Mgr::SyncTimedAffixCount(Player* player)
    {
        RunState*  st    = Get(player);
        Scheduler* sched = ClockFor(player);
        if (!st || !sched)
            return;

        // Design section 4.2 counts *timed affixes carried*, not affixes: the
        // budget stretches the intervals of things that keep a clock, and an
        // affix with no implementation in this build keeps none.
        uint32 timed = 0;
        for (AffixInstance const& a : st->affixes)
        {
            if (!a.impl)
                continue;
            if (MechanicDef const* def = FindMechanic(a.mechanic))
                if (def->flags & MF_Timed)
                    ++timed;
        }

        sched->SetTimedAffixCount(timed);
    }
}
