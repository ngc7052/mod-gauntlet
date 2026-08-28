/*
 * mod-gauntlet - script hooks
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAddon.h"
#include "GauntletMgr.h"
#include "Chat.h"
#include "Player.h"
#include "ScriptMgr.h"

using namespace Gauntlet;

// The .gauntlet tree moved to GauntletCommands.cpp with step 8, so plan
// section 2.6's "hook adapters only" is what is left here. The command script
// still has to be constructed from Addmod_gauntletScripts below -- the core's
// generated module loader calls that one name and nothing else -- so the seam
// is the free function AzerothCore uses for exactly this, declared here and
// defined beside the commands it registers.
void AddSC_gauntlet_commands();

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

    // Where the one-shot migration hangs. Three hooks can see a live database
    // and only one of them is right:
    //
    //   OnAfterConfigLoad is called from World::LoadConfigSettings
    //   (World.cpp:301), which also runs on `.reload config` -- a conversion
    //   that may happen twice is not a one-shot conversion.
    //
    //   OnStartup is called once, from Main.cpp:390, but that is *after*
    //   StartWorldNetwork (Main.cpp:355) has opened the listening socket, so
    //   there is a window, however small, in which a session could exist while
    //   the affix table is mid-conversion.
    //
    //   OnBeforeWorldInitialized is called once, from World.cpp:1022, inside
    //   SetInitialWorldSettings (Main.cpp:310) -- after StartDB (Main.cpp:280)
    //   has opened the databases and run the SQL updater that adds the
    //   redesign's columns, and before the network is listening. Nobody can be
    //   logged in, and it cannot fire twice.
    void OnBeforeWorldInitialized() override
    {
        sGauntlet->MigrateLegacyRuns();
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

        // HELLO and the snapshot go out before the chat lines and before the
        // retired check: an addon that is listening wants the tombstone drawn
        // too, and the run line below is the fallback for one that is not.
        sGauntletAddon->OnLogin(player);

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
        {
            sGauntlet->OfferTier(player, st->tier + 1);
            sGauntletAddon->SendOffers(player);
        }
    }

    void OnPlayerLogout(Player* player) override
    {
        // Unconditional and first: the addon's per-player state is keyed by
        // guid and must not outlive the session that owns it, and eligibility
        // is a config-dependent answer that may have changed since login.
        sGauntletAddon->Forget(player->GetGUID());

        if (!sGauntlet->IsEligible(player))
            return;

        // Logging out inside the death window used to be impossible, because
        // the run ended in OnPlayerJustDied. Now that it is a timer, quitting
        // while it runs would leave a dead character with a live run, so the
        // timer is settled here rather than abandoned.
        if (sGauntlet->IsPendingDeath(player))
            sGauntlet->EndRun(player, "slain");

        sGauntlet->Save(player);

        // OnDetach is documented as firing on swap, logout and death. Logout
        // is here, the swap is in Mgr::Pick, and death deliberately is not:
        // the character stays in the world with its affixes listed by
        // `.gauntlet status`, and tearing them down at the moment of death
        // would erase the run's own record of itself while the player is still
        // looking at it. The logout that follows always settles it.
        sGauntlet->DetachAll(player);
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
        {
            sGauntlet->OfferTier(player, st->tier + 1);
            sGauntletAddon->SendOffers(player);
        }
    }

    // Death no longer ends the run here (plan section 6, decision 5): it arms
    // a timer, so that a Phase 3 bargain -- Ankh Pact, Stone of the Damned --
    // has a window in which to cancel it. Releasing the corpse or letting the
    // timer expire is what ends the run, which is Blizzard's own rule.
    void OnPlayerJustDied(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->Hardcore() || !sGauntlet->IsEligible(player))
            return;

        sGauntlet->BeginPendingDeath(player);
    }

    void OnPlayerReleasedGhost(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->Hardcore() || !sGauntlet->IsEligible(player))
            return;

        if (sGauntlet->IsPendingDeath(player))
        {
            sGauntlet->EndRun(player, "slain");

            // Beyond the plan's table, which lists RUN at login and after a
            // pick. Without it the addon goes on drawing a retired run as
            // alive until the next login, which is the one moment the panel
            // has to be right about.
            sGauntletAddon->SendRun(player);
        }
    }

    // The seam the cancel path will use. Player::ResurrectPlayer is the one
    // entry point (Player.cpp:4546) and it consults OnPlayerCanResurrect at
    // the top (Player.cpp:4548) before firing this at the bottom
    // (Player.cpp:4597), so in Phase 0 this cannot be reached: the veto below
    // has already refused.
    //
    // It deliberately does not call CancelPendingDeath. Decision 5 cancels a
    // pending death *with a bargain charge*, and Phase 0 has no charge to
    // spend -- Last Rites and Ankh Pact are Phase 3. Cancelling unconditionally
    // would mean that any future path reaching here, or any relaxation of the
    // veto, silently stops the module being hardcore. Phase 3 spends the
    // charge here and calls Mgr::CancelPendingDeath.
    void OnPlayerResurrect(Player* /*player*/, float /*restorePercent*/, bool& /*applySickness*/) override
    {
    }

    // The 60-second timer, with no scheduler to hang it on: Phase 1 brings
    // one, and until then this is the module's only clock. It runs for every
    // player on every tick, so the first thing it does is an integer test that
    // is false whenever nobody in the world is dying.
    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (sGauntlet->AnyPendingDeath() && sGauntlet->Enabled() && sGauntlet->IsEligible(player))
            sGauntlet->UpdateDeathTimer(player, diff);

        // The addon channel's 500 ms flush has no clock of its own either.
        // This hook is the world tick, not a fixed cadence -- it fires once
        // per World::Update, which MinWorldUpdateTime leaves at roughly 1 ms
        // and load stretches -- so Addon::Update accumulates `diff` and
        // returns on an integer test whenever nothing is queued, which in
        // Phase 0 is always.
        sGauntletAddon->Update(player, diff);
    }

    // AzerothCore exposes proper veto hooks, so the run simply cannot be
    // revived - no kicking, no re-killing, no disconnection.
    //
    // The veto now covers the death window as well as the finished run. It has
    // to: before decision 5 the run was already over by the time anything
    // could offer a resurrection, and leaving the window open would let a
    // hardcore character be raised out of a death that has not resolved yet.
    // Phase 3 is what makes the window mean something, by spending a charge
    // here instead of refusing.
    bool OnPlayerCanResurrect(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->Hardcore() || !sGauntlet->IsEligible(player))
            return true;

        RunState* st = sGauntlet->Get(player);
        if (st && (st->dead || st->pendingDeath))
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff2020[Gauntlet]|r {}", GAUNTLET_RETIRED_MSG);
            return false;
        }
        return true;
    }

    // Releasing, by contrast, stays allowed while the timer runs: it is the
    // action that ends the run, and vetoing it would strand the corpse.
    bool OnPlayerCanRepopAtGraveyard(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->Hardcore() || !sGauntlet->IsEligible(player))
            return true;

        RunState* st = sGauntlet->Get(player);
        return !(st && st->dead);
    }

    // The client half of the GNT channel. The addon whispers itself with
    // SendAddonMessage("GNT", ...), which reaches Player::Whisper
    // ($CORE/src/server/game/Entities/Player/Player.cpp:9680) and this hook
    // before the packet is built, so returning false both acts on the message
    // and stops it ever being sent back out.
    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg,
                            Player* /*receiver*/) override
    {
        if (lang != uint32(LANG_ADDON))
            return true;

        // False only for a frame the module recognised; every other addon's
        // traffic passes through untouched.
        return !sGauntletAddon->HandleIncoming(player, msg);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*source*/) override
    {
        amount = uint32(amount * sGauntlet->Aggregate(player, AggregateKind::Experience));
    }
};

class GauntletUnitScript : public UnitScript
{
public:
    GauntletUnitScript() : UnitScript("GauntletUnitScript", true) { }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        if (Player* victim = target ? target->ToPlayer() : nullptr)
            damage = uint32(damage * sGauntlet->Aggregate(victim, AggregateKind::DamageTaken));
        if (Player* dealer = attacker ? attacker->ToPlayer() : nullptr)
            damage = uint32(damage * sGauntlet->Aggregate(dealer, AggregateKind::DamageDone));
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (Player* victim = target ? target->ToPlayer() : nullptr)
            damage = int32(damage * sGauntlet->Aggregate(victim, AggregateKind::DamageTaken));
        if (Player* dealer = attacker ? attacker->ToPlayer() : nullptr)
            damage = int32(damage * sGauntlet->Aggregate(dealer, AggregateKind::DamageDone));
    }

    void ModifyHealReceived(Unit* target, Unit* /*healer*/, uint32& heal, SpellInfo const* /*spellInfo*/) override
    {
        if (Player* p = target ? target->ToPlayer() : nullptr)
            heal = uint32(heal * sGauntlet->Aggregate(p, AggregateKind::HealTaken));
    }
};

void Addmod_gauntletScripts()
{
    new GauntletWorldScript();
    new GauntletPlayerScript();
    new GauntletUnitScript();
    AddSC_gauntlet_commands();
}
