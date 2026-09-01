/*
 * mod-gauntlet - 114 Gravedigger: the eighth corpse gets up again
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "GauntletState.h"
#include "GauntletSummons.h"
#include "../Boons.h"

#include "Chat.h"
#include "Creature.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"

#include <algorithm>
#include <string>

// Registry id 114, docs/commons.md section 4b, and the Spawn family's first
// reward-shaped card that is not a rare.
//
// Every eighth corpse you open gets up again, and putting it down a second
// time pays you what it would not give the first time -- its own loot table,
// rolled again. The curse is a fight you did not choose, arriving at the worst
// moment there is: the one where you have stopped to loot.
//
// It is deliberately smaller than Carrion (3), the rare that stands beside it:
// that card draws two scavengers every fourth corpse and pays nothing for
// them, and this one draws a single riser every eighth and pays for it. A
// common should be the quieter card.
//
// The seam that decides whether this card can exist at all is "make a creature
// the module summoned drop a table the module chose", and it is the core's own
// death path rather than an invention. Unit::Kill fills a dying creature's
// loot, then sets UNIT_DYNFLAG_LOOTABLE only if that loot is not already
// looted (Unit.cpp:14210-14220). ENTRY_RISEN's template carries lootid 0, so
// the core fills nothing and the corpse is not lootable -- unless the same
// four calls are made here, in the same order, which is what OnKill does.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_GRAVEDIGGER = 114;

        // Persistent, for Carrion's reason: a counter that resets at the login
        // screen cannot be planned around, and "every eighth" is only a rhythm
        // if it survives a logout.
        constexpr char const* KEY_LOOTS = "gravedigger.loots";

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_GRAVEDIGGER);
            return def ? def->key : "gravedigger";
        }

        class Gravedigger final : public IMechanic
        {
        public:
            void OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot) override;

            void OnKill(Ctx& ctx, Creature* killed) override { Pay(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Pay(ctx, killed); }

            void OnDetach(Ctx& ctx) override
            {
                _owed = ObjectGuid::Empty;
                _owedLoot = 0;
                if (ctx.player)
                    sGauntletSummons->DespawnFor(ctx.player, MECHANIC_GRAVEDIGGER);
            }

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "Every " + std::to_string(Rules::GRAVEDIGGER_EVERY)
                     + "th corpse you loot gets up again. Put it down and it drops what it was "
                       "holding back.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                int32 const loots = ctx.state ? ctx.state->Get(KEY_LOOTS, 0) : 0;
                std::string out = "gravedigger: " + std::to_string(loots) + " corpse(s) looted, "
                                + std::to_string(_raised) + " raised, " + std::to_string(_paid)
                                + " paid out";
                out += "; " + std::to_string(Rules::GRAVEDIGGER_EVERY - (loots % Rules::GRAVEDIGGER_EVERY))
                     + " more until the next one gets up";
                if (_owedLoot != 0)
                    out += "; one is standing and owes a table";
                return out;
            }

        private:
            void Pay(Ctx& ctx, Creature* killed);

            ObjectGuid _owed;
            uint32     _owedLoot = 0;
            uint32     _raised   = 0;
            uint32     _paid     = 0;
        };

        void Gravedigger::OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* /*loot*/)
        {
            Player* player = ctx.player;
            if (!player || !player->IsInWorld() || !player->IsAlive())
                return;
            if (ctx.run && (ctx.run->dead || OfferHoldsBack(*ctx.run)))
                return;

            int32 const next = (ctx.state ? ctx.state->Get(KEY_LOOTS, 0) : 0) + 1;
            if (ctx.state)
                ctx.state->Set(KEY_LOOTS, next);

            if (next % Rules::GRAVEDIGGER_EVERY != 0)
                return;

            // The table the riser will owe. Read now, while the corpse is still
            // there to ask: by the time it is killed the original creature may
            // be gone.
            Creature* corpse = ObjectAccessor::GetCreature(*player, lootGuid);
            uint32 const owed = corpse ? corpse->GetCreatureTemplate()->lootid : 0;

            Creature* risen = sGauntletSummons->Summon(player, ENTRY_RISEN, player->GetPosition(),
                                                       Rules::GRAVEDIGGER_LIFE_MS,
                                                       /*countsAsStalker*/ false,
                                                       MECHANIC_GRAVEDIGGER);
            if (!risen)
                return;   // the summon caps refused; the corpse stays a corpse

            _owed     = risen->GetGUID();
            _owedLoot = owed;
            ++_raised;

            risen->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);   // Unit.h:1943

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), 0, "Gravedigger");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r It gets back up. Whatever it kept is still on it.");
        }

        void Gravedigger::Pay(Ctx& ctx, Creature* killed)
        {
            Player* player = ctx.player;
            if (!player || !killed || killed->GetGUID() != _owed)
                return;

            uint32 const lootId = _owedLoot;
            _owed     = ObjectGuid::Empty;
            _owedLoot = 0;

            if (lootId == 0)
                return;   // the corpse it rose from had no table of its own

            // The core's own death path, in the core's own order. Unit::Kill
            // fills a dying creature's loot from its template and then sets
            // UNIT_DYNFLAG_LOOTABLE only when that loot is not already looted
            // (Unit.cpp:14210-14220). ENTRY_RISEN carries lootid 0, so the core
            // filled nothing and would leave the body unlootable; these are the
            // same four calls it would have made had the template had a table.
            killed->loot.clear();
            killed->loot.FillLoot(lootId, LootTemplates_Creature, player, true, true);
            killed->SetLootRecipient(player);
            if (!killed->loot.isLooted())
                killed->SetDynamicFlag(UNIT_DYNFLAG_LOOTABLE);

            ++_paid;

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), 0, "Gravedigger");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r It was holding out. Search it again.");
        }
    }

    GAUNTLET_MECHANIC(114, Gravedigger);
}
