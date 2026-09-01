/*
 * mod-gauntlet - 117 Elite Tithe: the elite's blue is yours, and the elite is worse for it
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "../Boons.h"

#include "Chat.h"
#include "Creature.h"
#include "ItemTemplate.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <string>

// Registry id 117, docs/greed-redesign.md section 7.3 and the brief's own
// example: "killing elite guarantees loot".
//
// Every candidate of uncommon quality or better in an elite's pockets goes to
// a certain drop, and elites hit 25% harder for it. That is the whole trade:
// the fight you were avoiding becomes the fight you want, and it is more
// dangerous than the one you were avoiding.
//
// Section 7.1's honest limit is worth repeating here, because this is the card
// most likely to disappoint someone who has not read it: **quality cannot be
// invented**. The card makes an elite drop the blue that is in its table; it
// cannot put a blue into a table that has none. Most elites at level have a
// green or two and nothing better, and those are the greens this guarantees.
//
// The seam is GlobalScript::OnItemRoll, which the core consults once per
// candidate item per loot (LootMgr.cpp:315, :1276) and which hands over the
// chance by reference. Setting it to 100 or more is a guarantee (:318). The
// adapter used to drop the candidate and the source on the floor and was
// widened for this card: neither can be recovered later, because by the time a
// loot window opens every roll is already over.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_ELITE_TITHE = 117;

        constexpr float CERTAIN = 100.0f;

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_ELITE_TITHE);
            return def ? def->key : "elite_tithe";
        }

        // Creature::isElite() is false for rank RARE, which is deliberate in
        // the core and wrong for this card: a silver dragon is exactly the
        // fight this is about. Champions reads all three the same way.
        bool IsElite(Creature const* c)
        {
            if (!c)
                return false;
            if (c->isElite() || c->isWorldBoss() || c->IsDungeonBoss())
                return true;
            uint32 const rank = c->GetCreatureTemplate()->rank;
            return rank == CREATURE_ELITE_RARE || rank == CREATURE_ELITE_RAREELITE;
        }

        class EliteTithe final : public IMechanic
        {
        public:
            void OnItemRoll(Ctx& ctx, float& chance, ItemTemplate const* item,
                            ObjectGuid const& source) override
            {
                Player* player = ctx.player;
                if (!player || !item || !source)
                    return;

                // Uncommon or better only. A guarantee on every grey would be
                // a card about carrying vendor trash, and the sentence on the
                // card says "everything uncommon or better in their pockets".
                if (item->Quality < ITEM_QUALITY_UNCOMMON)
                    return;

                Creature* corpse = ObjectAccessor::GetCreature(*player, source);
                if (!IsElite(corpse))
                    return;

                if (chance < CERTAIN)
                {
                    chance = CERTAIN;
                    ++_forced;
                }
            }

            // And the other half. Hubris' shape: the multiplier is decided by
            // who is hitting you, so a card that makes elites harder does not
            // make the trash harder too.
            float DamageTakenMult(Ctx& /*ctx*/, Unit* attacker, SpellInfo const*) override
            {
                Creature* c = attacker ? attacker->ToCreature() : nullptr;
                return IsElite(c) ? Rules::EliteTitheTakenMult() : 1.0f;
            }

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "Elites hit you " + std::to_string(Rules::ELITE_TITHE_TAKEN_PCT)
                     + "% harder, and always give up everything uncommon or better they carry.";
            }

            std::string Diagnose(Ctx&) const override
            {
                std::string out = "elite tithe: " + std::to_string(_forced)
                                + " drop(s) forced to certain";
                if (_forced == 0)
                    out += "; no elite has been looted since this was attached";
                return out;
            }

        private:
            uint32 _forced = 0;
        };
    }

    GAUNTLET_MECHANIC(117, EliteTithe);
}
