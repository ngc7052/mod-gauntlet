/*
 * mod-gauntlet - registry id to implementation
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"
#include <unordered_map>

namespace Gauntlet
{
    namespace
    {
        // Self-registration rather than one switch. Every mechanic lives in its
        // own translation unit and each phase adds several at once, so a shared
        // switch would be the one file every author had to edit -- and, when
        // several are written in parallel, the one file they would collide in.
        //
        // The map is a function-local static so it is built on first use: the
        // registrars below are file-scope objects in other translation units,
        // and their construction order relative to a namespace-scope map here
        // is not defined.
        std::unordered_map<uint16, MechanicFactoryFn>& Factories()
        {
            static std::unordered_map<uint16, MechanicFactoryFn> map;
            return map;
        }
    }

    MechanicRegistrar::MechanicRegistrar(uint16 id, MechanicFactoryFn fn)
    {
        Factories()[id] = fn;
    }

    IMechanic* MakeMechanic(uint16 id)
    {
        auto const it = Factories().find(id);
        return it == Factories().end() ? nullptr : it->second();
    }
}
