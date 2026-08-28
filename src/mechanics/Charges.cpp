/*
 * mod-gauntlet - "once per level", counted somewhere it survives a logout
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "Charges.h"

#include <string>

namespace Gauntlet
{
    namespace Charges
    {
        std::string SpentKey(char const* mechanicKey)
        {
            return std::string(mechanicKey ? mechanicKey : "?") + ".spent";
        }

        namespace
        {
            uint8 Ladder(uint8 everyLevels)
            {
                return everyLevels == 0 ? 1 : everyLevels;
            }

            // The stored spend, with the level-down case already folded in: a
            // value above the player's current level cannot have been written
            // by this run at this level and is read as "never spent".
            int32 SpentAt(State const* state, char const* mechanicKey, uint8 level)
            {
                if (!state)
                    return 0;

                int32 const spent = state->Get(SpentKey(mechanicKey));
                if (spent <= 0 || spent > int32(level))
                    return 0;

                return spent;
            }
        }

        bool Available(State const* state, char const* mechanicKey, uint8 level, uint8 everyLevels)
        {
            int32 const spent = SpentAt(state, mechanicKey, level);
            if (spent == 0)
                return true;

            return int32(level) >= spent + int32(Ladder(everyLevels));
        }

        void Spend(State* state, char const* mechanicKey, uint8 level)
        {
            if (state)
                state->Set(SpentKey(mechanicKey), int32(level));
        }

        uint8 ReturnsAtLevel(State const* state, char const* mechanicKey, uint8 level, uint8 everyLevels)
        {
            if (Available(state, mechanicKey, level, everyLevels))
                return 0;

            int32 const at = SpentAt(state, mechanicKey, level) + int32(Ladder(everyLevels));
            return at > 255 ? 255 : uint8(at);
        }

        void Clear(State* state, char const* mechanicKey)
        {
            if (state)
                state->Set(SpentKey(mechanicKey), 0);
        }
    }
}
