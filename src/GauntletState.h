/*
 * mod-gauntlet - per-player mechanic state that survives a logout
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_STATE_H
#define MOD_GAUNTLET_STATE_H

#include "Define.h"
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

// Deliberately free of DatabaseEnv.h and every other core game header, like
// Gauntlet.h and GauntletAggregate.h: a mechanic and the unit tests must be
// able to read and write this map with no database at all. LoadFrom and
// SaveTo are the two calls that need one; they are declared here but defined
// in GauntletState.cpp, which does include DatabaseEnv.h and is therefore
// skipped by tests/syntax-check.sh and tests/run-tests.sh -- the same rule
// that already skips GauntletMgr.cpp -- and checked only by the real core
// build. Every other member is defined inline, right here, so
// tests/StateTest.cpp exercises the real map logic without linking that file
// or reaching a database.

namespace Gauntlet
{
    // Plan section 3.3 / CONTRACT-P1 section 5.2: per-player key/value that
    // outlives a session. Keys are "<mechanic key>.<field>" ("champions.count",
    // "shade.rank", "shade.deadUntilTier", "deepwounds.wound"), where the first
    // half is the registry's MechanicDef::key. Values are a signed 32-bit int:
    // a percentage, a count, a tier, or Deep Wounds' wound, which is an
    // absolute health amount. gauntlet_state.v is a plain SQL INT (signed, not
    // UNSIGNED), which is exactly int32 -- see Field.h's own guidance table
    // (MEDIUMINT/INT <-> Get<int32>/Get<uint32>) -- and even a health pool many
    // times a level-80 character's actual maximum sits nowhere near an int32's
    // +-2.1 billion range, so nothing here can overflow the column.
    class State
    {
    public:
        // gauntlet_state.k is VARCHAR(32). A key past that length is a bug in
        // the calling mechanic, not something to truncate: truncating two
        // different long keys onto the same 32 characters would silently merge
        // their counters under one row. Set() refuses a key over the limit
        // instead of shortening it, so an oversized key is never written to
        // the map and therefore never reaches the database under a mangled
        // name either -- it simply behaves as a key that is never set.
        static constexpr std::size_t MaxKeyLen = 32;

        int32 Get(std::string_view key, int32 fallback = 0) const
        {
            auto it = _values.find(std::string(key));
            return it != _values.end() ? it->second.value : fallback;
        }

        void Set(std::string_view key, int32 value)
        {
            if (key.size() > MaxKeyLen)
                return;

            Entry& entry = _values[std::string(key)];
            entry.value  = value;
            entry.dirty  = true;
        }

        int32 Add(std::string_view key, int32 delta)
        {
            int32 const next = Get(key, 0) + delta;
            Set(key, next);
            return next;
        }

        // Whether any key has a pending write. Integration's periodic save
        // reads this before bothering to call SaveTo at all; SaveTo itself
        // is just as cheap to call when nothing is dirty; see below.
        bool Dirty() const
        {
            for (auto const& [key, entry] : _values)
                if (entry.dirty)
                    return true;
            return false;
        }

        // One query on login (CONTRACT-P1 section 5.2): the whole map is
        // replaced from a single "WHERE guid = ?", not one query per key.
        // Defined in GauntletState.cpp.
        void LoadFrom(uint32 lowGuid);

        // Writes only the keys marked dirty since the last successful save,
        // and issues no query at all when none are. Defined in
        // GauntletState.cpp.
        void SaveTo(uint32 lowGuid);

        // Wipes every key, dirty or not, with no database access. Integration
        // calls this when a run is discarded because the guid it was keyed on
        // has been reassigned to a different character (Mgr::Load's
        // wrongClass/impossibleTier branch, GauntletMgr.cpp) -- PurgeCharacter
        // already deletes the row set from `gauntlet_state`, but a State
        // object that had already loaded the previous occupant's keys would
        // otherwise keep them in memory and resurrect them on the next save.
        void Clear()
        {
            _values.clear();
        }

    private:
        // Set() always marks a key dirty, even when the new value equals the
        // old one: comparing against the stored value would need reading it
        // back on every call, and in practice a mechanic calls Set/Add
        // exactly when the value has actually changed (a counter incremented,
        // a rank taken), so there is nothing to gain from the comparison.
        struct Entry
        {
            int32 value = 0;
            bool  dirty = false;
        };

        std::unordered_map<std::string, Entry> _values;
    };
}

#endif // MOD_GAUNTLET_STATE_H
