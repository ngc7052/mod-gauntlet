/*
 * mod-gauntlet - the pure half of the attach/detach audit
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAudit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// Nothing here includes a core header, which is what puts this file in
// tests/run-tests.sh's Player-free set. The reason for the split is the one
// GauntletWire.cpp records: the interesting part of a piece of code is usually
// the part that does not need a world, and leaving it in the file that does
// need one means it never gets a test.

namespace Gauntlet
{
    namespace
    {
        // The aggregate is a product of floats and the two readings are taken
        // microseconds apart, so an exact comparison would report the last bit
        // of a multiplication as a leak. A thousandth is far below anything a
        // ladder step moves and far above the noise.
        constexpr float EPSILON = 0.001f;

        bool Moved(float a, float b) { return std::fabs(a - b) > EPSILON; }

        std::string Fixed(float v)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
            return buf;
        }

        // Multiset difference: every id in `b` that `b` has more copies of than
        // `a` does. Both inputs are sorted, which Capture guarantees and the
        // tests state.
        //
        // Multiset rather than set because an aura's stack count is state. An
        // affix that applies a second stack of something the character already
        // had, and removes only one on detach, has leaked -- and a plain set
        // difference sees nothing at all.
        std::vector<uint32> Extra(std::vector<uint32> const& a, std::vector<uint32> const& b)
        {
            std::vector<uint32> out;
            std::size_t i = 0, j = 0;
            while (j < b.size())
            {
                if (i >= a.size() || b[j] < a[i])
                {
                    out.push_back(b[j]);
                    ++j;
                }
                else if (a[i] < b[j])
                {
                    ++i;
                }
                else
                {
                    ++i;
                    ++j;
                }
            }
            return out;
        }

        void Count(std::vector<std::string>& out, char const* noun, uint32 before, uint32 after)
        {
            if (before != after)
                out.push_back(std::string(noun) + " " + std::to_string(before) + " -> " + std::to_string(after));
        }

        void Ratio(std::vector<std::string>& out, std::string const& noun, float before, float after)
        {
            if (Moved(before, after))
                out.push_back(noun + " x" + Fixed(before) + " -> x" + Fixed(after));
        }
    }

    std::vector<std::string> Diff(Footprint const& before, Footprint const& after)
    {
        std::vector<std::string> out;

        // Structure first. A carried count that did not come back means the
        // audit's own detach failed, which makes every line below it suspect,
        // so it is reported before anything it would explain.
        Count(out, "carried affixes", before.carried, after.carried);

        // Then the two that are loose in the world rather than on the
        // character. A summon with no affix behind it is the failure phase 9
        // called the worst this module can produce, and an event still armed
        // will fire later with nothing carried to blame it on -- both are worse
        // than any number below, so both are read first.
        Count(out, "summons owned", before.summons, after.summons);
        Count(out, "scheduler entries queued", before.armed, after.armed);

        // The equipment, before the auras: a slot that did not get its item
        // back is the denials' one real failure, and every aura line after it
        // would only be describing the same missing item.
        for (std::size_t slot = 0; slot < before.equipment.size() && slot < after.equipment.size(); ++slot)
            if (before.equipment[slot] != after.equipment[slot])
            {
                std::string line = "equipment slot " + std::to_string(slot) + ": ";
                line += before.equipment[slot] ? "item " + std::to_string(before.equipment[slot]) : std::string("nothing");
                line += " -> ";
                line += after.equipment[slot] ? "item " + std::to_string(after.equipment[slot]) : std::string("nothing");
                out.push_back(line);
            }

        for (uint32 id : Extra(before.auras, after.auras))
            out.push_back("aura " + std::to_string(id) + " still applied");
        for (uint32 id : Extra(after.auras, before.auras))
            out.push_back("aura " + std::to_string(id) + " was removed and not restored");

        for (uint32 id : Extra(before.cooldowns, after.cooldowns))
            out.push_back("spell " + std::to_string(id) + " still on cooldown");
        for (uint32 id : Extra(after.cooldowns, before.cooldowns))
            out.push_back("spell " + std::to_string(id) + " had its cooldown cleared and not restored");

        Count(out, "shapeshift form", before.shapeshift, after.shapeshift);
        Count(out, "max health", before.maxHealth, after.maxHealth);
        Count(out, "max power", before.maxPower, after.maxPower);
        Count(out, "free talent points", before.freeTalents, after.freeTalents);

        Ratio(out, "run speed", before.speedRun, after.speedRun);
        Ratio(out, "swim speed", before.speedSwim, after.speedSwim);

        for (uint8 k = 0; k < static_cast<uint8>(AggregateKind::MAX); ++k)
            Ratio(out, AggregateKindName(static_cast<AggregateKind>(k)),
                  before.aggregate[k], after.aggregate[k]);

        return out;
    }
}
