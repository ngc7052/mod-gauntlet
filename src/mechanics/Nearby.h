/*
 * mod-gauntlet - what is standing around the player, and which of it is fair game
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_MECHANICS_NEARBY_H
#define MOD_GAUNTLET_MECHANICS_NEARBY_H

#include "Define.h"
#include <vector>

// Core types by pointer only, so a mechanic that includes this header does not
// have to drag Player.h in behind it. The definitions are in Nearby.cpp, which
// does include the grid notifiers and is therefore checked only by
// tests/compile-check.sh and the real build.
class Creature;
class Player;
class Unit;
class WorldObject;

namespace Gauntlet
{
    // Every creature within `range` of `from` that is in the world and alive,
    // in no particular order. Design section 6 measured this: a grid search at
    // 500 ms for a handful of real players is cheap, and bots never reach it
    // because Mgr::IsEligible has already refused them.
    //
    // Four Phase 2 mechanics need it -- Call to Arms fetches the nearest kin of
    // a corpse, Craven fetches a friend for a fleeing mob, Keen-nosed alerts
    // what would not have seen you, Reinforcements counts what it has already
    // spawned -- and each filters the list itself rather than passing a
    // predicate, because the lists are a handful of entries and a virtual call
    // per creature per tick is the thing worth avoiding.
    std::vector<Creature*> CreaturesNear(WorldObject const* from, float range);

    // Every corpse within `range` of `from`: creatures in the world that are
    // not alive. Death Rattle and Grudge both stand something on one.
    std::vector<Creature*> CorpsesNear(WorldObject const* from, float range);

    // "An ordinary enemy": the set of creatures this module is allowed to
    // change, promote, hurry, frighten or copy. Never an elite, a boss or a
    // quest giver, and never anything that belongs to somebody -- a pet, a
    // guardian, a totem, another script's summon, a vehicle, a critter or an
    // invisible trigger. Champions has enforced exactly this list since
    // Phase 1; it is here so that eight more mechanics cannot each drift from
    // it, and Champions now reads it from here.
    //
    // Alive-ness is deliberately NOT part of it: Call to Arms and Death Rattle
    // both start from a corpse and have to ask this question about one.
    bool IsOrdinaryFoe(Creature const* creature);

    // IsOrdinaryFoe, alive, in the world, and not on the owner's side.
    //
    // `hostileOnly` is what separates the two questions this predicate gets
    // asked, and getting it wrong costs whole affixes. Unit::IsHostileTo is
    // `GetReactionTo(unit) <= REP_HOSTILE` (Unit.cpp:7303-7306), so a *neutral*
    // creature -- every yellow nameplate in the game, which is most of what a
    // levelling character fights -- is not hostile to anybody. A predicate that
    // demanded hostility therefore excluded them all.
    //
    //   true  (default): "would this creature have attacked me anyway?"
    //                    Keen-nosed asks this. It only makes an enemy notice
    //                    you sooner than it would have, and waking a neutral
    //                    mob that was never going to attack is a different and
    //                    much larger affix than the card describes.
    //
    //   false:           "is this creature on my side?" Call to Arms and Craven
    //                    ask this, about the kin of a creature the player is
    //                    already fighting. If you are fighting one gnoll, the
    //                    gnoll beside it is fair game whatever colour its
    //                    nameplate is -- that is the whole of "a camp becomes a
    //                    rolling fight".
    bool IsFairGame(Player* owner, Creature* creature, bool hostileOnly = true);

    // The nearest creature to `origin` that is fair game for `owner`, shares
    // `kin`'s faction, and is not already fighting anybody -- what Call to Arms
    // and Craven both mean by "its nearest kin". Null when the camp is empty.
    //
    // `exclude` is skipped even when it qualifies, so a corpse cannot fetch
    // itself and a fleeing mob cannot fetch the mob it is fleeing from.
    Creature* NearestIdleKin(Player* owner, Creature const* kin, WorldObject const* origin,
                             float range, Creature const* exclude = nullptr);
}

#endif // MOD_GAUNTLET_MECHANICS_NEARBY_H
