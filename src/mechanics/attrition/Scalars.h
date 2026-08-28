/*
 * mod-gauntlet - the shape shared by the four legacy scalars
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MECHANICS_SCALARS_H
#define MOD_GAUNTLET_MECHANICS_SCALARS_H

#include "GauntletMechanic.h"
#include "../Boons.h"

namespace Gauntlet
{
    // Rank to curse percentage. Design section 5 sets the floor -- "minimum
    // 15%: below that it is unfelt and therefore a fake affix" -- and section
    // 4.1 gives three ranks, but neither document names the three numbers.
    // 15/25/35 keeps rank III at the +35% the design uses as its worked
    // example and leaves room under the x2.0 damage-taken ceiling for two
    // scalars to stack before the cap bites.
    constexpr uint16 RANK_MAGNITUDE[MAX_RANK] = { 15, 25, 35 };   // TODO(design)

    // Generator 1 rolled a free percentage from a severity band and scaled it
    // by the condition, so a live affix sits anywhere in 2..115 and does not
    // land on the ladder above. AffixInstance::legacyMag carries that exact
    // number across the migration; 0 means the row has none and the rank is
    // the strength, which is every generator 2 affix.
    //
    // The point is that "Desperate Exposed 31%" stays 31% after the migration
    // instead of being rounded to the nearest rank, which would silently
    // change a hardcore character mid-run.
    inline uint16 ScalarMagnitude(AffixInstance const& a)
    {
        if (a.legacyMag != 0)
            return a.legacyMag;

        uint8 const rank = a.rank < 1 ? 1 : (a.rank > MAX_RANK ? MAX_RANK : a.rank);
        return RANK_MAGNITUDE[rank - 1];
    }

    // Every mechanic Phase 0 implements is the same thing: one percentage on
    // one AggregateKind, gated by the instance's condition. The condition is
    // evaluated by the caller (Mgr fills AggregateInput; see CONTRACT section
    // 7.3), so nothing below needs a Player and all four files compile without
    // a single core game header.
    class ScalarMechanic : public IMechanic
    {
    public:
        float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override;
        std::string Describe(AffixInstance const& self) const override;

    protected:
        // `lead` and `tail` wrap the magnitude: "you take " + "31" + "% more
        // damage". `raises` says whether the curse multiplies the base up.
        ScalarMechanic(AggregateKind kind, bool raises, char const* lead, char const* tail)
            : _kind(kind), _raises(raises), _lead(lead), _tail(tail) { }

    private:
        AggregateKind _kind;
        bool          _raises;
        char const*   _lead;
        char const*   _tail;
    };

    // " below half health", or "" for Condition::Always. Deliberately a local
    // table rather than a call to ConditionName: that lives in
    // GauntletAffix.cpp, which step 4b deletes, and these four mechanics must
    // keep describing themselves afterwards.
    std::string ConditionClause(Condition c);

    IMechanic* MakeExposed();
    IMechanic* MakeFeeble();
    IMechanic* MakeWithering();
    IMechanic* MakeForgetful();
}

#endif // MOD_GAUNTLET_MECHANICS_SCALARS_H
