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
    // The first tier at which anything in the table can be offered, and the
    // first level of a run.
    //
    // The rescale to one tier per level multiplied every window by five, which
    // preserved the *level* each affix unlocked at and therefore put the
    // earliest rows at tier 5. That was the wrong reading of the change: the
    // point of a tier per level is that the run starts choosing at level 1, and
    // four levels of nothing at the very start is exactly the stretch where a
    // player is deciding whether the module is doing anything at all.
    //
    // So the seven rows that opened at the old tier 1 -- Champions, Carrion,
    // Hubris, Overextended and the three Rules rows -- open at tier 1 again.
    // Everything else keeps the level it always unlocked at.
    constexpr uint8 FIRST_TIER = 1;

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

    // Every family's bit set: Family::MAX is 7, so 0x7F.
    //
    // A bit per family rather than seven bools because this travels through
    // BuildOffers as an input the offers are deterministic in, and one byte
    // that a test can write as a literal is easier to reason about than a
    // seven-field struct that has to be compared field by field.
    constexpr uint8 FAMILY_MASK_ALL =
        static_cast<uint8>((1u << static_cast<uint8>(Family::MAX)) - 1u);

    constexpr uint8 FamilyBit(Family f)
    {
        return static_cast<uint8>(1u << static_cast<uint8>(f));
    }

    // The same shape for rarities: one bit each in Rarity order. This is how
    // Draw tells RollRarity which rarities have a candidate in front of it.
    constexpr uint8 RarityBit(Rarity r)
    {
        return static_cast<uint8>(1u << static_cast<uint8>(r));
    }

    constexpr uint8 RARITY_MASK_ALL =
        static_cast<uint8>((1u << static_cast<uint8>(Rarity::MAX)) - 1u);

    // A view of the registry the offer builder may draw from. The live one
    // hides MF_NotImplemented entries; the tests use one that does not, so the
    // invariants exercise the whole 73-entry table rather than the four
    // mechanics Phase 0 happens to implement.
    struct RegistryView
    {
        bool includeUnimplemented = false;

        // Gauntlet.Family.<Spawn|Enemy|Tempo|Attrition|Rules|Bargain|Class>
        // .Enable, one bit each in Family order. A family whose bit is clear is
        // never offered.
        //
        // It is in RegistryView rather than a parameter of its own because it
        // is the same kind of thing includeUnimplemented is: a statement about
        // which rows of the table exist as far as this call is concerned. The
        // generator stays deterministic in its inputs and this is one of them,
        // so a realm that turns a family off gets a different run from a realm
        // that does not, reproducibly.
        //
        // Carried affixes are untouched. The conf file has promised since
        // Phase 0 that disabling a family "removes it from future offers;
        // anything a character already carries keeps working", and that is
        // what this does -- the offer builder is the only thing that reads it.
        uint8 familyMask = FAMILY_MASK_ALL;

        bool FamilyAllowed(Family f) const { return (familyMask & FamilyBit(f)) != 0; }
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
        // MECHANIC_NONE.
        //
        // The bit is the one Phase 0 called GR_FellBackToScalar and its value
        // is unchanged; only the name is, because there is no scalar to fall
        // back to any more.
        GR_NoCandidate      = 1u << 2,

        // Set when the "one reward-shaped offer per tier" guarantee (§4.4.5)
        // found nothing to pay it with.
        //
        // This shared GR_NoCandidate's bit for five phases, on the reasoning
        // that it is the same failure -- nothing eligible -- seen from the
        // reward-shaped end. Measured, that turned out to be wrong, and wrong
        // in the direction that mattered: over 240,000 sets, 48.86% carried
        // some relaxation and 15.52 points of that were sets with three real
        // offers, no empty slot and no repeated family, marked only because
        // none of the three was reward-shaped. In the twenties it is most of
        // the number -- tier 21 relaxes 46.67% of the time and 36.50 points of
        // that is this and nothing else.
        //
        // Sharing the bit is what let that read as a pool problem for four
        // phases. It is not one. Ten of sixty-nine rows carry MF_RewardShaped
        // and only four of those are available to every class, so the
        // guarantee stops being satisfiable at the tier a character has
        // carried all four -- which happens in the twenties and has nothing to
        // do with how big the table is.
        GR_NoRewardShaped   = 1u << 3
    };

    struct OfferSet
    {
        std::vector<Offer> offers;
        uint32 relaxations = GR_None;
    };

    // The magnitude a boon of this category is worth on this mechanic at this
    // rank, as a percentage.
    //
    // The offer builder's own table, exposed because it has a second honest
    // caller: anything that wants to *show* what an affix pays without making
    // an offer -- `.gauntlet debug cards`, and the generated affix table in the
    // README. A second copy of these numbers somewhere else would be a second
    // thing that can disagree with what the player is actually paid, which is
    // the fault this redesign exists to remove.
    //
    // Pure: no stream, no state, and calling it does not consume a roll.
    uint32 BoonMagnitude(uint16 mechanic, Boon boon, uint8 rank);

    // The rarity roll of one offer slot: exactly one draw from the stream,
    // weighted by Rules::RarityWeight at this tier over the rarities whose
    // bit is set in `availableMask`. A rarity with no candidate is never
    // returned, however heavy its weight, and when every available rarity
    // weighs zero at this tier the roll is uniform over what is there -- the
    // weights shape the mix, they do not veto a card the registry made
    // eligible. A mask of zero returns Common and consumes nothing; Draw
    // never asks with one.
    //
    // Public for the same reason BoonMagnitude is: it is the one piece of the
    // roll whose shape can be tested directly, and with every card in the
    // table Rare the sweep cannot yet show the weights doing anything.
    Rarity RollRarity(uint64& state, uint8 tier, uint8 availableMask);

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
