/*
 * mod-gauntlet - making one buff last longer, or a debuff outstay its welcome
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "AuraDurationEdit.h"

#include "Player.h"
#include "SpellAuras.h"
#include "SpellInfo.h"

#include <algorithm>
#include <limits>

namespace Gauntlet
{
    namespace AuraDurationEdit
    {
        void Edit(Aura* aura, int32 ms)
        {
            if (!aura || ms <= 0)
                return;

            // A permanent aura has duration -1 and is left alone; see the
            // header.
            if (aura->GetMaxDuration() < 0)
                return;

            aura->SetMaxDuration(ms);           // SpellAuras.h:130
            aura->SetDuration(ms);              // SpellAuras.h:134
        }

        void Scale(Aura* aura, float factor)
        {
            if (!aura || factor <= 0.0f)
                return;

            int32 const base = aura->GetMaxDuration();
            if (base <= 0)
                return;

            double const scaled = double(base) * double(factor) + 0.5;
            Edit(aura, int32(std::min(scaled, double(std::numeric_limits<int32>::max()))));
        }

        bool Matches(Unit* unit, Aura* aura, Player* owner, uint32 spellId)
        {
            if (!unit || !aura || !owner || spellId == 0)
                return false;

            // The unit the aura landed on has to be our player. OnAuraApply is
            // a UnitScript hook and fires for every unit on the map.
            if (unit != owner)
                return false;

            SpellInfo const* info = aura->GetSpellInfo();
            return info && info->Id == spellId;
        }
    }
}
