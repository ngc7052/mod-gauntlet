/*
 * mod-gauntlet - deterministic offer builder
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletGenerator.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "GauntletTrades.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace
{
    using namespace Gauntlet;
    using Stream::Mix;
    using Stream::RollIn;

    constexpr size_t FAMILY_COUNT = static_cast<size_t>(Family::MAX);

    // ------------------------------------------------------------------
    // Values the design does not give. Every one of them is a decision that
    // belongs to the design, not to the generator, so each is marked and
    // each is listed in the step 3 report.
    // ------------------------------------------------------------------

    // Weight of each family in the offer roll, in Family order. Design §4.4
    // says "roll three families first" but never says with what weights. The
    // shape here is: the three families that carry the run's identity
    // (Enemy, Tempo, and the class curses) are the common draw; Spawn and
    // Attrition sit just below because a run wants one or two of each, not
    // four; Rules were three brief entries that all disappear by tier 6, so
    // they are rare; Bargain is reached mostly through slot C's own bargain
    // roll and does not need a large share of the family roll as well.
    //
    // The rarity plan's commons changed what Rules holds without changing this
    // number, and that is deliberate for now: seven of the first ten commons
    // are denials filed as Rules and three are trades filed as Attrition, so
    // at tier 1 a Common slot draws Attrition's three about three times as
    // often as Rules' seven. Measured, not tuned -- `build/sweep --rarity`
    // shows the mix -- and the plan's step 5 is where the weights get argued.
    constexpr std::array<uint32, FAMILY_COUNT> FAMILY_WEIGHT = {
        3,   // Spawn        TODO(design)
        4,   // Enemy        TODO(design)
        4,   // Tempo        TODO(design)
        3,   // Attrition    TODO(design)
        1,   // Rules        TODO(design)
        2,   // Bargain      TODO(design)
        3    // Class        TODO(design)
    };

    // Design §4.1's counted caps. The exclusivity caps -- one stalker, one
    // role tax, one rule, one class curse, E4 xor E5 -- are already carried by
    // the registry's exclusiveKeys ("stalker", "roletax", "rule", "classcurse",
    // "onkill-positional") and are enforced by the key check below rather than
    // being counted twice here.
    constexpr uint32 CAP_ON_KILL = 2;   // §4.1 "two on-kill mechanics"
    constexpr uint32 CAP_TEMPO   = 2;   // §4.1 "two tempo mechanics"
    constexpr uint32 CAP_BARGAIN = 2;   // §4.1 "two bargains"

    // How many class curses a run may carry.
    //
    // The design gives each class four, "covering four verbs -- a threshold, a
    // shortcut tax, a companion or anchor rule, and an identity rule", and the
    // point of four is that a character can end up shaped by more than one of
    // them. Three leaves room for the fourth to stay a live offer rather than
    // becoming the only thing left to take.
    //
    // Before Phase 4 this was not a cap at all: every class row shared one
    // exclusive key, which made it a limit of one. See the note at the head of
    // GauntletRegistry.cpp.
    constexpr uint32 CAP_CLASS = 3;   // TODO(design)

    // Design §4.6 and the note on Cursed Hoard's row: the bargain family opens
    // at tier 6 whatever a card's own minTier says.
    // Moved to the header so RegistryTest can assert against it. The two
    // places that named a bargain's earliest tier -- this constant and Cursed
    // Hoard's registry row, which said 4 -- disagreed from Phase 0 until Phase
    // 3, with the constant silently winning. A test cannot catch a drift it
    // cannot see the number for.

    // Phase 2 deleted the last four Scalars, so nothing in the table takes a
    // condition any more and the roll that drew one is gone with them. The
    // Condition axis itself stays -- the enum, the column, the names and
    // Mgr::ConditionActive are all still here -- because design §6 keeps it as
    // "the multiplier on variety" for a later phase to use. Every offer this
    // generator builds carries Condition::Always, and Mgr::NameOf prints no
    // adjective for it.

    // TODO(design): the boon magnitudes. §3 states a boon on every card, but
    // most of them -- "Divine Shield cooldown -1 min", "the second life",
    // "Sprint cooldown halved" -- are not one of the seven Boon categories, so
    // there is nothing yet to key a per-mechanic row on. The table is
    // therefore one base per boon category, sized from the numbers §3 does
    // state in the categories it can express (+10% damage, +25% experience),
    // with the per-mechanic switch left in place for the rows that override
    // it. It used to scale linearly by rank; with the ranks gone the base is
    // the number, and it is the first knob the sweep and play should argue
    // with.
    uint32 BoonTable(uint16 mechanic, Boon boon)
    {
        // A common's magnitude is its trade line's, read from the same header
        // the mechanic reads, so the card and the payout cannot disagree.
        if (TradeDef const* trade = FindTrade(mechanic))
            return trade->boon == boon ? trade->boonPct : 0u;

        // Two rows state their own boon on the card, so they do not take the
        // category's magnitude: the number the offer promises has to be the
        // number the mechanic pays, or the card is lying at the one moment the
        // player is reading it.
        switch (mechanic)
        {
            // T2 Frenzy: "+6% damage dealt per stack". The boon is the
            // damage-dealt half, so it is per stack and equal to the curse.
            case 15:
                return boon == Boon::BonusDamage ? 6u : 0u;

            // T5 Hubris. The override used to key on Boon::BonusExperience,
            // back when the card was an experience rule; the redesign moved it
            // to Boon::BonusDamage and left this behind, so the row silently
            // paid a magnitude of zero -- an offer card promising nothing.
            //
            // There is no bespoke number to promise any more: the duel's
            // shelter is the card's own upside and is described in its text, so
            // the boon is an ordinary damage boon and takes the category's
            // ladder. Falling through is the fix.
            case 18:
                break;

            // C2 Berserker's Bargain: "below 35% health you deal 25% more
            // damage". One number, the card's.
            case 29:
                return boon == Boon::BonusDamage ? 25u : 0u;

            // C5 Long Forbearance: "Holy Light 10% cheaper". One number on the
            // card, and Boon::BonusAbility has no category row of its own --
            // the five fixed boons above LastGenericBoon are bespoke by
            // definition, so the per-mechanic override is where their magnitude
            // has to come from when they have one at all.
            case 32:
                return boon == Boon::BonusAbility ? 10u : 0u;

            default:
                break;
        }

        uint32 base = 0;
        switch (boon)
        {
            case Boon::BonusDamage:     base =  8; break;   // TODO(design)
            case Boon::BonusHealing:    base = 10; break;   // TODO(design)
            case Boon::BonusMoveSpeed:  base =  5; break;   // TODO(design)
            case Boon::BonusExperience: base = 15; break;   // TODO(design)
            // Gold is not felt while playing. You notice it at a vendor, an
            // hour later, in a different zone, which makes it the worst
            // available answer to "what am I getting for carrying this curse".
            // Nothing declares it any more; the enum value stays because live
            // characters have it stored in gauntlet_affix, and paying a stored
            // one nothing is better than paying it a number the card no longer
            // describes.
            case Boon::BonusMoney:      return 0;
            case Boon::BonusMaxHealth:  base =  5; break;   // TODO(design)
            case Boon::BonusRegen:      base = 15; break;   // TODO(design)
            default:                    return 0;           // Boon::None
        }

        return base;
    }

    // TODO(design): the relevance discount. §3 prices "a curse that is cheap
    // for this spec" at half its boon, and offers the alternative of not
    // offering it at all. The hard relevance filter below takes the second
    // option for everything that declares a gate, so what is left to price is
    // the class curse that declares none: it is aimed at a class rather than
    // at a build, and nothing the server can read shows that it bites this
    // character. Those pay half. A curse that proves its gate has been shown
    // to bite, and a mechanic that is not class-specific at all has no build
    // to be outside of; both pay full.
    uint32 RelevancePercent(MechanicDef const& def)
    {
        if (def.requiresSpell != 0 || def.requiresTree != 0)
            return 100;
        return def.classMask != 0 ? 50 : 100;
    }

    // ------------------------------------------------------------------
    // Exclusive keys: '|'-separated tokens, shared by no two active mechanics.
    // ------------------------------------------------------------------
    bool KeyListsIntersect(char const* a, char const* b)
    {
        if (!a || !b || !*a || !*b)
            return false;

        std::string_view const left(a);
        std::string_view const right(b);

        for (size_t i = 0; i <= left.size(); )
        {
            size_t const end = std::min(left.find('|', i), left.size());
            std::string_view const token = left.substr(i, end - i);
            if (!token.empty())
            {
                for (size_t j = 0; j <= right.size(); )
                {
                    size_t const rend = std::min(right.find('|', j), right.size());
                    if (token == right.substr(j, rend - j))
                        return true;
                    j = rend + 1;
                }
            }
            i = end + 1;
        }

        return false;
    }

    // ------------------------------------------------------------------
    // What the carried set costs the offer builder. Computed once per call
    // from a vector, so nothing here depends on hash order.
    // ------------------------------------------------------------------
    struct Carried
    {
        std::vector<AffixInstance> const*   set = nullptr;
        std::array<uint32, FAMILY_COUNT>    perFamily = {};
        uint32                              onKill  = 0;
        uint32                              tempo   = 0;
        uint32                              bargain = 0;
        uint32                              klass   = 0;   // `class` is a keyword

        AffixInstance const* Find(uint16 mechanic) const
        {
            for (AffixInstance const& a : *set)
                if (a.mechanic == mechanic)
                    return &a;
            return nullptr;
        }
    };

    Carried Summarise(std::vector<AffixInstance> const& carried)
    {
        Carried out;
        out.set = &carried;

        for (AffixInstance const& a : carried)
        {
            MechanicDef const* def = FindMechanic(a.mechanic);
            if (!def)
                continue;   // a row from a newer generator; it constrains nothing here

            out.perFamily[static_cast<size_t>(def->family)]++;
            if (def->flags & MF_OnKill)
                out.onKill++;
            if (def->family == Family::Tempo)
                out.tempo++;
            if (def->family == Family::Bargain)
                out.bargain++;
            if (def->family == Family::Class)
                out.klass++;
        }

        return out;
    }

    // Does adding `def` to the run break one of §4.1's counted caps?
    bool CapsAllow(MechanicDef const& def, Carried const& carried)
    {
        if ((def.flags & MF_OnKill) && carried.onKill >= CAP_ON_KILL)
            return false;
        if (def.family == Family::Tempo && carried.tempo >= CAP_TEMPO)
            return false;
        if (def.family == Family::Bargain && carried.bargain >= CAP_BARGAIN)
            return false;
        if (def.family == Family::Class && carried.klass >= CAP_CLASS)
            return false;

        return true;
    }

    bool ExclusiveKeysAllow(MechanicDef const& def, Carried const& carried)
    {
        for (AffixInstance const& a : *carried.set)
        {
            if (a.mechanic == def.id)
                continue;   // a mechanic never excludes itself

            MechanicDef const* other = FindMechanic(a.mechanic);
            if (other && KeyListsIntersect(def.exclusiveKeys, other->exclusiveKeys))
                return false;
        }

        return true;
    }

    bool RelevantTo(MechanicDef const& def, IPlayerView const& view)
    {
        if (def.classMask != 0 && (def.classMask & view.GetClassMask()) == 0)
            return false;
        if (def.requiresSpell != 0 && !view.HasSpell(def.requiresSpell))
            return false;
        if (def.requiresTree != 0 && def.requiresTree != view.GetTalentTree())
            return false;
        return true;
    }

    // ------------------------------------------------------------------
    // Pool construction.
    //
    // Nothing here consumes the stream: a slot builds every pool it is allowed
    // to draw from, empty families drop out, and exactly three rolls follow --
    // a weighted one for the rarity, a weighted one for the family, a uniform
    // one inside it. That is a deliberate departure from the plan's
    // "if pool empty: try next family" retry, which would make the number of
    // rolls a slot consumes depend on the shape of the table -- and the whole
    // point of the stream is that the same inputs consume the same rolls in
    // the same order. A family whose pool comes out empty is exactly the
    // plan's "saturated", so no candidate is lost by pre-filtering.
    // ------------------------------------------------------------------
    enum Relax : uint32
    {
        RELAX_STRICT = 0,   // every rule
        RELAX_FAMILY = 1,   // a family another slot already used
        RELAX_MECHANIC = 2, // a mechanic another slot already offered
        RELAX_COUNT = 3
    };

    struct SlotContext
    {
        uint8                            tier = 0;
        IPlayerView const*               view = nullptr;
        Carried const*                   carried = nullptr;
        RegistryView                     reg;
        uint8                            maxCarried = MAX_CARRIED;
        std::array<bool, FAMILY_COUNT>   familyUsed = {};
        std::vector<uint16>              mechanicUsed;

        bool IsFamilyUsed(Family f) const { return familyUsed[static_cast<size_t>(f)]; }

        bool IsMechanicUsed(uint16 id) const
        {
            return std::find(mechanicUsed.begin(), mechanicUsed.end(), id) != mechanicUsed.end();
        }
    };

    bool Eligible(MechanicDef const& def, SlotContext const& ctx, OfferKind kind,
                  uint32 relax, bool rewardShapedOnly)
    {
        if (!ctx.reg.includeUnimplemented && !IsImplemented(def))
            return false;
        // Gauntlet.Family.<X>.Enable. Checked here rather than in BuildPools so
        // that it holds at every relaxation: a switched-off family must not
        // come back as the last resort when nothing else fits. An empty slot is
        // the right answer for a realm that has turned six families off.
        if (!ctx.reg.FamilyAllowed(def.family))
            return false;
        if (rewardShapedOnly && !(def.flags & MF_RewardShaped))
            return false;

        // minTier and maxTier say when a mechanic is appropriate to
        // *introduce*: Carrion is an early-run curse and offering it fresh at
        // tier 70 would be offering a level-1 problem to a level-70 character.
        // Every offer is an introduction now that rank-ups are gone, so the
        // window binds every kind.
        if (ctx.tier < def.minTier || ctx.tier > def.maxTier)
            return false;
        if (def.family == Family::Bargain && ctx.tier < BARGAIN_MIN_TIER)
            return false;
        if (kind == OfferKind::Bargain && def.family != Family::Bargain)
            return false;

        if (!RelevantTo(def, *ctx.view))
            return false;

        // Never offer a carried mechanic again: with the ranks gone there is
        // no "again" to offer, and Mgr::Pick refuses one that slips through.
        if (ctx.carried->Find(def.id))
            return false;

        if (ctx.carried->set && ctx.carried->set->size() >= ctx.maxCarried)
        {
            // The set is full, so nothing new may join it. A Swap is exempt
            // and deliberately so: it is the offer that takes one out before
            // it puts one in, which is the whole reason a cap is a design
            // rather than a wall. Past the cap a tier asks "deepen, or trade",
            // and both are real questions.
            if (kind != OfferKind::Swap)
                return false;
        }

        if (!CapsAllow(def, *ctx.carried))
            return false;
        if (!ExclusiveKeysAllow(def, *ctx.carried))
            return false;

        if (relax < RELAX_FAMILY && ctx.IsFamilyUsed(def.family))
            return false;
        // Never twice in one set, at any relaxation.
        //
        // RELAX_MECHANIC used to allow it as the last resort before giving up,
        // on the reasoning that a filled slot beats an empty one. Played, it
        // does not: a tier 39 offer came back as Cursed Hoard, Last Rites NEW,
        // and Last Rites SWAP-out-Carrion -- and the third is not a choice, it
        // is the second one with a price attached. Nobody takes the swap when
        // the plain version is sitting above it, so the slot only looks like an
        // option. An empty slot is at least honest about having nothing.
        //
        // RELAX_FAMILY stays: two Spawn affixes in one set are two real
        // choices. It is the identical *mechanic* that is dominated.
        if (ctx.IsMechanicUsed(def.id))
            return false;

        return true;
    }

    using Pools = std::array<std::vector<MechanicDef const*>, FAMILY_COUNT>;

    // AllMechanics() is a vector in id order, so the pools are built in a
    // fixed order on every platform and in every process.
    Pools BuildPools(SlotContext const& ctx, OfferKind kind, uint32 relax, bool rewardShapedOnly)
    {
        Pools pools;
        for (MechanicDef const& def : AllMechanics())
            if (Eligible(def, ctx, kind, relax, rewardShapedOnly))
                pools[static_cast<size_t>(def.family)].push_back(&def);
        return pools;
    }

    bool AnyPool(Pools const& pools)
    {
        for (auto const& pool : pools)
            if (!pool.empty())
                return true;
        return false;
    }

    // Every rarity with at least one candidate anywhere in the pools, as the
    // mask RollRarity takes.
    uint8 AvailableRarities(Pools const& pools)
    {
        uint8 mask = 0;
        for (auto const& pool : pools)
            for (MechanicDef const* def : pool)
                mask |= RarityBit(def->rarity);
        return mask;
    }

    // Rarity first, then family, then the card: one weighted roll for the
    // rarity over the rarities that have a candidate, one weighted roll over
    // the families that hold a card of that rarity, then one uniform roll
    // inside that family among its cards of that rarity.
    //
    // Rarity outranks family on purpose. Design section 2.4 says "roll three
    // families first", and until the rarity plan that was the first roll; but
    // rarity is now the axis a run escalates along (docs/rarity-plan.md section
    // 2), and a roll that picked a family first would let the family's
    // contents skew the mix the tier weights are there to set. Distinct
    // families are still a rule -- familyUsed and the relaxation ladder hold
    // it -- so nothing about the set's shape is given up; only the order the
    // dice fall in.
    //
    // With every card in the table Rare, the rarity roll has one candidate and
    // the draw is what it was before the roll existed, bar the stream having
    // advanced once. That is the property step 1 of the plan is built on.
    MechanicDef const* Draw(uint64& state, Pools const& pools, uint8 tier)
    {
        uint8 const available = AvailableRarities(pools);
        if (available == 0)
            return nullptr;

        Rarity const rarity = RollRarity(state, tier, available);

        // The family weights, over the families holding a card of that rarity.
        std::array<uint32, FAMILY_COUNT> ofRarity = {};
        uint32 total = 0;
        for (size_t f = 0; f < FAMILY_COUNT; ++f)
        {
            for (MechanicDef const* def : pools[f])
                if (def->rarity == rarity)
                    ofRarity[f]++;
            if (ofRarity[f] != 0)
                total += FAMILY_WEIGHT[f];
        }

        if (total == 0)
            return nullptr;   // unreachable: the rarity was drawn from these pools

        uint32 roll = RollIn(state, 0, total - 1);
        size_t chosen = FAMILY_COUNT;
        for (size_t f = 0; f < FAMILY_COUNT; ++f)
        {
            if (ofRarity[f] == 0)
                continue;
            if (roll < FAMILY_WEIGHT[f])
            {
                chosen = f;
                break;
            }
            roll -= FAMILY_WEIGHT[f];
        }

        if (chosen == FAMILY_COUNT)
            return nullptr;   // unreachable while the weights are all non-zero

        // Uniform among the chosen family's cards of the chosen rarity, in
        // table order, which is id order on every platform.
        uint32 index = RollIn(state, 0, ofRarity[chosen] - 1);
        for (MechanicDef const* def : pools[chosen])
            if (def->rarity == rarity && index-- == 0)
                return def;

        return nullptr;   // unreachable: ofRarity counted these
    }

    Offer MakeOffer(uint64& state, MechanicDef const& def, SlotContext const& ctx, OfferKind kind)
    {
        Offer offer;
        offer.mechanic  = def.id;
        offer.kind      = kind;
        // Every mechanic's boon is named by its registry row and delivered by
        // the mechanic itself; the condition axis is unused since the scalars
        // were deleted. Neither consumes the stream any more, so an offer's
        // roll order is rarity, family, mechanic and -- for a swap -- the slot
        // it replaces.
        offer.condition = Condition::Always;
        offer.boon      = def.boon;

        // The card's own rarity, not the one the slot rolled. The roll decides
        // which rarity to draw *from*, and RollRarity only ever answers with
        // one that has a candidate, so the two agree whenever the roll was
        // honoured; when it could not be -- nothing of that rarity eligible --
        // the card that was drawn is still what the player is being offered,
        // and that is what the card must say.
        offer.rarity    = def.rarity;

        uint32 const mag = BoonTable(def.id, offer.boon) * RelevancePercent(def) / 100u;
        offer.boonMag = static_cast<uint8>(std::min<uint32>(mag, 255u));

        if (kind == OfferKind::Swap && !ctx.carried->set->empty())
        {
            // The player does not choose what to discard; the offer names it.
            auto const& set = *ctx.carried->set;
            offer.swapSlot = set[RollIn(state, 0, static_cast<uint32>(set.size()) - 1)].slot;
        }

        return offer;
    }

    // GR_RepeatedFamily and GR_RepeatedMechanic describe the finished set, so
    // they are read off it rather than accumulated while it is built: a slot
    // that repeated a family and was then replaced by the reward-shaped
    // guarantee must not leave its bit behind.
    uint32 Repeats(std::vector<Offer> const& offers)
    {
        uint32 relaxations = GR_None;
        std::array<uint32, FAMILY_COUNT> families = {};
        std::vector<uint16> seen;

        for (Offer const& offer : offers)
        {
            if (offer.mechanic == MECHANIC_NONE)
                continue;

            if (std::find(seen.begin(), seen.end(), offer.mechanic) != seen.end())
                relaxations |= GR_RepeatedMechanic;
            seen.push_back(offer.mechanic);

            if (MechanicDef const* def = FindMechanic(offer.mechanic))
                if (families[static_cast<size_t>(def->family)]++ > 0)
                    relaxations |= GR_RepeatedFamily;
        }

        return relaxations;
    }

    void Remember(SlotContext& ctx, Offer const& offer)
    {
        if (MechanicDef const* def = FindMechanic(offer.mechanic))
            ctx.familyUsed[static_cast<size_t>(def->family)] = true;

        ctx.mechanicUsed.push_back(offer.mechanic);
    }

    // Walk the relaxation ladder until some family has a pool. Returns the
    // step that produced it, or RELAX_COUNT when nothing did.
    uint32 FirstUsableStep(SlotContext const& ctx, OfferKind kind, bool rewardShapedOnly, Pools& out)
    {
        for (uint32 relax = RELAX_STRICT; relax < RELAX_COUNT; ++relax)
        {
            Pools pools = BuildPools(ctx, kind, relax, rewardShapedOnly);
            if (AnyPool(pools))
            {
                out = std::move(pools);
                return relax;
            }
        }
        return RELAX_COUNT;
    }
}

namespace Gauntlet
{
    // The anonymous namespace's table, published under a name callers outside
    // the offer builder can use. One line rather than a second copy.
    uint32 BoonMagnitude(uint16 mechanic, Boon boon)
    {
        return BoonTable(mechanic, boon);
    }

    Rarity RollRarity(uint64& state, uint8 tier, uint8 availableMask)
    {
        uint32 total = 0;
        for (size_t r = 0; r < Rules::RARITY_COUNT; ++r)
            if (availableMask & RarityBit(static_cast<Rarity>(r)))
                total += Rules::RarityWeight(tier, static_cast<Rarity>(r));

        // Every available rarity weighs zero in this band -- a tier-5 slot
        // whose only eligible cards are epics, say. The weights shape the mix;
        // they do not veto a card the registry made eligible at this tier, so
        // the roll goes uniform over what is there rather than emptying the
        // slot. Still one draw from the stream, so the roll count of a slot
        // does not depend on the table.
        bool const uniform = total == 0;
        if (uniform)
            for (size_t r = 0; r < Rules::RARITY_COUNT; ++r)
                if (availableMask & RarityBit(static_cast<Rarity>(r)))
                    ++total;

        if (total == 0)
            return Rarity::Common;   // nothing available; Draw never asks this

        uint32 roll = RollIn(state, 0, total - 1);
        for (size_t r = 0; r < Rules::RARITY_COUNT; ++r)
        {
            Rarity const rarity = static_cast<Rarity>(r);
            if (!(availableMask & RarityBit(rarity)))
                continue;

            // A zero weight never satisfies roll < 0, so a rarity the band
            // gives nothing to is skipped here without a special case.
            uint32 const weight = uniform ? 1u : Rules::RarityWeight(tier, rarity);
            if (roll < weight)
                return rarity;
            roll -= weight;
        }

        return Rarity::Common;   // unreachable: the weights sum to `total`
    }

    OfferSet BuildOffers(uint32 seed, uint8 tier, IPlayerView const& view,
                         std::vector<AffixInstance> const& carried,
                         uint32 count, RegistryView reg, uint8 maxCarried, uint8 rerolls)
    {
        OfferSet result;
        if (count == 0)
            return result;

        // Plan §2.4. GeneratorVersion is in the seed, so a bump moves every
        // offer in the game and no old run is touched: picks live in columns.
        // The reroll count takes bits 16-23 -- the seed owns 32 and up, the
        // tier 8-15, the version 0-7 -- so no count below 256 can be mistaken
        // for either neighbour, and zero folds to nothing at all.
        uint64 state = Mix((static_cast<uint64>(seed) << 32)
                         ^ (static_cast<uint64>(rerolls) << 16)
                         ^ (static_cast<uint64>(tier) << 8)
                         ^ static_cast<uint64>(GeneratorVersion));

        Carried const summary = Summarise(carried);

        SlotContext ctx;
        ctx.tier    = tier;
        ctx.view    = &view;
        ctx.carried = &summary;
        ctx.reg         = reg;
        ctx.maxCarried  = maxCarried;

        // Slot kinds, decided before any family is drawn. The bargain roll is
        // consumed only on a tier that is not a swap tier, exactly as the
        // plan's expression short-circuits; tier is folded into the stream
        // seed, so no two tiers share a stream and the skipped roll cannot
        // shift another tier's offers.
        //
        // Slot A used to become a rank-up from tier 9. With the ranks gone
        // every slot is an introduction, and a run escalates by what it is
        // offered -- rarer cards later, through the rarity roll in Draw --
        // rather than by buying the same card again.
        std::vector<OfferKind> kinds(count, OfferKind::New);
        // Levels 20, 40 and 60, which is where tiers 4, 8 and 12 used to fall.
        //
        // They are no longer the only place a swap happens. With one tier per
        // level and a carried set that fills up, every offer past the cap is a
        // swap -- so the fixed swap tiers are now the three where a swap is
        // *guaranteed* rather than the three where it is possible.
        bool const swapTier = tier == 20 || tier == 40 || tier == 60;

        // A full carried set changes what a tier asks. Nothing new may join it,
        // so slot C becomes the trade -- "give one up for one" -- and the
        // bargain roll is not consumed, because a bargain is a new mechanic and
        // there is no room for one.
        //
        // Without this the cap was a wall rather than a design: slot C stayed
        // New, found nothing, and the whole offer set came back empty --
        // measured at 100% relaxed with every slot empty from tier 55 to 80,
        // which is thirty levels of a run being asked nothing at all.
        bool const full = carried.size() >= maxCarried;

        if (count >= 3)
            kinds[2] = (swapTier || full)
                     ? OfferKind::Swap
                     : (RollIn(state, 0, 5) == 0 ? OfferKind::Bargain : OfferKind::New);

        for (uint32 slot = 0; slot < count; ++slot)
        {
            OfferKind kind = kinds[slot];

            Pools pools;
            uint32 step = FirstUsableStep(ctx, kind, false, pools);

            // A kind the table cannot satisfy degrades rather than blocking the
            // slot: a bargain before the family opens becomes a new mechanic,
            // and a slot with nothing new to offer becomes a swap once the set
            // is full. The offer says which it is, so nothing is hidden from
            // the player.
            // A bargain is a bonus, never a requirement, and it must not be
            // bought with the offer set's distinct-family guarantee.
            //
            // Degrading only on outright failure was not enough once the family
            // actually had implementations. Family B has exactly two rows, only
            // one of them in window below tier 8, and CAP_BARGAIN retires both
            // for the rest of the run -- so a slot that asks for a bargain
            // one time in six very often finds one it can place only by
            // relaxing a rule, and took it. Measured: turning the bargain slot
            // on moved tiers 6-14 from an exact zero to 3-7% relaxed, and every
            // point of that was this slot.
            //
            // So the test is not "can a bargain be placed at all" but "can it
            // be placed without giving anything up". If not, and an ordinary
            // new mechanic can, the ordinary one wins.
            if (kind == OfferKind::Bargain && step > RELAX_STRICT)
            {
                Pools newPools;
                if (FirstUsableStep(ctx, OfferKind::New, false, newPools) == RELAX_STRICT)
                {
                    kind  = OfferKind::New;
                    pools = std::move(newPools);
                    step  = RELAX_STRICT;
                }
                else if (step == RELAX_COUNT)
                {
                    kind = OfferKind::New;
                    step = FirstUsableStep(ctx, kind, false, pools);
                }
            }
            // Last resort, and it only exists once the set is full: a slot with
            // no new mechanic to offer can still offer a trade. This is what
            // keeps the late run from going quiet -- a character carrying
            // sixteen affixes has nothing to add and everything still to
            // choose between, and reroll and skip are the other two answers.
            if (step == RELAX_COUNT && kind != OfferKind::Swap && full)
            {
                Pools swapPools;
                if (FirstUsableStep(ctx, OfferKind::Swap, false, swapPools) != RELAX_COUNT)
                {
                    kind  = OfferKind::Swap;
                    pools = std::move(swapPools);
                    step  = RELAX_STRICT;
                }
            }

            MechanicDef const* def = step != RELAX_COUNT ? Draw(state, pools, tier) : nullptr;

            if (!def)
            {
                // Plan §2.4's last line was "if all empty: fall back to a
                // Scalar". Phase 2 deleted the scalars, so there is no pool of
                // last resort left and nothing to fall back to: every mechanic
                // in the table is now a named thing that does something, and
                // there is no such thing as a generic filler affix to invent
                // one from.
                //
                // An empty offer is what is left, and it is the honest answer.
                // Mgr::OfferTier prints it as "Nothing - no affix is available
                // to you at this tier" rather than as a numbered line the
                // player cannot take, and Phase 0 already made MECHANIC_NONE
                // mean exactly that everywhere.
                result.relaxations |= GR_NoCandidate;
                result.offers.push_back(Offer());
                continue;
            }

            Offer offer = MakeOffer(state, *def, ctx, kind);
            Remember(ctx, offer);
            result.offers.push_back(offer);
        }

        // "Guarantee at least one reward-shaped offer per tier" (§4.4.5). Slot
        // B is the one that gives way, because slot A is the run's escalation
        // and slot C is the swap or the bargain.
        bool rewardShaped = false;
        for (Offer const& offer : result.offers)
        {
            MechanicDef const* def = FindMechanic(offer.mechanic);
            if (def && (def->flags & MF_RewardShaped))
            {
                rewardShaped = true;
                break;
            }
        }

        if (!rewardShaped && count >= 2)
        {
            // Rebuild slot B's context without slot B in it, so the
            // replacement is measured against its siblings, not against the
            // pick it is replacing.
            SlotContext replace;
            replace.tier       = tier;
            replace.view       = &view;
            replace.carried    = &summary;
            replace.reg        = reg;
            // Carried over from the caller, not left at MAX_CARRIED.
            //
            // It was left at the default for five phases, and the default is
            // the shipped ceiling -- so on a realm with Gauntlet.MaxAffixes
            // below 16 this path was the one place a New offer could be made
            // to a run that the slot loop above had already decided was full.
            // A player on such a realm would have been offered a seventeenth
            // affix exactly when the tier had no reward-shaped offer to give,
            // which is a hard bug to ever see and a trivial one to prevent.
            replace.maxCarried = maxCarried;
            for (size_t i = 0; i < result.offers.size(); ++i)
                if (i != 1)
                    Remember(replace, result.offers[i]);

            // A swap once the set is full, because nothing New can join a full
            // set and the guarantee is about the *offer*: a swap that brings a
            // reward-shaped card in is one. Until the ranks went, a
            // reward-shaped rank-up of something carried paid the guarantee
            // here; now the cap is reached in the low twenties and the late
            // run is trades, so the trade is what has to pay it.
            OfferKind const kind = full ? OfferKind::Swap : OfferKind::New;
            Pools           pools;
            uint32 const    step = FirstUsableStep(replace, kind, true, pools);

            // Once a run carries every reward-shaped card its class can be
            // offered, the guarantee genuinely runs out of table. The invariant
            // sweep's ceiling on how often that happens is where that shows.
            if (step == RELAX_COUNT)
                pools = Pools();

            if (AnyPool(pools))
            {
                if (MechanicDef const* def = Draw(state, pools, tier))
                    result.offers[1] = MakeOffer(state, *def, replace, kind);
            }
            else
            {
                // No reward-shaped mechanic is eligible at all. On its own bit
                // since Phase 5: see the note on GR_NoRewardShaped for the
                // measurement that separated it from GR_NoCandidate, and for
                // why the two being one bit hid the diagnosis for four phases.
                result.relaxations |= GR_NoRewardShaped;
            }
        }

        result.relaxations |= Repeats(result.offers);
        return result;
    }
}
