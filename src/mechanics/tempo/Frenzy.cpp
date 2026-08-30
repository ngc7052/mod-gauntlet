/*
 * mod-gauntlet - T2 Frenzy: chained kills stack damage dealt and damage taken
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletSummons.h"
#include "../Boons.h"

#include "Chat.h"
#include "Creature.h"
#include "Player.h"
#include "Unit.h"

#include <algorithm>
#include <string>
#include <iterator>

// Registry id 15. Design section 3, card T2: "Each kill within 8 seconds of the
// last stacks Frenzy: +6% damage dealt and +6% damage taken per stack."
//
// The card calls this "a dial the player turns", and that is exactly what makes
// it reward-shaped: chain-pull for speed or pause to drop stacks before a
// dangerous target, and the decision recurs every pull. It is the risk/reward
// version of "vengeful enemies"; the punitive version was cut in the design
// because it only ever says "slow down".
//
// The two halves are the curse and the boon of the same affix, which is why the
// registry names Boon::BonusDamage for it and why the generator gives this row
// its own magnitude: the boon is the damage-dealt half of a stack, and it is
// worth exactly what the card says it is worth.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_FRENZY = 15;

        // The card's ladder: 4 -> 6 -> 8% per stack, both ways, and 10 at
        // rank IV. BoonTable's override for this row is 2 + 2 * rank, which
        // gives 10 at rank IV on its own -- the two have to agree, because
        // the boon is the damage-dealt half of this same number.
        constexpr uint32 PCT_PER_STACK[] = { 4, 6, 8, 10 };
        static_assert(std::size(PCT_PER_STACK) >= MAX_RANK, "PCT_PER_STACK is short a rank");

        // The card's two fixed numbers.
        constexpr uint32 MAX_STACKS = 5;
        constexpr uint32 WINDOW_MS  = 8000;

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_FRENZY);
            return def ? def->key : "frenzy";
        }

        class Frenzy final : public IMechanic
        {
        public:
            // Stacks are transient by design. Plan section 3.3 names "Frenzy
            // stacks" in the list of state that is not persisted and resets on
            // login, and it should: eight seconds of chain-killing is a fact
            // about the pull you are in, not about the run.
            void OnAttach(Ctx& ctx) override { Reset(ctx); }
            void OnDetach(Ctx& ctx) override { Reset(ctx); }

            void OnKill(Ctx& ctx, Creature* killed) override { Chain(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Chain(ctx, killed); }
            void OnTick(Ctx& ctx, uint32 diffMs) override;

            // Damage taken is no longer touched.
            //
            // Frenzy used to raise damage dealt *and* damage taken per stack,
            // which made the card fight itself: it asked the player to
            // chain-pull and then punished them for the chain they built, so
            // the correct play was often to let it fall off. A card whose best
            // line is "do not use it" is not a choice.
            //
            // The cost is the chain's fragility instead. Taking a hit resets it
            // to nothing (see OnDamageTaken), so the stacks are kept by playing
            // cleanly rather than by accepting a tax -- and that puts the card
            // in tension with Falling Sky, whose strike now costs a Frenzy
            // chain as well as health. See docs/tempo-redesign.md.
            void OnDamageTaken(Ctx& ctx, Unit* /*attacker*/, uint32 amount) override
            {
                if (amount == 0 || _stacks == 0)
                    return;

                Reset(ctx);

                if (ctx.player && ctx.player->GetSession())
                    ChatHandler(ctx.player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r The chain breaks.");
            }

            // The boon half. It is a Mult callback and not AggregateFactor
            // because it is not a standing multiplier: it exists only while the
            // stacks do, and the stacks are what the player is choosing to
            // build. The boon magnitude is the per-stack figure the generator
            // gives this row -- see BoonTable -- so the two halves are equal at
            // every rank, exactly as the card has them, and a player reading the
            // offer sees the real number.
            float DamageDoneMult(Ctx& ctx, Unit* /*victim*/, SpellInfo const*) override
            {
                if (_stacks == 0)
                    return 1.0f;

                uint32 const pct = (ctx.self && ctx.self->boonMag != 0)
                                 ? uint32(ctx.self->boonMag)
                                 : PCT_PER_STACK[RankIndex(ctx.self)];

                return 1.0f + float(pct) / 100.0f * float(_stacks);
            }

            std::string Describe(AffixInstance const& self) const override;

        private:
            void Chain(Ctx& ctx, Creature* killed);
            void Reset(Ctx& ctx);
            void Publish(Ctx& ctx);
            void Announce(Player* player) const;

            uint32 _stacks   = 0;
            uint32 _windowMs = 0;

            // What the chat line last said, so it says it once per chain rather
            // than once per kill. Mutable because Announce is const: it reports
            // the state, it does not change it.
            mutable uint32 _lastAnnounced = 0;
        };

        void Frenzy::Chain(Ctx& ctx, Creature* killed)
        {
            if (!killed || !ctx.player)
                return;
            if (ctx.run && ctx.run->dead)
                return;

            // Nothing this module summoned feeds the dial. An Echo or a
            // Reinforcements copy killed in a chain would let a player build
            // stacks off the very affixes that are supposed to be spending
            // them, and Carrion's scavengers arrive in pairs.
            if (sGauntletSummons->IsGauntletSummon(killed))
                return;

            _windowMs = WINDOW_MS;

            if (_stacks < MAX_STACKS)
                ++_stacks;

            Publish(ctx);
        }

        void Frenzy::OnTick(Ctx& ctx, uint32 diffMs)
        {
            if (_stacks == 0)
                return;

            if (_windowMs > diffMs)
            {
                _windowMs -= diffMs;
                return;
            }

            // The whole stack falls off at once, which is what the card
            // describes ("stacks ... fall off 8 s after the last") and what
            // makes pausing a real decision rather than a slow bleed.
            Reset(ctx);
        }

        void Frenzy::Reset(Ctx& ctx)
        {
            bool const had = _stacks != 0;
            _stacks   = 0;
            _windowMs = 0;

            if (had)
                Publish(ctx);
        }

        void Frenzy::Publish(Ctx& ctx)
        {
            // The counter is the real readout: it changes on every kill, and
            // the whole point of the affix is that the player watches it and
            // decides. CTR coalesces to the latest value per key, so a chain of
            // five kills in five seconds costs at most a handful of messages.
            if (ctx.addon && ctx.player)
                ctx.addon->QueueCounter(ctx.player, MechanicKey(), _stacks, MAX_STACKS);

            Announce(ctx.player);
        }

        // And the two moments that matter, in chat, for a player with no addon.
        //
        // Not every change: five lines per chain is a combat log nobody reads,
        // and the counter above is what a player watching the dial actually
        // wants. But this affix applies no aura -- nothing in this module does,
        // by design, since there are no client patches and no new spell visuals
        // -- so without the addon it is completely invisible, and design
        // section 5's fourth rule is that a scalar you cannot see acting is one
        // you cannot learn from.
        //
        // Full stacks and the drop are the two edges the decision turns on:
        // one says "you are at your most dangerous, in both directions", the
        // other says "the window closed, you are clean again".
        void Frenzy::Announce(Player* player) const
        {
            if (!player || !player->GetSession())
                return;

            if (_stacks == MAX_STACKS && _lastAnnounced != MAX_STACKS)
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Frenzy at five: you deal and take much more damage until the killing stops.");
            }
            else if (_stacks == 0 && _lastAnnounced == MAX_STACKS)
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Frenzy fades.");
            }

            _lastAnnounced = _stacks;
        }

        std::string Frenzy::Describe(AffixInstance const& self) const
        {
            uint8 const  i    = RankIndex(&self);
            uint32 const up   = self.boonMag != 0 ? uint32(self.boonMag) : PCT_PER_STACK[i];

            // No BoonClause here, and that is deliberate: this affix's boon is
            // not "in exchange" for anything, it is the other half of the same
            // stack, and a second sentence promising it again would read as a
            // second bonus.
            return "Each kill within eight seconds of the last stacks Frenzy, up to five: +"
                 + std::to_string(up) + "% damage dealt per stack. Any damage you take breaks the"
                   " chain. Chain-pull clean, or lose it.";
        }
    }

    GAUNTLET_MECHANIC(15, Frenzy);
}
