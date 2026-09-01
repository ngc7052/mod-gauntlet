/*
 * mod-gauntlet - 89 Blood for Bread: no meals, and every kill feeds you
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

// Registry id 89. docs/greed-redesign.md section 3 wrote it as Iron Purse's
// replacement -- "you cannot eat or drink; every kill restores 8% of your
// health and mana" -- and docs/commons.md is why it is a Common rather than
// the Epic that document floated.
//
// The measurement, in short: every offer set must contain one card flagged
// MF_RewardShaped, and until this card and its two siblings landed, every row
// that carried the flag was Rare. One slot in three was therefore rare before
// any rarity weight was consulted, and no quantity of ordinary commons moved
// it -- fifty-two hypothetical ones left tier 1 at 54% against a 70% target.
// Three reward-shaped commons and uncommons moved it further than twenty
// ordinary rows did. An Epic carrying the flag would have helped nothing
// before tier 40.
//
// The flag is earned rather than asserted. "Reward-shaped" means the card's
// own mechanic hands the player something when they engage with it, the way
// Champions pays double and Killing Floor hands the bank back on a kill. This
// card deletes downtime and sells it back a kill at a time: you cannot sit
// down to eat, so the only way to recover is to keep killing, and every kill
// pays both bars. The boon column is empty on purpose, exactly as Killing
// Floor's is -- the restore *is* the upside and a BoonClause would promise a
// second one.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_BLOOD_FOR_BREAD = 89;

        // A refusal reaches the client as a generic error, so the card owes
        // the player the reason. The client asks CanUseItem more than once for
        // one click, hence the window -- SimpleTrade's denials throttle the
        // same way and for the same reason.
        constexpr uint32 TOLD_WINDOW_MS = 3000;

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_BLOOD_FOR_BREAD);
            return def ? def->key : "blood_for_bread";
        }

        class BloodForBread final : public IMechanic
        {
        public:
            // ITEM_SUBCLASS_FOOD is the "Food & Drink" subclass
            // (ItemTemplate.h:319), so this one test is both halves of the
            // card. Bandages (subclass 7), potions (1), elixirs (2) and
            // flasks (3) are all deliberately still allowed: the curse is
            // about sitting down, not about consumables. Waste Not (90) is
            // the card that takes potions, and a run may carry both.
            bool CanUseItem(Ctx& ctx, ItemTemplate const* proto) override
            {
                if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE
                    || proto->SubClass != ITEM_SUBCLASS_FOOD)
                    return true;

                ++_refused;
                Tell(ctx.player, proto);
                return false;
            }

            void OnKill(Ctx& ctx, Creature* /*killed*/) override { Feed(ctx); }

            // A kill is a kill. The pet made it, the player still cannot eat.
            void OnPetKill(Ctx& ctx, Creature* /*killed*/) override { Feed(ctx); }

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "You cannot eat or drink to recover. Every kill restores "
                     + std::to_string(Rules::BLOOD_FOR_BREAD_PCT)
                     + "% of your health and mana.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                std::string out = "blood for bread: refused " + std::to_string(_refused)
                                + " meal(s), fed on " + std::to_string(_fed) + " kill(s), restored "
                                + std::to_string(_health) + " health and " + std::to_string(_power)
                                + " power";
                if (_fed == 0)
                    out += "; nothing has been killed since this was attached";
                if (ctx.player && ctx.player->getPowerType() != POWER_MANA)
                    out += "; this character has no mana, so only the health half can pay";
                return out;
            }

        private:
            void Feed(Ctx& ctx)
            {
                Player* player = ctx.player;
                if (!player || !player->IsInWorld() || !player->IsAlive())
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                uint32 const health = Rules::BloodForBreadRestore(player->GetMaxHealth());
                player->ModifyHealth(static_cast<int32>(health));
                _health += health;

                // Only for a character that has mana at all. A warrior's rage
                // and a rogue's energy refill on their own and a card that
                // "restored" three energy would be claiming credit for the
                // core's own regeneration.
                uint32 power = 0;
                if (player->getPowerType() == POWER_MANA)
                {
                    power = Rules::BloodForBreadRestore(player->GetMaxPower(POWER_MANA));
                    player->ModifyPower(POWER_MANA, static_cast<int32>(power));
                    _power += power;
                }

                ++_fed;

                // Once, at the first kill. The bars move on screen and the
                // affix is in the panel; a line per kill would be noise, and
                // the one thing the player needs told is which card did it.
                if (_fed == 1 && player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r Blood for Bread: the kill feeds you. It is the only "
                        "thing that will.");

                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "Blood for Bread");
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
                    "|cffff2020[Gauntlet]|r Blood for Bread: you cannot eat or drink. Kill something.");
            }

            uint32 _refused    = 0;
            uint32 _fed        = 0;
            uint32 _health     = 0;
            uint32 _power      = 0;
            uint32 _lastEntry  = 0;
            uint32 _lastToldMs = 0;
        };
    }

    GAUNTLET_MECHANIC(89, BloodForBread);
}
