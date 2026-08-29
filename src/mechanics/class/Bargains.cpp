/*
 * mod-gauntlet - the two class bargains: Ankh Pact, Stone of the Damned
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletMgr.h"
#include "GauntletRegistry.h"
#include "GauntletSummons.h"
#include "../Charges.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Player.h"
#include "Position.h"

#include <string>

// Design section 3, family C, C43 and C44. These are the two that spend the
// pending-death seam Phase 0 opened with a comment naming a later phase, and
// they are the only affixes in the module that make a death survivable *after*
// it has happened -- Last Rites stops one arriving, these two undo one.
//
// The shape both share: the core will not raise a hardcore character, because
// GauntletPlayerScript::OnPlayerCanResurrect refuses while a death is pending.
// A bargain that intends to pay says so through WillBuyDeath, which changes
// nothing; the veto then lets the resurrection through, and OnResurrect is
// where the price is actually taken. Asking and paying are separate for a
// reason -- the veto runs before the core has committed to anything, and a
// charge spent there would be spent on a resurrection that never happened.

namespace Gauntlet
{
    namespace
    {
        char const* KeyOf(uint16 id, char const* fallback)
        {
            MechanicDef const* def = FindMechanic(id);
            return def ? def->key : fallback;
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        // Both bargains are once per run rather than once per level, so the
        // ladder Charges takes is a level count large enough that no run
        // reaches it twice.
        constexpr uint8 ONCE_PER_RUN = 255;

        // ==================================================================
        // C43 - Ankh Pact (70), shaman
        //
        // "Reincarnation works once in this run. When it does, every boon you
        // carry is burned away."
        //
        // The card's note is the design of it: "knowing there is a second life
        // is the whole trap; the price is paid in the run's power for the
        // remaining tiers. Use it to save a level-70 character, not a
        // level-30 one."
        // ==================================================================
        constexpr uint16 MECHANIC_ANKH_PACT = 70;

        class AnkhPact final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Publish(ctx); }

            bool WillBuyDeath(Ctx& ctx) const override
            {
                return ctx.state && ctx.player
                    && Charges::Available(ctx.state, KeyOf(MECHANIC_ANKH_PACT, "c43_ankh_pact"),
                                          ctx.player->GetLevel(), ONCE_PER_RUN);
            }

            void OnResurrect(Ctx& ctx) override;

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                bool const up = ctx.state && ctx.player
                             && Charges::Available(ctx.state, KeyOf(MECHANIC_ANKH_PACT, "c43_ankh_pact"),
                                                   ctx.player->GetLevel(), ONCE_PER_RUN);
                return std::string("ankh pact: ") + (up ? "unspent" : "spent");
            }

        private:
            void Publish(Ctx& ctx)
            {
                if (!ctx.player || !ctx.state)
                    return;

                bool const up = Charges::Available(ctx.state, KeyOf(MECHANIC_ANKH_PACT, "c43_ankh_pact"),
                                                   ctx.player->GetLevel(), ONCE_PER_RUN);
                AddonFor(ctx)->QueueCounter(ctx.player, "second_life", up ? 1u : 0u, 1u);
            }
        };

        void AnkhPact::OnResurrect(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !ctx.run || !ctx.state)
                return;
            if (!ctx.run->pendingDeath)
                return;
            if (!Charges::Available(ctx.state, KeyOf(MECHANIC_ANKH_PACT, "c43_ankh_pact"),
                                    player->GetLevel(), ONCE_PER_RUN))
                return;

            Charges::Spend(ctx.state, KeyOf(MECHANIC_ANKH_PACT, "c43_ankh_pact"), player->GetLevel());

            // The price. Every boon on every carried affix goes to zero, and it
            // is the run's own state so it persists: the remaining tiers are
            // played with the curses and none of what paid for them.
            uint32 burned = 0;
            for (AffixInstance& a : ctx.run->affixes)
                if (a.boonMag != 0)
                {
                    a.boonMag = 0;
                    ++burned;
                }

            ctx.run->dirty = true;

            // The run survives, which is the whole point and the reason this
            // hook exists at all.
            sGauntlet->CancelPendingDeath(player);
            Publish(ctx);

            if (player->GetSession())
            {
                ChatHandler handler(player->GetSession());
                handler.PSendSysMessage(
                    "|cff20ff20[Gauntlet]|r The Ankh answers. The run continues.");
                handler.PSendSysMessage(
                    "|cffff2020[Gauntlet]|r {} of your boons burned away to pay for it. The"
                    " curses remain.", burned);
            }
        }

        std::string AnkhPact::Describe(AffixInstance const& /*self*/) const
        {
            return "Reincarnation will bring you back once in this run instead of ending it."
                   " When it does, every boon you carry is burned away and only the curses"
                   " remain. Spend it late.";
        }

        // ==================================================================
        // C44 - Stone of the Damned (71), warlock
        //
        // "A Soulstone will bring you back once. Whoever kills you will be
        // waiting."
        //
        // The card: "the second life is a rematch, not an escape. Prepare the
        // Soulstone before dangerous content, and be ready to win the fight
        // you just lost."
        // ==================================================================
        constexpr uint16 MECHANIC_STONE = 71;

        constexpr uint32 REMATCH_LIFETIME_MS = 300000;   // TODO(design)

        class StoneOfTheDamned final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Publish(ctx); }

            void OnDetach(Ctx& ctx) override
            {
                if (ctx.player)
                    sGauntletSummons->DespawnFor(ctx.player, MECHANIC_STONE);
            }

            bool WillBuyDeath(Ctx& ctx) const override
            {
                return ctx.state && ctx.player
                    && Charges::Available(ctx.state, KeyOf(MECHANIC_STONE, "c44_stone_of_the_damned"),
                                          ctx.player->GetLevel(), ONCE_PER_RUN);
            }

            void OnResurrect(Ctx& ctx) override;

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "A Soulstone will bring you back once in this run instead of ending it."
                       " Whatever killed you is standing there when you rise, at full health."
                       " Prepare the stone before anything dangerous.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                bool const up = ctx.state && ctx.player
                             && Charges::Available(ctx.state, KeyOf(MECHANIC_STONE, "c44_stone_of_the_damned"),
                                                   ctx.player->GetLevel(), ONCE_PER_RUN);
                std::string out = std::string("stone of the damned: ") + (up ? "unspent" : "spent");
                if (ctx.run)
                    out += ", killer entry " + std::to_string(ctx.run->lastKillerEntry);
                return out;
            }

        private:
            void Publish(Ctx& ctx)
            {
                if (!ctx.player || !ctx.state)
                    return;

                bool const up = Charges::Available(ctx.state, KeyOf(MECHANIC_STONE, "c44_stone_of_the_damned"),
                                                   ctx.player->GetLevel(), ONCE_PER_RUN);
                AddonFor(ctx)->QueueCounter(ctx.player, "second_life", up ? 1u : 0u, 1u);
            }
        };

        void StoneOfTheDamned::OnResurrect(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !ctx.run || !ctx.state)
                return;
            if (!ctx.run->pendingDeath)
                return;

            char const* key = KeyOf(MECHANIC_STONE, "c44_stone_of_the_damned");
            if (!Charges::Available(ctx.state, key, player->GetLevel(), ONCE_PER_RUN))
                return;

            Charges::Spend(ctx.state, key, player->GetLevel());
            sGauntlet->CancelPendingDeath(player);
            Publish(ctx);

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cff20ff20[Gauntlet]|r The stone gives you back. Something is waiting.");

            // The price: the rematch. Recorded on every blow rather than on the
            // killing one, because by the time the player is dead the attacker
            // may already have wandered off or despawned.
            uint32 const entry = ctx.run->lastKillerEntry;
            if (entry == 0)
                return;   // nothing remembered; the second life is simply free this once

            Position const at = player->GetFirstCollisionPosition(8.0f, 0.0f);

            Creature* rematch = sGauntletSummons->Summon(player, entry, at, REMATCH_LIFETIME_MS,
                                                          /*countsAsStalker*/ false, MECHANIC_STONE);
            if (!rematch)
                return;

            if (CreatureAI* ai = rematch->AI())
                ai->AttackStart(player);

            rematch->AddThreat(player, 1.0f);

            AddonFor(ctx)->SendEvent(player, key, 0, "Rematch");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r It followed you back. Win it this time.");
        }
    }

    GAUNTLET_MECHANIC(70, AnkhPact);
    GAUNTLET_MECHANIC(71, StoneOfTheDamned);
}
