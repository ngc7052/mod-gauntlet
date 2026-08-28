/*
 * mod-gauntlet - run state, persistence and effect aggregation
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Map.h"
#include "World.h"
#include "GameTime.h"

namespace Gauntlet
{
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

        if (_interval == 0)
            _interval = 5;
        if (_choices == 0)
            _choices = 1;
    }

    RunState* Mgr::Get(Player* player)
    {
        if (!player)
            return nullptr;
        auto it = _runs.find(player->GetGUID());
        return it == _runs.end() ? nullptr : &it->second;
    }

    void Mgr::Load(Player* player)
    {
        if (!player)
            return;

        RunState st;
        uint32 const low = player->GetGUID().GetCounter();

        if (QueryResult r = CharacterDatabase.Query(
                "SELECT seed, tier, dead FROM gauntlet_run WHERE guid = {}", low))
        {
            Field* f  = r->Fetch();
            st.seed   = f[0].Get<uint32>();
            st.tier   = f[1].Get<uint32>();
            st.dead   = f[2].Get<uint8>() != 0;
        }
        else
        {
            // New run: derive a seed that is stable for this character.
            st.seed = static_cast<uint32>(low * 2654435761u) ^ static_cast<uint32>(GameTime::GetGameTime().count());
            CharacterDatabase.Execute(
                "INSERT INTO gauntlet_run (guid, seed, tier, dead) VALUES ({}, {}, 0, 0)", low, st.seed);
        }

        if (QueryResult r = CharacterDatabase.Query(
                "SELECT tier, roll FROM gauntlet_affix WHERE guid = {} ORDER BY tier ASC", low))
        {
            do
            {
                Field* f = r->Fetch();
                st.affixes.push_back(Roll(st.seed, f[0].Get<uint32>(), f[1].Get<uint32>()));
            } while (r->NextRow());
        }

        _runs[player->GetGUID()] = std::move(st);
    }

    void Mgr::Save(Player* player)
    {
        RunState* st = Get(player);
        if (!st || !player)
            return;

        CharacterDatabase.Execute("UPDATE gauntlet_run SET tier = {}, dead = {} WHERE guid = {}",
                                  st->tier, st->dead ? 1 : 0, player->GetGUID().GetCounter());
    }

    void Mgr::Forget(ObjectGuid guid)
    {
        _runs.erase(guid);
    }

    void Mgr::OfferTier(Player* player, uint32 tier)
    {
        RunState* st = Get(player);
        if (!st || st->dead)
            return;

        st->pending.clear();
        for (uint32 i = 0; i < _choices; ++i)
            st->pending.push_back(Roll(st->seed, tier, i));

        ChatHandler ch(player->GetSession());
        ch.PSendSysMessage("|cffff2020[Gauntlet]|r Tier {} reached. Choose your affix:", tier);
        for (uint32 i = 0; i < st->pending.size(); ++i)
            ch.PSendSysMessage("  |cffffff00{}.|r {} - {}", i + 1,
                               st->pending[i].Name(), st->pending[i].Describe());
        ch.PSendSysMessage("Use |cff00ff00.gauntlet pick <number>|r to commit. It cannot be undone.");
    }

    bool Mgr::Pick(Player* player, uint32 index)
    {
        RunState* st = Get(player);
        if (!st || st->pending.empty() || index == 0 || index > st->pending.size())
            return false;

        Affix chosen = st->pending[index - 1];
        st->tier += 1;
        st->affixes.push_back(chosen);
        st->pending.clear();

        CharacterDatabase.Execute(
            "INSERT INTO gauntlet_affix (guid, tier, roll) VALUES ({}, {}, {})",
            player->GetGUID().GetCounter(), st->tier, index - 1);
        Save(player);

        ChatHandler(player->GetSession()).PSendSysMessage(
            "|cffff2020[Gauntlet]|r You bear |cffffff00{}|r. {}", chosen.Name(), chosen.Describe());

        if (_announce)
            sWorld->SendServerMessage(SERVER_MSG_STRING,
                Acore::StringFormat("[Gauntlet] {} reached tier {} and took {}.",
                                    player->GetName(), st->tier, chosen.Name()));
        return true;
    }

    void Mgr::EndRun(Player* player, std::string const& cause)
    {
        RunState* st = Get(player);
        if (!st || st->dead)
            return;

        st->dead = true;
        Save(player);

        uint32 const low = player->GetGUID().GetCounter();
        CharacterDatabase.Execute(
            "REPLACE INTO gauntlet_leaderboard (guid, name, tier, level, cause, ended) "
            "VALUES ({}, '{}', {}, {}, '{}', NOW())",
            low, player->GetName(), st->tier, player->GetLevel(), cause);

        if (_announce)
            sWorld->SendServerMessage(SERVER_MSG_STRING,
                Acore::StringFormat("[Gauntlet] {} has fallen at level {} on tier {} ({}).",
                                    player->GetName(), player->GetLevel(), st->tier, cause));
    }

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

    float Mgr::Multiplier(Player* player, Effect effect) const
    {
        if (!_enabled || !player)
            return 1.0f;

        auto it = _runs.find(player->GetGUID());
        if (it == _runs.end())
            return 1.0f;

        float pct = 0.0f;
        for (Affix const& a : it->second.affixes)
        {
            if (a.effect == effect && ConditionActive(player, a.condition))
                pct += static_cast<float>(a.magnitude);

            // A boon can offset the same effect it is paired against.
            if (a.boon != Boon::None && ConditionActive(player, a.condition))
            {
                bool const offsets =
                    (a.boon == Boon::BonusDamage     && effect == Effect::DamageDone)      ||
                    (a.boon == Boon::BonusHealing    && effect == Effect::HealingDone)     ||
                    (a.boon == Boon::BonusMoveSpeed  && effect == Effect::MoveSpeed)       ||
                    (a.boon == Boon::BonusExperience && effect == Effect::ExperienceGain)  ||
                    (a.boon == Boon::BonusMoney      && effect == Effect::MoneyGain)       ||
                    (a.boon == Boon::BonusMaxHealth  && effect == Effect::MaxHealth)       ||
                    (a.boon == Boon::BonusRegen      && effect == Effect::HealthRegen);
                if (offsets)
                    pct -= static_cast<float>(a.boonMagnitude);
            }
        }

        // Penalty effects reduce; DamageTaken/Threat/Durability increase.
        bool const inverted = (effect == Effect::DamageTaken)
                           || (effect == Effect::ThreatGeneration)
                           || (effect == Effect::DurabilityLoss);

        float const m = inverted ? (1.0f + pct / 100.0f) : (1.0f - pct / 100.0f);
        return std::max(0.05f, m);
    }
}
