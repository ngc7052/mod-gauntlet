/*
 * mod-gauntlet - 90 Waste Not: no potions, and every kill mends you
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "../Boons.h"

#include "Chat.h"
#include "GameTime.h"
#include "ItemTemplate.h"
#include "Player.h"

#include <string>

// Registry id 90, and the third of docs/commons.md's reward-shaped cards.
// Blood for Bread (89) documents why the three exist; this is the smaller
// version of the same trade and the one a run is likeliest to meet first.
//
// It takes the emergency button rather than the meal. A potion is the thing a
// player reaches for when a pull goes wrong, and hardcore is the mode where
// that press is the difference between a run and a corpse -- so the curse is
// felt exactly when it matters and never otherwise, which is the shape design
// section 2.8 asks for. What it hands back is the same button pressed a
// different way: kill the thing that is hurting you and it mends you, for
// less than a potion would have, with no cooldown and no stack to run out.
//
// The card names potions and nothing else. ItemTemplate::IsPotion
// (ItemTemplate.h:817) is the core's own answer to what one is, so healing
// potions, mana potions and every rejuvenation flavour go together; elixirs,
// flasks, bandages and food are untouched. Blood for Bread is the card that
// takes the food, and a run may carry both -- which is a hard run, honestly
// arrived at, and exactly the pile the module is for.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_WASTE_NOT = 90;

        constexpr uint32 TOLD_WINDOW_MS = 3000;

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_WASTE_NOT);
            return def ? def->key : "waste_not";
        }

        class WasteNot final : public IMechanic
        {
        public:
            bool CanUseItem(Ctx& ctx, ItemTemplate const* proto) override
            {
                if (!proto || !proto->IsPotion())
                    return true;

                ++_refused;
                Tell(ctx.player, proto);
                return false;
            }

            void OnKill(Ctx& ctx, Creature* /*killed*/) override { Mend(ctx); }
            void OnPetKill(Ctx& ctx, Creature* /*killed*/) override { Mend(ctx); }

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "You cannot drink a potion. Every kill restores "
                     + std::to_string(Rules::WASTE_NOT_PCT) + "% of your health.";
            }

            std::string Diagnose(Ctx&) const override
            {
                std::string out = "waste not: refused " + std::to_string(_refused)
                                + " potion(s), mended on " + std::to_string(_mended)
                                + " kill(s), restored " + std::to_string(_health) + " health";
                if (_mended == 0)
                    out += "; nothing has been killed since this was attached";
                return out;
            }

        private:
            void Mend(Ctx& ctx)
            {
                Player* player = ctx.player;
                if (!player || !player->IsInWorld() || !player->IsAlive())
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                uint32 const health = Rules::WasteNotRestore(player->GetMaxHealth());
                player->ModifyHealth(static_cast<int32>(health));
                _health += health;
                ++_mended;

                if (_mended == 1 && player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Waste Not: the kill mends you. There is no bottle "
                        "to reach for now.");

                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "Waste Not");
            }

            void Tell(Player* player, ItemTemplate const* proto)
            {
                if (!player || !player->GetSession())
                    return;

                uint32 const now = static_cast<uint32>(GameTime::GetGameTimeMS().count());
                if (proto->ItemId == _lastEntry && now - _lastToldMs <= TOLD_WINDOW_MS)
                    return;

                _lastEntry  = proto->ItemId;
                _lastToldMs = now;
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Waste Not: the bottle stays corked. Kill something instead.");
            }

            uint32 _refused    = 0;
            uint32 _mended     = 0;
            uint32 _health     = 0;
            uint32 _lastEntry  = 0;
            uint32 _lastToldMs = 0;
        };
    }

    GAUNTLET_MECHANIC(90, WasteNot);
}
