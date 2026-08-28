/*
 * mod-gauntlet - T5 Hubris: easy kills are worth nothing, hard ones are worth more
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Boons.h"

#include "Player.h"
#include "Unit.h"

#include <algorithm>
#include <limits>
#include <string>

// Registry id 18. Design section 3, card T5: "Enemies below your level give no
// experience; enemies above give 40% more."
//
// This is the affix that replaces Forgetful, and the difference between them is
// the whole argument of design section 5. Forgetful was a flat experience tax:
// no moment, no verb, and the always-safe pick that made the other two offers
// irrelevant. Hubris is a *route* rule with real risk attached -- level in the
// zone one step ahead, fight orange and red with cooldowns up, or accept
// quest-only experience for a stretch -- and the player turns it every time they
// choose where to grind.
//
// It is reward-shaped in the registry because the upside is real: the bonus on
// anything above your level is the boon, and it is what the row's
// Boon::BonusExperience names.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_HUBRIS = 18;

        // The card's ladder: x0.5/x1.2 -> x0.25/x1.3 -> x0/x1.4. The penalty is
        // a percentage kept, so 50 -> 25 -> 0.
        constexpr uint32 BELOW_KEEP_PCT[MAX_RANK] = { 50, 25, 0 };

        // The bonus half, and the fallback when an instance carries no boon
        // magnitude. The generator gives this row its own BoonTable entry so
        // that boonMag is 20/30/40 and the offer card promises exactly what the
        // mechanic pays; this array is what a hand-built instance falls back to.
        constexpr uint32 ABOVE_BONUS_PCT[MAX_RANK] = { 20, 30, 40 };

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_HUBRIS);
            return def ? def->key : "hubris";
        }

        class Hubris final : public IMechanic
        {
        public:
            void OnXP(Ctx& ctx, uint32& amount, Unit* victim) override;
            void OnTick(Ctx& ctx, uint32 diffMs) override;

            std::string Describe(AffixInstance const& self) const override;

        private:
            void Publish(Ctx& ctx);

            // What the last kill was worth, so the addon can show the rule
            // acting. -100 means "that one paid nothing".
            int32  _lastDelta = 0;
            uint32 _publishMs = 0;
        };

        void Hubris::OnXP(Ctx& ctx, uint32& amount, Unit* victim)
        {
            Player* player = ctx.player;
            if (!player || amount == 0)
                return;

            // "Quest XP untouched." Player::GiveXP is called with a null victim
            // for a quest reward and for every other non-kill source
            // (Player.cpp, GiveQuestSourceItem and friends), so the absence of
            // a victim is the core's own answer to "was this a kill" and needs
            // no extra plumbing through the hook.
            if (!victim)
                return;

            uint8 const i = RankIndex(ctx.self);

            uint8 const mine   = player->GetLevel();
            uint8 const theirs = victim->GetLevel();

            if (theirs < mine)
            {
                uint32 const keep = BELOW_KEEP_PCT[i];
                amount = static_cast<uint32>(static_cast<uint64>(amount) * keep / 100u);
                _lastDelta = -100 + int32(keep);
                return;
            }

            if (theirs > mine)
            {
                uint32 const bonus = (ctx.self && ctx.self->boonMag != 0)
                                   ? uint32(ctx.self->boonMag)
                                   : ABOVE_BONUS_PCT[i];

                uint64 const raised = static_cast<uint64>(amount) * (100u + bonus) / 100u;
                amount = static_cast<uint32>(std::min<uint64>(raised, std::numeric_limits<uint32>::max()));
                _lastDelta = int32(bonus);
                return;
            }

            // Exactly your level: the card names only "below" and "above", so a
            // kill at your own level is left alone. It is also the level band a
            // player routing around this affix will spend most of their time
            // in, which makes "stay level with the zone" the neutral choice
            // rather than the punished one.
            _lastDelta = 0;
        }

        void Hubris::OnTick(Ctx& ctx, uint32 diffMs)
        {
            // Coalesced rather than sent from OnXP: a chain of kills would
            // otherwise be one message each, and the useful reading is "what is
            // this grind paying" rather than "what did that one pay".
            _publishMs += diffMs;
            if (_publishMs < 1000)
                return;
            _publishMs = 0;

            Publish(ctx);
        }

        void Hubris::Publish(Ctx& ctx)
        {
            if (ctx.addon && ctx.player)
                ctx.addon->QueueStat(ctx.player, MechanicKey(), _lastDelta);
        }

        std::string Hubris::Describe(AffixInstance const& self) const
        {
            uint8 const  i     = RankIndex(&self);
            uint32 const keep  = BELOW_KEEP_PCT[i];
            uint32 const bonus = self.boonMag != 0 ? uint32(self.boonMag) : ABOVE_BONUS_PCT[i];

            std::string out = "Enemies below your level give ";
            out += keep == 0 ? "no experience" : ("only " + std::to_string(keep) + "% of their experience");
            out += "; enemies above your level give " + std::to_string(bonus)
                 + "% more. Quest experience is untouched. Level in the zone one step ahead.";

            // No BoonClause: the bonus above is this affix's boon, already
            // named in the sentence, and a second "in exchange" line would read
            // as a second reward.
            return out;
        }
    }

    GAUNTLET_MECHANIC(18, Hubris);
}
