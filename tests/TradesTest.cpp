/*
 * mod-gauntlet - the commons' table, and the shape every line of it must keep
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

// A common is a table row, not a file (docs/rarity-plan.md section 3), and
// the table is the whole of what a common is: the mechanic reads its line,
// the generator reads its boon, the registry names it. So the failures worth
// guarding are the ones where the three disagree, or a line is not a trade at
// all -- a common that takes and pays on the same axis is a wash, and one that
// pays nothing is the flat tax this redesign exists to remove.

#include "GauntletTrades.h"
#include "GauntletGenerator.h"
#include "GauntletRegistry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <set>

namespace
{
    using namespace Gauntlet;

    // Which product each generic boon pays through, mirroring Boons.cpp's Pays()
    // and BoonHealMult; MAX for a boon the aggregate never sees.
    AggregateKind PaidThrough(Boon boon)
    {
        switch (boon)
        {
            case Boon::BonusDamage:     return AggregateKind::DamageDone;
            case Boon::BonusExperience: return AggregateKind::Experience;
            case Boon::BonusMaxHealth:  return AggregateKind::MaxHealth;
            case Boon::BonusHealing:    return AggregateKind::HealTaken;
            default:                    return AggregateKind::MAX;
        }
    }
}

TEST(Trades, EveryLineBacksATradeRow)
{
    for (TradeDef const& t : TRADES)
    {
        MechanicDef const* def = FindMechanic(t.id);
        ASSERT_NE(def, nullptr) << "trade line " << t.id << " has no registry row";
        EXPECT_TRUE(def->rarity == Rarity::Common || def->rarity == Rarity::Uncommon)
            << def->key << " has a trade line but is " << RarityName(def->rarity)
            << "; a line is a common or an uncommon, nothing rarer is one number";
        EXPECT_EQ(def->boon, t.boon)
            << def->key << ": the registry names one boon and the trade line pays another";
        EXPECT_TRUE(IsImplemented(*def)) << def->key;
    }
}

TEST(Trades, EveryCommonRowHasALine)
{
    // The other direction, and the one that matters more: a Rarity::Common row
    // with no line here is offered to a live character and does nothing, and
    // MakeTrade's null return is what it would do instead of crashing.
    for (MechanicDef const& def : AllMechanics())
        if (def.rarity == Rarity::Common)
            EXPECT_NE(FindTrade(def.id), nullptr)
                << def.key << " (id " << def.id << ") is a common with no trade line";
}

TEST(Trades, AnUncommonIsATradeWithACondition)
{
    // docs/rarity-plan.md section 2: a common is "lose X, gain Y" and an
    // uncommon "lose X while Y, gain Z". The condition is the whole of what
    // separates the two tiers, so a line's condition and its row's rarity have
    // to agree: a conditional common is an uncommon drawn at common weight, and
    // an unconditional uncommon a common wearing green.
    for (TradeDef const& t : TRADES)
    {
        MechanicDef const* def = FindMechanic(t.id);
        ASSERT_NE(def, nullptr) << "trade line " << t.id;
        EXPECT_EQ(def->rarity == Rarity::Uncommon, t.condition != Condition::Always)
            << def->key << " is " << RarityName(def->rarity) << " with condition "
            << static_cast<uint32>(t.condition);
        EXPECT_LT(static_cast<uint8>(t.condition), static_cast<uint8>(Condition::MAX)) << def->key;
    }

    // And the tier exists on the table, or the claim above is about nothing.
    EXPECT_TRUE(std::any_of(std::begin(TRADES), std::end(TRADES),
                            [](TradeDef const& t) { return t.condition != Condition::Always; }))
        << "no trade line carries a condition; the uncommon shape is untested";
}

TEST(Trades, IdsAreUniqueAndFindable)
{
    std::set<uint16> seen;
    for (TradeDef const& t : TRADES)
    {
        EXPECT_TRUE(seen.insert(t.id).second) << "id " << t.id << " has two lines";
        EXPECT_EQ(FindTrade(t.id), &t);
    }
    EXPECT_EQ(FindTrade(1), nullptr) << "The Shade is not a common";
    EXPECT_EQ(FindTrade(0), nullptr);
}

TEST(Trades, EveryTradePaysSomething)
{
    // "Lose X, gain Y". A line with no Y is a tax, which is the shape Phase 2
    // deleted the scalars for being.
    for (TradeDef const& t : TRADES)
    {
        EXPECT_NE(t.boon, Boon::None) << "id " << t.id;
        EXPECT_GT(t.boonPct, 0) << "id " << t.id;
        EXPECT_NE(std::string(t.text), "") << "id " << t.id;
    }
}

TEST(Trades, ADenialNamesWhatItDenies)
{
    for (TradeDef const& t : TRADES)
    {
        if (t.curse == TradeCurse::Coefficient)
            continue;

        EXPECT_NE(t.mask, 0u) << "id " << t.id << " denies nothing";
        EXPECT_NE(std::string(t.noun), "") << "id " << t.id << " cannot say what it put away";
        EXPECT_EQ(t.pct, 0) << "id " << t.id << ": a denial carries no coefficient";
        EXPECT_FLOAT_EQ(TradeFactor(t), 1.0f) << "id " << t.id;
    }
}

TEST(Trades, ACoefficientIsAlwaysACost)
{
    // The sign has to point the way the kind gets worse for the player. A line
    // that read +10 on max health would be a free boon with a curse's text,
    // and a line at 0 would be a card that does nothing.
    for (TradeDef const& t : TRADES)
    {
        if (t.curse != TradeCurse::Coefficient)
            continue;

        ASSERT_NE(t.kind, AggregateKind::MAX) << "id " << t.id;
        EXPECT_NE(t.pct, 0) << "id " << t.id;
        if (WorseWhenLarger(t.kind))
        {
            EXPECT_GT(t.pct, 0) << "id " << t.id << ": more of this kind is worse, so the percent must be positive";
            EXPECT_GT(TradeFactor(t), 1.0f) << "id " << t.id;
        }
        else
        {
            EXPECT_LT(t.pct, 0) << "id " << t.id << ": less of this kind is worse, so the percent must be negative";
            EXPECT_LT(TradeFactor(t), 1.0f) << "id " << t.id;
        }
    }
}

TEST(Trades, NoTradeTakesAndPaysOnTheSameAxis)
{
    // Iron Purse's comment says why: "a curse and a boon that cancel are worse
    // than either alone". Taking 10% health and paying 5% back is not a trade,
    // it is a smaller tax that reads like one.
    for (TradeDef const& t : TRADES)
        if (t.curse == TradeCurse::Coefficient)
            EXPECT_NE(t.kind, PaidThrough(t.boon))
                << "id " << t.id << " curses the product its boon pays";
}

TEST(Trades, TheOfferPromisesWhatTheTablePays)
{
    // BoonTable reads this table for a common, so the number on the card and
    // the number the mechanic pays are the same number. Asked through the
    // generator's own published function rather than a copy of the arithmetic.
    for (TradeDef const& t : TRADES)
    {
        EXPECT_EQ(BoonMagnitude(t.id, t.boon), uint32(t.boonPct)) << "id " << t.id;

        // And nothing for a boon the line does not name, so a registry row
        // that drifted to another boon pays zero rather than the wrong thing --
        // which Trades.EveryLineBacksACommonRow would already have caught.
        for (uint8 b = 0; b < static_cast<uint8>(Boon::MAX); ++b)
            if (static_cast<Boon>(b) != t.boon)
                EXPECT_EQ(BoonMagnitude(t.id, static_cast<Boon>(b)), 0u) << "id " << t.id;
    }
}

TEST(Trades, TheBitsStayInsideTheWord)
{
    // A template's InventoryType and SubClass are database-filled uint32s. A
    // value past 31 is "denies nothing", never a shift the standard leaves
    // undefined.
    EXPECT_NE(InvTypeBit(Inv::HEAD), 0u);
    EXPECT_NE(InvTypeBit(28), 0u);
    EXPECT_EQ(InvTypeBit(32), 0u);
    EXPECT_EQ(InvTypeBit(9999), 0u);
    EXPECT_NE(WeaponBit(Wpn::AXE2), 0u);
    EXPECT_EQ(WeaponBit(64), 0u);

    // The two halves of a weapon line are distinct bits.
    EXPECT_NE(WeaponBit(Wpn::AXE) & WeaponBit(Wpn::AXE2), WeaponBit(Wpn::AXE));
}
