/*
 * mod-gauntlet - E6 Nimble: enemies move faster
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletMgr.h"
#include "GauntletSummons.h"
#include "../Boons.h"
#include "../Nearby.h"

#include "Creature.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Unit.h"

#include <algorithm>
#include <string>
#include <vector>
#include <iterator>

// Registry id 11. Design section 3, card E6: "Enemies move 30% faster."
//
// A soft counter to kiting, which is the point, capped so that it is never a
// hard one: the ceiling is Gauntlet.Caps.EnemySpeed (design section 4.3's
// "enemy speed <= 140%") and mounts are untouched, because MOVE_RUN is not the
// speed a mounted player travels at -- so design section 2.8's eighth principle,
// never remove the universal escape, holds by construction.
//
// This is the first mechanic to use AggregateKind::EnemySpeed, so it is also
// the first to make that cap and that config key mean anything. The factor is
// reported through AggregateFactor and read back through Mgr::Aggregate, which
// is what puts it in `.gauntlet status` beside the others and what applies the
// clamp exactly once.

namespace Gauntlet
{
    namespace
    {
        // The card's ladder: 20 -> 30 -> 40% faster, and it ends there.
        //
        // Nimble keeps maxRank = 3 while the rest of the table moved to four,
        // and the reason is two lines above this one: the factor goes through
        // AggregateKind::EnemySpeed, whose ceiling is Gauntlet.Caps.EnemySpeed
        // at 1.40. Rank III is already exactly at it, so a rank IV of this
        // mechanic would be clamped to the same number and the offer would
        // promise an escalation that does not exist -- the fault this whole
        // redesign was written to remove.
        //
        // Raising the cap is not the fix either: 140% is design section 4.3's
        // number and section 2.8's eighth principle, never remove the
        // universal escape, is what it protects. A fourth rank here needs a
        // second axis, not a bigger multiplier, and there is not one on the
        // card. The fourth entry below exists only to satisfy the assert and
        // is unreachable: Eligible refuses a rank-up past def.maxRank.
        constexpr uint32 SPEED_PCT[] = { 20, 30, 40, 40 };
        static_assert(std::size(SPEED_PCT) >= MAX_RANK, "SPEED_PCT is short a rank");

        // How many creatures are hurried at once. A pull bigger than this is
        // already a death; the cap bounds the bookkeeping, not the affix.
        constexpr std::size_t MAX_HURRIED = 8;   // TODO(design)

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        class Nimble final : public IMechanic
        {
        public:
            void OnAttach(Ctx&) override { _hurried.clear(); }
            void OnDetach(Ctx& ctx) override { RestoreAll(ctx); }
            void OnLeaveCombat(Ctx& ctx) override { RestoreAll(ctx); }
            void OnTick(Ctx& ctx, uint32 diffMs) override;

            // Two answers from one callback. EnemySpeed is the curse, and it is
            // reported here rather than applied here so that plan section 2.5's
            // clamp is applied to the product once, in the one place that owns
            // it. MaxHealth is the boon: enemies you can no longer kite have to
            // be fought standing still, and a bigger pool is what pays for that.
            float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
            {
                if (kind == AggregateKind::EnemySpeed)
                    return 1.0f + static_cast<float>(SPEED_PCT[RankIndex(&self)]) / 100.0f;

                return BoonFactor(self, kind);
            }

            std::string Describe(AffixInstance const& self) const override;

        private:
            // What one creature's run rate was before this affix touched it.
            // A float and a guid, never a pointer, so a stale record cannot
            // dangle -- and the rate is what is put back rather than a division
            // by the multiplier, which would drift.
            struct Hurried
            {
                ObjectGuid guid;
                float      rate = 1.0f;
            };

            void Hurry(Ctx& ctx, Creature* creature, float mult);
            void Restore(Player* owner, Hurried const& record);
            void RestoreAll(Ctx& ctx);
            bool Known(ObjectGuid const& guid) const;

            std::vector<Hurried> _hurried;
        };

        bool Nimble::Known(ObjectGuid const& guid) const
        {
            for (Hurried const& record : _hurried)
                if (record.guid == guid)
                    return true;
            return false;
        }

        void Nimble::OnTick(Ctx& ctx, uint32 /*diffMs*/)
        {
            Player* player = ctx.player;
            if (!player || !player->IsInWorld())
            {
                _hurried.clear();
                return;
            }

            // Put back anything that is no longer in the fight, first, so a
            // creature that evaded is at its own speed again before the next
            // sweep can consider re-hurrying it.
            for (std::size_t i = _hurried.size(); i-- > 0; )
            {
                Hurried const& record = _hurried[i];

                Creature* creature = ObjectAccessor::GetCreature(*player, record.guid);
                bool const gone = !creature || !creature->IsInWorld() || !creature->IsAlive()
                               || creature->IsInEvadeMode()                 // Creature.h:138
                               || !creature->IsInCombatWith(player);        // Unit.h:939

                if (!gone)
                    continue;

                Restore(player, record);
                _hurried.erase(_hurried.begin() + static_cast<std::ptrdiff_t>(i));
            }

            if (ctx.run && (ctx.run->dead || !ctx.run->pending.empty()))
                return;
            if (!player->IsAlive() || !player->IsInCombat())
                return;

            // The card says "every non-elite creature that enters combat with
            // the owner", and the honest reading of "in combat with the owner"
            // is the owner's own attacker set: PlayerScript::OnPlayerEnterCombat
            // fires once, on the out-of-combat edge, with one enemy, so a mob
            // that joined a fight already under way would never be seen. This
            // sweep sees all of them and costs one set walk per module tick.
            float const mult = sGauntlet->Aggregate(player, AggregateKind::EnemySpeed);
            if (mult <= 1.0f)
                return;

            for (Unit* attacker : player->getAttackers())                  // Unit.h:901
            {
                Creature* creature = attacker ? attacker->ToCreature() : nullptr;
                if (!creature || Known(creature->GetGUID()))
                    continue;
                if (!IsOrdinaryFoe(creature) || !creature->IsAlive())
                    continue;

                // Nothing this module summoned is hurried. The Shade's card
                // fixes its speed at 85% of a player's precisely so it can be
                // outpaced, and speeding it up would take that away.
                if (sGauntletSummons->IsGauntletSummon(creature))
                    continue;

                if (_hurried.size() >= MAX_HURRIED)
                    break;

                Hurry(ctx, creature, mult);
            }
        }

        void Nimble::Hurry(Ctx& ctx, Creature* creature, float mult)
        {
            Hurried record;
            record.guid = creature->GetGUID();
            record.rate = creature->GetSpeedRate(MOVE_RUN);                // Unit.h:1740

            // Unit::SetSpeed rather than the card's SetSpeedRate. The latter
            // writes m_speed_rate and nothing else (Unit.h:1742), so the server
            // would move the creature faster while every client went on drawing
            // it at the old speed; SetSpeed writes the same field, calls
            // propagateSpeedChange and -- with `forced` -- sends the opcode
            // (Unit.cpp:11310-11335). A creature that is faster than it looks
            // is exactly the untelegraphed punishment design section 2.9 lists
            // as a known-bad pattern.
            creature->SetSpeed(MOVE_RUN, record.rate * mult, true);

            _hurried.push_back(record);

            // No chat line and no EVT. This affix is a standing property of the
            // fight rather than an event, the creature visibly moving faster is
            // its own telegraph, and a line per creature per pull would drown
            // out the affixes that do have moments.
            if (ctx.addon && ctx.player)
                ctx.addon->QueueStat(ctx.player, "nimble",
                                     static_cast<int32>((mult - 1.0f) * 100.0f + 0.5f));
        }

        void Nimble::Restore(Player* owner, Hurried const& record)
        {
            if (!owner || !owner->IsInWorld())
                return;

            Creature* creature = ObjectAccessor::GetCreature(*owner, record.guid);
            if (!creature || !creature->IsInWorld())
                return;

            creature->SetSpeed(MOVE_RUN, record.rate, true);
        }

        void Nimble::RestoreAll(Ctx& ctx)
        {
            for (Hurried const& record : _hurried)
                Restore(ctx.player, record);

            _hurried.clear();

            if (ctx.addon && ctx.player)
                ctx.addon->QueueStat(ctx.player, "nimble", 0);
        }

        std::string Nimble::Describe(AffixInstance const& self) const
        {
            uint8 const i = RankIndex(&self);

            std::string out = "Every ordinary enemy fighting you runs "
                            + std::to_string(SPEED_PCT[i])
                            + "% faster, up to the realm's ceiling, until it evades or the fight"
                              " ends. Mounts are untouched, so riding away still works; snares"
                              " matter again, and so does choosing the fight before you take it.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(11, Nimble);
}
