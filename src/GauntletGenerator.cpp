/*
 * mod-gauntlet - deterministic offer builder
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletGenerator.h"
#include "GauntletRegistry.h"

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
    // four; Rules are three one-rank entries that all disappear by tier 6, so
    // they are rare; Bargain is reached mostly through slot C's own bargain
    // roll and does not need a large share of the family roll as well.
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

    // Design §4.6 and the note on Cursed Hoard's row: the bargain family opens
    // at tier 6 whatever a card's own minTier says.
    constexpr uint8 BARGAIN_MIN_TIER = 6;

    // The conditions a scalar may be offered with.
    //
    // Never Always or InCombat: design §5 keeps damage-taken only "with a state
    // condition", because a threshold is a decision and a flat coefficient is
    // weather. Never VersusElites either, and that one is a Phase 0 limit
    // rather than a design position -- the attacker is not in the ambient stat
    // callback, so the condition cannot be evaluated until the reworked
    // scalars land in Phase 2. Put it back then.
    constexpr std::array<Condition, 13> SCALAR_CONDITIONS = {
        Condition::OutOfCombat,
        Condition::BelowHalfHealth,
        Condition::AboveHalfHealth,
        Condition::WhileSolo,
        Condition::WhileGrouped,
        Condition::InDungeon,
        Condition::InOpenWorld,
        Condition::VersusPlayers,
        Condition::AtNight,
        Condition::AtDay,
        Condition::WhileMoving,
        Condition::WhileStationary,
        Condition::WhileMounted
    };

    // The rank a mechanic arrives at when it is offered as new.
    //
    // TODO(design): §4.6 gives the shape -- "rank-ups dominate from tier 11",
    // "escalate what exists rather than adding verbs" -- but no floor. One
    // rank every six tiers means a mechanic first taken at tier 15 arrives at
    // III instead of being a rank-I curiosity in a run where everything else
    // is III, while I and II stay reachable for two thirds of the run.
    uint8 RankFloor(uint8 tier)
    {
        uint32 const floor = 1u + (tier > 0 ? (tier - 1u) / 6u : 0u);
        return static_cast<uint8>(std::min<uint32>(floor, MAX_RANK));
    }

    // TODO(design): the boon magnitudes. §3 states a boon on every card, but
    // most of them -- "Divine Shield cooldown -1 min", "the second life",
    // "Sprint cooldown halved" -- are not one of the seven Boon categories, so
    // there is nothing yet to key a per-mechanic row on. The table is
    // therefore a base per boon category scaled linearly by rank, sized from
    // the numbers §3 does state in the categories it can express (+10% damage,
    // +25% experience, +50% money), with the per-mechanic switch left in place
    // for the rows that will want to override it.
    uint32 BoonTable(uint16 mechanic, Boon boon, uint8 rank)
    {
        switch (mechanic)
        {
            default:
                break;   // no mechanic overrides its category yet
        }

        uint32 base = 0;
        switch (boon)
        {
            case Boon::BonusDamage:     base =  8; break;   // TODO(design)
            case Boon::BonusHealing:    base = 10; break;   // TODO(design)
            case Boon::BonusMoveSpeed:  base =  5; break;   // TODO(design)
            case Boon::BonusExperience: base = 15; break;   // TODO(design)
            case Boon::BonusMoney:      base = 25; break;   // TODO(design)
            case Boon::BonusMaxHealth:  base =  5; break;   // TODO(design)
            case Boon::BonusRegen:      base = 15; break;   // TODO(design)
            default:                    return 0;           // Boon::None
        }

        return base * std::max<uint32>(1u, rank);
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
        }

        return out;
    }

    // Does adding `def` to the run break one of §4.1's counted caps? A rank-up
    // is exempt: the mechanic is already counted.
    bool CapsAllow(MechanicDef const& def, Carried const& carried, bool rankUp)
    {
        if (rankUp)
            return true;

        if ((def.flags & MF_OnKill) && carried.onKill >= CAP_ON_KILL)
            return false;
        if (def.family == Family::Tempo && carried.tempo >= CAP_TEMPO)
            return false;
        if (def.family == Family::Bargain && carried.bargain >= CAP_BARGAIN)
            return false;

        return true;
    }

    bool ExclusiveKeysAllow(MechanicDef const& def, Carried const& carried)
    {
        for (AffixInstance const& a : *carried.set)
        {
            if (a.mechanic == def.id)
                continue;   // the mechanic being ranked up never excludes itself

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
    // to draw from, empty families drop out, and exactly one weighted roll and
    // one uniform roll follow. That is a deliberate departure from the plan's
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
        std::array<bool, FAMILY_COUNT>   familyUsed = {};
        std::vector<uint16>              mechanicUsed;
        // Conditions already offered per mechanic, so a repeated mechanic can
        // at least be repeated as a different decision.
        std::vector<std::pair<uint16, Condition>> conditionUsed;

        bool IsFamilyUsed(Family f) const { return familyUsed[static_cast<size_t>(f)]; }

        bool IsMechanicUsed(uint16 id) const
        {
            return std::find(mechanicUsed.begin(), mechanicUsed.end(), id) != mechanicUsed.end();
        }

        bool IsConditionUsed(uint16 id, Condition c) const
        {
            for (auto const& used : conditionUsed)
                if (used.first == id && used.second == c)
                    return true;
            return false;
        }
    };

    bool Eligible(MechanicDef const& def, SlotContext const& ctx, OfferKind kind,
                  uint32 relax, bool rewardShapedOnly)
    {
        if (!ctx.reg.includeUnimplemented && !IsImplemented(def))
            return false;
        if (rewardShapedOnly && !(def.flags & MF_RewardShaped))
            return false;

        if (ctx.tier < def.minTier || ctx.tier > def.maxTier)
            return false;
        if (def.family == Family::Bargain && ctx.tier < BARGAIN_MIN_TIER)
            return false;
        if (kind == OfferKind::Bargain && def.family != Family::Bargain)
            return false;

        if (!RelevantTo(def, *ctx.view))
            return false;

        AffixInstance const* held = ctx.carried->Find(def.id);
        bool const rankUp = kind == OfferKind::RankUp;

        if (rankUp)
        {
            if (!held)
                return false;
            if (held->rank >= def.maxRank || held->rank >= MAX_RANK)
                return false;
        }
        else if (held)
        {
            return false;   // never offer a carried mechanic as new
        }

        if (!CapsAllow(def, *ctx.carried, rankUp))
            return false;
        if (!ExclusiveKeysAllow(def, *ctx.carried))
            return false;

        if (relax < RELAX_FAMILY && ctx.IsFamilyUsed(def.family))
            return false;
        if (relax < RELAX_MECHANIC && ctx.IsMechanicUsed(def.id))
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

    // One weighted roll over the families that have a pool, then one uniform
    // roll inside it.
    MechanicDef const* Draw(uint64& state, Pools const& pools)
    {
        uint32 total = 0;
        for (size_t f = 0; f < FAMILY_COUNT; ++f)
            if (!pools[f].empty())
                total += FAMILY_WEIGHT[f];

        if (total == 0)
            return nullptr;

        uint32 roll = RollIn(state, 0, total - 1);
        size_t chosen = FAMILY_COUNT;
        for (size_t f = 0; f < FAMILY_COUNT; ++f)
        {
            if (pools[f].empty())
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

        auto const& pool = pools[chosen];
        return pool[RollIn(state, 0, static_cast<uint32>(pool.size()) - 1)];
    }

    // The scalar pool of last resort: every scalar the player could carry,
    // with the structural rules -- family, no repeats, not already carried --
    // all dropped. Plan §2.4's "if all empty: fall back to a Scalar".
    std::vector<MechanicDef const*> ScalarPool(SlotContext const& ctx)
    {
        std::vector<MechanicDef const*> pool;
        for (MechanicDef const& def : AllMechanics())
        {
            if (!(def.flags & MF_Scalar))
                continue;
            if (!ctx.reg.includeUnimplemented && !IsImplemented(def))
                continue;
            if (ctx.tier < def.minTier || ctx.tier > def.maxTier)
                continue;
            if (!RelevantTo(def, *ctx.view))
                continue;
            pool.push_back(&def);
        }
        return pool;
    }

    // The condition roll. A scalar takes one from the state axis; everything
    // else is unconditional and takes none, so the stream is only consumed
    // where the design says there is a decision to make.
    Condition RollCondition(uint64& state, MechanicDef const& def, SlotContext const& ctx)
    {
        if (!(def.flags & MF_Scalar))
            return Condition::Always;

        // Prefer a condition this mechanic is not already offered or carried
        // with: when the pool is too small to avoid repeating a mechanic, two
        // different thresholds are still two different decisions, which the
        // same line printed twice is not.
        std::vector<Condition> pool;
        for (Condition c : SCALAR_CONDITIONS)
        {
            if (ctx.IsConditionUsed(def.id, c))
                continue;

            bool carriedWith = false;
            for (AffixInstance const& a : *ctx.carried->set)
                if (a.mechanic == def.id && a.condition == c)
                    carriedWith = true;

            if (!carriedWith)
                pool.push_back(c);
        }

        if (pool.empty())
            pool.assign(SCALAR_CONDITIONS.begin(), SCALAR_CONDITIONS.end());

        return pool[RollIn(state, 0, static_cast<uint32>(pool.size()) - 1)];
    }

    // A mechanic's boon type is fixed by the registry. The two scalars carry
    // Boon::None there on purpose -- design §5 pairs a standalone scalar with
    // a boon but does not say which -- so theirs comes from the stream.
    //
    // Unlike LegacyRoll, this may follow Boon::MAX: an offer is rebuilt from
    // the seed every time it is shown and is never stored, and the contract on
    // GeneratorVersion is that it is bumped whenever the table changes.
    Boon RollBoon(uint64& state, MechanicDef const& def)
    {
        if (def.boon != Boon::None || !(def.flags & MF_Scalar))
            return def.boon;

        // LastRolledBoon, not Boon::MAX: the enum grew in Phase 1 with values a
        // mechanic names for itself -- a cooldown reduction, a second life -- and
        // none of them has a magnitude table, an aggregate kind or an
        // implementation. Rolling one onto a Scalar would name an affix for an
        // upside it does not have.
        return static_cast<Boon>(RollIn(state, 1, static_cast<uint32>(LastRolledBoon)));
    }

    Offer MakeOffer(uint64& state, MechanicDef const& def, SlotContext const& ctx, OfferKind kind)
    {
        Offer offer;
        offer.mechanic  = def.id;
        offer.kind      = kind;
        offer.condition = RollCondition(state, def, ctx);
        offer.boon      = RollBoon(state, def);

        AffixInstance const* held = ctx.carried->Find(def.id);
        if (kind == OfferKind::RankUp && held)
            offer.rank = static_cast<uint8>(held->rank + 1);
        else
            offer.rank = RankFloor(ctx.tier);

        offer.rank = static_cast<uint8>(std::min<uint32>(offer.rank, std::min<uint32>(def.maxRank, MAX_RANK)));
        offer.rank = static_cast<uint8>(std::max<uint32>(1u, offer.rank));

        uint32 const mag = BoonTable(def.id, offer.boon, offer.rank) * RelevancePercent(def) / 100u;
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
        ctx.conditionUsed.emplace_back(offer.mechanic, offer.condition);
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
    OfferSet BuildOffers(uint32 seed, uint8 tier, IPlayerView const& view,
                         std::vector<AffixInstance> const& carried,
                         uint32 count, RegistryView reg)
    {
        OfferSet result;
        if (count == 0)
            return result;

        // Plan §2.4. GeneratorVersion is in the seed, so a bump moves every
        // offer in the game and no old run is touched: picks live in columns.
        uint64 state = Mix((static_cast<uint64>(seed) << 32)
                         ^ (static_cast<uint64>(tier) << 8)
                         ^ static_cast<uint64>(GeneratorVersion));

        Carried const summary = Summarise(carried);

        SlotContext ctx;
        ctx.tier    = tier;
        ctx.view    = &view;
        ctx.carried = &summary;
        ctx.reg     = reg;

        // Slot kinds, decided before any family is drawn. The bargain roll is
        // consumed only on a tier that is not a swap tier, exactly as the
        // plan's expression short-circuits; tier is folded into the stream
        // seed, so no two tiers share a stream and the skipped roll cannot
        // shift another tier's offers.
        std::vector<OfferKind> kinds(count, OfferKind::New);
        bool const swapTier = tier == 4 || tier == 8 || tier == 12;

        if (count >= 1 && (carried.size() >= 3 || tier >= 9))
            kinds[0] = OfferKind::RankUp;
        if (count >= 3)
            kinds[2] = swapTier ? OfferKind::Swap
                                : (RollIn(state, 0, 5) == 0 ? OfferKind::Bargain : OfferKind::New);

        for (uint32 slot = 0; slot < count; ++slot)
        {
            OfferKind kind = kinds[slot];

            Pools pools;
            uint32 step = FirstUsableStep(ctx, kind, false, pools);

            // A kind the table cannot satisfy degrades rather than blocking the
            // slot: a bargain before the family opens becomes a new mechanic, a
            // rank-up with nothing to raise becomes new, and a slot with no new
            // mechanic left to offer becomes a rank-up. The offer says which it
            // is, so nothing is hidden from the player.
            if (step == RELAX_COUNT && kind == OfferKind::Bargain)
            {
                kind = OfferKind::New;
                step = FirstUsableStep(ctx, kind, false, pools);
            }
            if (step == RELAX_COUNT && kind == OfferKind::RankUp)
            {
                kind = OfferKind::New;
                step = FirstUsableStep(ctx, kind, false, pools);
            }
            if (step == RELAX_COUNT && (kind == OfferKind::New || kind == OfferKind::Swap))
            {
                OfferKind const fallback = OfferKind::RankUp;
                Pools rankPools;
                if (FirstUsableStep(ctx, fallback, false, rankPools) != RELAX_COUNT)
                {
                    kind  = fallback;
                    pools = std::move(rankPools);
                    step  = RELAX_STRICT;
                }
            }

            MechanicDef const* def = step != RELAX_COUNT ? Draw(state, pools) : nullptr;

            if (!def)
            {
                std::vector<MechanicDef const*> const scalars = ScalarPool(ctx);
                if (scalars.empty())
                {
                    // Nothing in the table fits this character at this tier.
                    // An empty offer is honest; an invented one is not.
                    result.relaxations |= GR_FellBackToScalar;
                    result.offers.push_back(Offer());
                    continue;
                }

                result.relaxations |= GR_FellBackToScalar;
                def = scalars[RollIn(state, 0, static_cast<uint32>(scalars.size()) - 1)];

                // The scalar pool ignores the carried set, so a fallback can
                // land on something the player already holds. Offer it as the
                // rank-up it really is rather than as a new mechanic.
                AffixInstance const* held = summary.Find(def->id);
                if (held && kind != OfferKind::RankUp && held->rank < std::min<uint8>(def->maxRank, MAX_RANK))
                    kind = OfferKind::RankUp;
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
            replace.tier    = tier;
            replace.view    = &view;
            replace.carried = &summary;
            replace.reg     = reg;
            for (size_t i = 0; i < result.offers.size(); ++i)
                if (i != 1)
                    Remember(replace, result.offers[i]);

            OfferKind kind = OfferKind::New;
            Pools pools;
            if (FirstUsableStep(replace, kind, true, pools) == RELAX_COUNT)
            {
                // Nothing new is reward-shaped, but something carried may be
                // one rank short of its ceiling; that still pays the tier its
                // reward-shaped offer.
                kind = OfferKind::RankUp;
                if (FirstUsableStep(replace, kind, true, pools) == RELAX_COUNT)
                    pools = Pools();
            }

            if (AnyPool(pools))
            {
                if (MechanicDef const* def = Draw(state, pools))
                    result.offers[1] = MakeOffer(state, *def, replace, kind);
            }
            else
            {
                // No reward-shaped mechanic is eligible at all, which is Phase
                // 0's normal state: the two mechanics the module implements are
                // both plain scalars. Recorded on the scalar-fallback bit; see
                // the note on GR_FellBackToScalar.
                result.relaxations |= GR_FellBackToScalar;
            }
        }

        result.relaxations |= Repeats(result.offers);
        return result;
    }
}
