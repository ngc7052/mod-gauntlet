/*
 * mod-gauntlet - the druid's one: Bound Skin
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Boons.h"

#include "Chat.h"
#include "Player.h"
#include "SharedDefines.h"

#include <array>
#include <string>

// Design section 3, family C, druid. The identity verb: powershifting is gone
// and leaving Cat to heal becomes a commitment rather than a flicker. The card
// names the real change -- the class decides *before* the pull whether this is
// a Bear fight or a Cat fight, and learns to heal at 50% instead of 15%.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_BOUND_SKIN = 64;

        // The card's ladder, in seconds of lockout after any form change.
        constexpr uint32 SHIFT_LOCK_MS[MAX_RANK] = { 4000, 6000, 10000 };

        // Every form-granting spell, so the cooldown lands on the buttons and
        // not merely on the state. Base ranks; GetFirstSpellInChain is not
        // needed because none of these has one.
        constexpr std::array<uint32, 7> FORM_SPELLS = { {
            5487,   // Bear Form
            9634,   // Dire Bear Form
            768,    // Cat Form
            783,    // Travel Form
            1066,   // Aquatic Form
            24858,  // Moonkin Form
            33891   // Tree of Life
        } };

        uint8 RankIndexOf(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* KeyOf(uint16 id, char const* fallback)
        {
            MechanicDef const* def = FindMechanic(id);
            return def ? def->key : fallback;
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        class BoundSkin final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                // Every form back immediately. A druid who swapped this affix
                // away should not be stuck in caster form for ten seconds.
                if (Player* player = ctx.player)
                    for (uint32 id : FORM_SPELLS)
                        player->RemoveSpellCooldown(id, /*update*/ true);
            }

            // Fires on every form change including the one *out* of a form,
            // which is correct: the card prices changing, not entering.
            void OnShapeshift(Ctx& ctx, uint8 /*form*/) override
            {
                Player* player = ctx.player;
                if (!player)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                uint32 const ms = SHIFT_LOCK_MS[RankIndexOf(ctx.self)];

                for (uint32 id : FORM_SPELLS)
                    player->AddSpellCooldown(id, 0, ms, /*needSendToClient*/ true);

                ++_shifts;

                AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_BOUND_SKIN, "c37_bound_skin"),
                                         ms / 1000u, "Bound Skin");
            }

            // Boon::BonusMaxHealth, and the card gates it: "+10% max health
            // while shapeshifted". AggregateFactor is Player-free and cannot
            // see the form, so it is applied here where the pool is built.
            void OnMaxHealth(Ctx& ctx, float& value) override
            {
                Player* player = ctx.player;
                if (!player || !ctx.self || ctx.self->boonMag == 0)
                    return;
                if (player->GetShapeshiftForm() == FORM_NONE)
                    return;

                value *= 1.0f + float(ctx.self->boonMag) / 100.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const secs = SHIFT_LOCK_MS[RankIndexOf(&self)] / 1000u;
                uint32 const pct  = self.boonMag;

                std::string out = "Changing form locks every form for " + std::to_string(secs)
                                + " seconds. Decide before the pull whether this is a Bear fight"
                                  " or a Cat fight.";

                if (pct != 0)
                    out += " In exchange you have " + std::to_string(pct)
                         + "% more health while shapeshifted.";

                return out;
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                return "bound skin: " + std::to_string(SHIFT_LOCK_MS[RankIndexOf(ctx.self)] / 1000u)
                     + "s lock, " + std::to_string(_shifts) + " shift(s)";
            }

        private:
            uint32 _shifts = 0;
        };
    }

    GAUNTLET_MECHANIC(64, BoundSkin);
}
