/*
 * mod-gauntlet - 116 The Tenth Corpse: nine hold nothing, the tenth holds all of it
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "GauntletState.h"
#include "../Boons.h"

#include "Chat.h"
#include "Creature.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <string>

// Registry id 116, docs/greed-redesign.md section 7.3, and an epic by the
// rarity ladder's definition: it changes how the whole run loots.
//
// Nine corpses hold nothing but their quest items. The tenth holds everything
// the nine were carrying -- their tables, rolled in turn into one window. The
// run is *faster*, because nine loot windows are never opened, and it is
// riskier, because dying on the ninth throws all of it away.
//
// Exclusive with Fresh Kill through the "loot-rhythm" key. Two cards that
// rewrite when a corpse pays are one card twice, and a run carrying both would
// have Fresh Kill's eight-second clock deciding whether The Tenth Corpse's
// bank was ever filled.
//
// The nine tables are remembered as loot ids in the state store rather than as
// items, which is what makes the bank survive a logout: an id is an int32 and
// the store holds those, and the tenth corpse rolls them fresh. A player who
// logs out on the seventh comes back on the seventh.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_TENTH_CORPSE = 116;

        constexpr char const* KEY_OPENED = "tenth.opened";

        // "tenth.bank0" .. "tenth.bank8", well inside State::MaxKeyLen.
        std::string BankKey(int32 slot)
        {
            return "tenth.bank" + std::to_string(slot);
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_TENTH_CORPSE);
            return def ? def->key : "tenth_corpse";
        }

        class TenthCorpse final : public IMechanic
        {
        public:
            void OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot) override
            {
                Player* player = ctx.player;
                if (!loot || !player || !ctx.state)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                Creature* corpse = ObjectAccessor::GetCreature(*player, lootGuid);
                uint32 const lootId = corpse ? corpse->GetCreatureTemplate()->lootid : 0;

                int32 const opened = ctx.state->Get(KEY_OPENED, 0) + 1;
                ctx.state->Set(KEY_OPENED, opened);

                int32 const into = opened % Rules::TENTH_EVERY;
                if (into != 0)
                {
                    // One of the nine. It keeps its quest items -- a card may
                    // take your greens, never your quest (section 7.1) -- and
                    // its table is banked for the tenth.
                    ctx.state->Set(BankKey(into - 1), static_cast<int32>(lootId));

                    if (!loot->items.empty())
                    {
                        loot->items.clear();
                        ++_emptied;
                    }

                    if (player->GetSession())
                        ChatHandler(player->GetSession()).PSendSysMessage(
                            "|cffff2020[Gauntlet]|r Nothing here. {} more.",
                            Rules::TenthCorpseLeft(opened));
                    return;
                }

                // The tenth. Every table the nine were carrying, rolled into
                // this one window, and then this corpse's own on top.
                uint32 rolled = 0;
                for (int32 slot = 0; slot < Rules::TENTH_EVERY - 1; ++slot)
                {
                    uint32 const banked = static_cast<uint32>(std::max(0, ctx.state->Get(BankKey(slot), 0)));
                    ctx.state->Set(BankKey(slot), 0);
                    if (banked == 0)
                        continue;

                    loot->FillLoot(banked, LootTemplates_Creature, player, true, true);
                    ++rolled;
                }

                if (lootId != 0)
                {
                    loot->FillLoot(lootId, LootTemplates_Creature, player, true, true);
                    ++rolled;
                }

                _paid += rolled;
                ++_tenths;

                if (ctx.addon)
                    ctx.addon->SendEvent(player, MechanicKey(), 0, "The Tenth Corpse");

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r The tenth. Everything the other nine were keeping.");
            }

            std::string Describe(AffixInstance const& /*self*/) const override
            {
                return "Corpses hold nothing but their quest items until the "
                     + std::to_string(Rules::TENTH_EVERY)
                     + "th. That one holds everything the others were carrying.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                int32 const opened = ctx.state ? ctx.state->Get(KEY_OPENED, 0) : 0;
                return "tenth corpse: " + std::to_string(opened) + " opened, "
                     + std::to_string(_emptied) + " emptied, " + std::to_string(_tenths)
                     + " paid out over " + std::to_string(_paid) + " table(s); "
                     + std::to_string(Rules::TenthCorpseLeft(opened)) + " to the next payout";
            }

        private:
            uint32 _emptied = 0;
            uint32 _tenths  = 0;
            uint32 _paid    = 0;
        };
    }

    GAUNTLET_MECHANIC(116, TenthCorpse);
}
