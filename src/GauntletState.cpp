/*
 * mod-gauntlet - the two SQL calls behind GauntletState
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletState.h"
#include "DatabaseEnv.h"

// The only two functions in this translation unit are the ones that touch a
// database, which is exactly why it needs the core's full include set and why
// it is skipped by tests/syntax-check.sh and never linked into
// tests/run-tests.sh's gauntlet_tests binary -- both scripts already skip any
// .cpp that includes DatabaseEnv.h, the same rule that excludes
// GauntletMgr.cpp. Everything Get/Set/Add/Dirty/Clear needs to work lives in
// GauntletState.h instead, inline, so tests/StateTest.cpp exercises the real
// map without this file or a database. CONTRACT-P1 section 7 asks that this
// be said plainly: the SQL round trip itself has no unit test.

namespace Gauntlet
{
    void State::LoadFrom(uint32 lowGuid)
    {
        _values.clear();

        // One query, not one per key: whatever this player carries comes back
        // in a single round trip, however many keys that is.
        if (QueryResult r = CharacterDatabase.Query(
                "SELECT `k`, `v` FROM `gauntlet_state` WHERE `guid` = {}", lowGuid))
        {
            do
            {
                Field* f = r->Fetch();
                std::string key = f[0].Get<std::string>();
                int32 value      = f[1].Get<int32>();

                // A row just loaded from the database is not a pending write.
                _values.emplace(std::move(key), Entry{ value, false });
            } while (r->NextRow());
        }
    }

    void State::SaveTo(uint32 lowGuid)
    {
        // No dirty key: no query at all, which is what makes calling this
        // every 60 seconds regardless of activity cheap.
        for (auto& [key, entry] : _values)
        {
            if (!entry.dirty)
                continue;

            std::string escapedKey = key;
            CharacterDatabase.EscapeString(escapedKey);

            // REPLACE rather than INSERT .. ON DUPLICATE KEY UPDATE: the table
            // has no column this would need to preserve across the write, and
            // it is what the rest of the module already uses for a
            // guid-keyed row (Mgr::EndRun's `gauntlet_leaderboard` write,
            // GauntletMgr.cpp:749-753).
            CharacterDatabase.Execute(
                "REPLACE INTO `gauntlet_state` (`guid`, `k`, `v`) VALUES ({}, '{}', {})",
                lowGuid, escapedKey, entry.value);

            entry.dirty = false;
        }
    }
}
