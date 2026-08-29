/*
 * mod-gauntlet - making one buff last longer, or a debuff outstay its welcome
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MECHANICS_AURA_DURATION_EDIT_H
#define MOD_GAUNTLET_MECHANICS_AURA_DURATION_EDIT_H

#include "Define.h"

class Aura;
class Player;
class Unit;

namespace Gauntlet
{
    // The third shared primitive: change how long a named aura lasts on this
    // module's own player, at the moment it is applied.
    //
    // C5 Long Forbearance is the card that needs it -- "Forbearance lasts three
    // minutes" against the spell's own sixty seconds -- and the shape recurs
    // wherever a curse makes a cost linger rather than adding a new one. It is
    // the tax half of family C's "tax before deny" ladder expressed in time
    // instead of in resource.
    //
    // Two things the caller must know, and both are why this is a helper rather
    // than three lines inlined in a curse.
    //
    // **The tooltip will lie.** Aura::SetDuration and SetMaxDuration
    // ($CORE/src/server/game/Spells/Auras/SpellAuras.h:130, :134) move the
    // server's clock and the client's timer, but the spell's DBC entry still
    // says sixty seconds, and anything reading the DBC -- the tooltip, an addon
    // -- will go on saying so. This is the same cost Falling Sky's dodge buff
    // already pays and it cannot be fixed without a client patch, so the curse
    // that uses this owes the player a sentence saying what the real number is.
    //
    // **Both halves or neither.** SetMaxDuration alone leaves the bar draining
    // at the old rate to a stop short of the end; SetDuration alone leaves the
    // bar full-length and the aura falling off early. Edit() sets both, which
    // is the only combination that makes the client's timer true.
    namespace AuraDurationEdit
    {
        // Sets the aura's remaining and maximum duration to `ms`. Ignores an
        // aura that is permanent (duration -1), because extending "forever" is
        // meaningless and shortening it is a different affix.
        void Edit(Aura* aura, int32 ms);

        // Multiplies instead, for a curse whose card states a factor rather
        // than a figure. Rounds to the nearest millisecond and never produces
        // zero, which would be an aura that expires the instant it lands.
        void Scale(Aura* aura, float factor);

        // Whether this aura is the one a curse is watching for, on the player
        // it is watching. Both halves matter: OnAuraApply fires for every unit
        // in the world, so a curse that checks only the spell id will edit the
        // duration of a mob's copy of the same buff.
        bool Matches(Unit* unit, Aura* aura, Player* owner, uint32 spellId);
    }
}

#endif // MOD_GAUNTLET_MECHANICS_AURA_DURATION_EDIT_H
