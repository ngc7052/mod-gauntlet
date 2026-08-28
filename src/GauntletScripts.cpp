/*
 * mod-gauntlet - script hooks and commands
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"

using namespace Acore::ChatCommands;
using namespace Gauntlet;

static char const* GAUNTLET_RETIRED_MSG =
    "Your Gauntlet run has ended. This character is retired.";

class GauntletWorldScript : public WorldScript
{
public:
    GauntletWorldScript() : WorldScript("GauntletWorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sGauntlet->LoadConfig();
    }
};

class GauntletPlayerScript : public PlayerScript
{
public:
    GauntletPlayerScript() : PlayerScript("GauntletPlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->IsEligible(player))
            return;

        sGauntlet->Load(player);
        RunState* st = sGauntlet->Get(player);
        if (!st)
            return;

        ChatHandler ch(player->GetSession());
        if (st->dead)
        {
            ch.PSendSysMessage("|cffff2020[Gauntlet]|r {}", GAUNTLET_RETIRED_MSG);
            return;
        }

        ch.PSendSysMessage("|cffff2020[Gauntlet]|r Run seed |cffffff00{}|r - tier {} - {} affix(es) borne.",
                           st->seed, st->tier, uint32(st->affixes.size()));

        // A tier may have been reached while offline, or left unpicked.
        uint32 const due = player->GetLevel() / sGauntlet->Interval();
        if (due > st->tier)
            sGauntlet->OfferTier(player, st->tier + 1);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!sGauntlet->IsEligible(player))
            return;
        sGauntlet->Save(player);
        sGauntlet->Forget(player->GetGUID());
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->IsEligible(player))
            return;

        RunState* st = sGauntlet->Get(player);
        if (!st || st->dead)
            return;

        uint32 const due = player->GetLevel() / sGauntlet->Interval();
        if (due > st->tier)
            sGauntlet->OfferTier(player, st->tier + 1);
    }

    void OnPlayerJustDied(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->Hardcore() || !sGauntlet->IsEligible(player))
            return;

        sGauntlet->EndRun(player, "slain");
    }

    // AzerothCore exposes proper veto hooks, so the run simply cannot be
    // revived - no kicking, no re-killing, no disconnection.
    bool OnPlayerCanResurrect(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->Hardcore() || !sGauntlet->IsEligible(player))
            return true;

        RunState* st = sGauntlet->Get(player);
        if (st && st->dead)
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff2020[Gauntlet]|r {}", GAUNTLET_RETIRED_MSG);
            return false;
        }
        return true;
    }

    bool OnPlayerCanRepopAtGraveyard(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->Hardcore() || !sGauntlet->IsEligible(player))
            return true;

        RunState* st = sGauntlet->Get(player);
        return !(st && st->dead);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*source*/) override
    {
        amount = uint32(amount * sGauntlet->Multiplier(player, Effect::ExperienceGain));
    }
};

class GauntletUnitScript : public UnitScript
{
public:
    GauntletUnitScript() : UnitScript("GauntletUnitScript", true) { }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        if (Player* victim = target ? target->ToPlayer() : nullptr)
            damage = uint32(damage * sGauntlet->Multiplier(victim, Effect::DamageTaken));
        if (Player* dealer = attacker ? attacker->ToPlayer() : nullptr)
            damage = uint32(damage * sGauntlet->Multiplier(dealer, Effect::DamageDone));
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (Player* victim = target ? target->ToPlayer() : nullptr)
            damage = int32(damage * sGauntlet->Multiplier(victim, Effect::DamageTaken));
        if (Player* dealer = attacker ? attacker->ToPlayer() : nullptr)
            damage = int32(damage * sGauntlet->Multiplier(dealer, Effect::DamageDone));
    }

    void ModifyHealReceived(Unit* target, Unit* /*healer*/, uint32& heal, SpellInfo const* /*spellInfo*/) override
    {
        if (Player* p = target ? target->ToPlayer() : nullptr)
            heal = uint32(heal * sGauntlet->Multiplier(p, Effect::HealingReceived));
    }
};

class GauntletCommandScript : public CommandScript
{
public:
    GauntletCommandScript() : CommandScript("GauntletCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable sub =
        {
            { "pick",   HandlePick,   SEC_PLAYER, Console::No },
            { "status", HandleStatus, SEC_PLAYER, Console::No },
            { "top",    HandleTop,    SEC_PLAYER, Console::No },
        };
        static ChatCommandTable root =
        {
            { "gauntlet", sub },
        };
        return root;
    }

    static bool HandlePick(ChatHandler* handler, uint32 index)
    {
        Player* p = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!p)
            return false;

        if (!sGauntlet->Pick(p, index))
        {
            handler->PSendSysMessage("|cffff2020[Gauntlet]|r Nothing to pick, or invalid choice.");
            return true;
        }
        return true;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        Player* p = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        RunState* st = p ? sGauntlet->Get(p) : nullptr;
        if (!st)
            return false;

        handler->PSendSysMessage("|cffff2020[Gauntlet]|r seed {} | tier {} | {}",
                                 st->seed, st->tier, st->dead ? "RETIRED" : "alive");
        for (uint32 i = 0; i < st->affixes.size(); ++i)
            handler->PSendSysMessage("  {}. {} - {}", i + 1,
                                     st->affixes[i].Name(), st->affixes[i].Describe());
        return true;
    }

    static bool HandleTop(ChatHandler* handler)
    {
        QueryResult r = CharacterDatabase.Query(
            "SELECT name, tier, level, cause FROM gauntlet_leaderboard "
            "ORDER BY tier DESC, level DESC LIMIT 10");

        handler->PSendSysMessage("|cffff2020[Gauntlet]|r Furthest runs:");
        if (!r)
        {
            handler->PSendSysMessage("  No completed runs yet.");
            return true;
        }

        uint32 rank = 1;
        do
        {
            Field* f = r->Fetch();
            handler->PSendSysMessage("  {}. {} - tier {} at level {} ({})", rank++,
                                     f[0].Get<std::string>(), f[1].Get<uint32>(),
                                     f[2].Get<uint32>(), f[3].Get<std::string>());
        } while (r->NextRow());
        return true;
    }
};

void Addmod_gauntletScripts()
{
    new GauntletWorldScript();
    new GauntletPlayerScript();
    new GauntletUnitScript();
    new GauntletCommandScript();
}
