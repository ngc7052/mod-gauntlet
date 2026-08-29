/*
 * mod-gauntlet - deterministic offer builder
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_GENERATOR_H
#define MOD_GAUNTLET_GENERATOR_H

#include "Gauntlet.h"

#include <vector>

namespace Gauntlet
{
    // Design section 3's family-B header: bargains are "rare (weighted ~1 in 6
    // offers), only from tier 6, and each may be taken once per run". The
    // offer builder enforces the tier half here; the registry's own windows
    // must not contradict it, and RegistryTest asserts that they do not.
    // The first tier at which anything in the table can be offered.
    //
    // Every window was multiplied by five when a tier became a level, so the
    // earliest -- Champions, Carrion, Hubris, Overextended and the three Rules
    // rows, all of them 1 before -- now opens at 5. That is the level the old
    // tier 1 was reached at, so nothing has actually moved; it just has a
    // number now, and the sweeps need to know it so they do not measure four
    // tiers of guaranteed emptiness and call it a collision.
    constexpr uint8 FIRST_TIER = 5;

    constexpr uint8 BARGAIN_MIN_TIER = 30;

    // The most affixes a run may carry at once.
    //
    // It exists because the tier axis became one tier per level: eighty offers
    // instead of sixteen, and nothing anywhere refused a New pick, so a run
    // would have ended somewhere past thirty simultaneous curses. That is not
    // a harder run, it is a run where no individual affix matters -- the
    // damage-taken ceiling and the maximum-health floor both sit pegged from
    // the midgame on, and the event budget, 1 + 0.25 x (timed - 1), stretches
    // every cadence past x4 so the timed affixes barely act at all.
    //
    // Sixteen is the design's own bound: plan section 2.2 puts the dispatch
    // loop at "<= 16, so no indexing needed", and it is what the aggregate
    // caps were sized against.
    //
    // Reaching it does not end the choosing. It changes what is on offer: a
    // full set can still rank up and can still swap, so the late run is about
    // deepening what you have and deciding what to give up, which is a better
    // question than "which of three more".
    constexpr uint8 MAX_CARRIED = 16;

    // ---------------------------------------------------------------------
    // The roll stream.
    //
    // splitmix64, byte-identical on every platform, so a seed reproduces a run
    // anywhere. It lives in a header because generator 1 and generator 2 must
    // draw from the same arithmetic: the legacy roll is frozen against a
    // fixture, and a second copy of these four lines is a second thing that
    // can drift. Deliberately not std::rand, and deliberately not seeded from
    // anything the world can change.
    // ---------------------------------------------------------------------
    namespace Stream
    {
        inline uint64 Mix(uint64 x)
        {
            x += 0x9E3779B97F4A7C15ULL;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
            return x ^ (x >> 31);
        }

        inline uint32 RollIn(uint64& state, uint32 lo, uint32 hi)
        {
            state = Mix(state);
            return lo + static_cast<uint32>(state % (hi - lo + 1));
        }
    }

    // A view of the registry the offer builder may draw from. The live one
    // hides MF_NotImplemented entries; the tests use one that does not, so the
    // invariants exercise the whole 73-entry table rather than the four
    // mechanics Phase 0 happens to implement.
    struct RegistryView
    {
        bool includeUnimplemented = false;
    };

    // Why the builder had to relax a rule; empty when it did not.
    //
    // The ladder is fixed and is walked in this order: keep every rule; then
    // allow a family a sibling slot already used; then allow a mechanic another
    // slot already offered. Phase 0 had a fourth rung -- fall back to the
    // scalar pool with every structural rule dropped -- and Phase 2 deleted the
    // scalars, so there is nothing below the third any more and a slot that
    // cannot be filled comes back empty instead.
    enum GeneratorRelaxation : uint32
    {
        GR_None             = 0,
        GR_RepeatedFamily   = 1u << 0,
        GR_RepeatedMechanic = 1u << 1,

        // Set when a slot could not be filled at all and came back as
        // MECHANIC_NONE, and also when the "one reward-shaped offer per tier"
        // guarantee found no reward-shaped candidate -- which is the same
        // failure, nothing eligible, seen from the other end.
        //
        // The bit is the one Phase 0 called GR_FellBackToScalar and its value
        // is unchanged; only the name is, because there is no scalar to fall
        // back to any more.
        GR_NoCandidate      = 1u << 2
    };

    struct OfferSet
    {
        std::vector<Offer> offers;
        uint32 relaxations = GR_None;
    };

    // Deterministic in (seed, tier, view, carried, count, reg,
    // GeneratorVersion) and in nothing else: no clock, no rand(), no pointer
    // value, no unordered container on a path that feeds a roll. The offers
    // are never stored -- they are rebuilt from the seed every time the tier
    // prompt is shown -- so anything that changes the table, the weights or
    // this algorithm must bump GeneratorVersion, which is folded into the
    // stream. A pick, once taken, is stored in columns and never regenerated.
    OfferSet BuildOffers(uint32 seed, uint8 tier, IPlayerView const& view,
                         std::vector<AffixInstance> const& carried,
                         uint32 count = 3, RegistryView reg = {},
                         uint8 maxCarried = MAX_CARRIED);
}

#endif // MOD_GAUNTLET_GENERATOR_H
