/*
 * mod-gauntlet - what is standing around the player, and which of it is fair game
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "Nearby.h"

#include "Cell.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "Unit.h"

#include <list>

namespace Gauntlet
{
    namespace
    {
        // Acore's own searchers all want a check object with an operator()
        // over Unit*; these are the two this module needs and they are as
        // small as the core's own AllCreaturesOfEntryInRange
        // ($CORE/src/server/game/Grids/Notifiers/GridNotifiers.h:1504-1520),
        // which is the shape they are copied from.
        //
        // IsWithinDist(..., false) is the 3D distance without the bounding
        // radius, the same argument the core passes there.
        struct LiveInRange
        {
            LiveInRange(WorldObject const* from, float range) : _from(from), _range(range) { }

            bool operator()(Unit* unit) const
            {
                Creature* creature = unit ? unit->ToCreature() : nullptr;
                return creature && creature->IsInWorld() && creature->IsAlive()
                    && _from->IsWithinDist(creature, _range, false);
            }

        private:
            WorldObject const* _from;
            float              _range;
        };

        struct DeadInRange
        {
            DeadInRange(WorldObject const* from, float range) : _from(from), _range(range) { }

            bool operator()(Unit* unit) const
            {
                Creature* creature = unit ? unit->ToCreature() : nullptr;
                return creature && creature->IsInWorld() && !creature->IsAlive()
                    && _from->IsWithinDist(creature, _range, false);
            }

        private:
            WorldObject const* _from;
            float              _range;
        };

        template <typename Check>
        std::vector<Creature*> Collect(WorldObject const* from, float range, Check check)
        {
            std::vector<Creature*> out;
            if (!from || !from->IsInWorld() || range <= 0.0f)
                return out;

            std::list<Creature*> found;
            Acore::CreatureListSearcher<Check> searcher(from, found, check);
            Cell::VisitObjects(from, searcher, range);

            out.reserve(found.size());
            for (Creature* creature : found)
                out.push_back(creature);
            return out;
        }
    }

    std::vector<Creature*> CreaturesNear(WorldObject const* from, float range)
    {
        return Collect(from, range, LiveInRange(from, range));
    }

    std::vector<Creature*> CorpsesNear(WorldObject const* from, float range)
    {
        return Collect(from, range, DeadInRange(from, range));
    }

    bool IsOrdinaryFoe(Creature const* creature)
    {
        if (!creature)
            return false;

        // isElite() is false for rank RARE, so a rare mob is fair game and a
        // rare elite is not (Creature.h:115-122); isWorldBoss() reads the
        // template's boss type flag (:124) and IsDungeonBoss() the flags_extra
        // (:132).
        if (creature->isElite() || creature->isWorldBoss() || creature->IsDungeonBoss())
            return false;

        // IsQuestGiver() is the only cheap test the core offers for "a creature
        // a quest needs" (Unit.h:806); a kill-objective mob is indistinguishable
        // from any other and is deliberately left alone-able, since being
        // harder to kill is the point of half these affixes.
        if (creature->IsQuestGiver())
            return false;

        // Nothing that belongs to somebody, nothing that is scenery, and
        // nothing another script summoned. Promoting a warlock's felhunter or
        // copying a scripted event's add is a bug in every case.
        if (creature->IsPet() || creature->IsGuardian() || creature->IsTotem() ||
            creature->IsSummon() || creature->IsVehicle() || creature->IsCritter() ||
            creature->IsTrigger() || creature->IsCharmedOwnedByPlayerOrPlayer())
            return false;

        return true;
    }

    bool IsFairGame(Player* owner, Creature* creature, bool hostileOnly)
    {
        if (!owner || !creature || !creature->IsInWorld() || !creature->IsAlive())
            return false;

        if (!IsOrdinaryFoe(creature))
            return false;

        // Asked of the creature rather than of the player: a player's own
        // reaction can be softened by a disguise or a phase, and what matters
        // is how the creature would answer.
        //
        // IsHostileTo is REP_HOSTILE or worse and IsFriendlyTo is REP_FRIENDLY
        // or better (Unit.cpp:7303-7311), so neutral satisfies neither. See the
        // header for which question each caller is actually asking.
        return hostileOnly ? creature->IsHostileTo(owner) : !creature->IsFriendlyTo(owner);
    }

    Creature* NearestIdleKin(Player* owner, Creature const* kin, WorldObject const* origin,
                             float range, Creature const* exclude)
    {
        if (!owner || !kin || !origin)
            return nullptr;

        uint32 const faction = kin->GetFaction();

        Creature* best     = nullptr;
        float     bestDist = range + 1.0f;

        for (Creature* candidate : CreaturesNear(origin, range))
        {
            if (candidate == kin || candidate == exclude)
                continue;
            if (candidate->GetFaction() != faction)
                continue;
            // Not `hostileOnly`. The candidate shares a faction with a
            // creature this player is already fighting, so "not on your side"
            // is the right question -- and asking for hostility instead
            // excluded every neutral camp in the game, which is most of what a
            // levelling character pulls.
            if (!IsFairGame(owner, candidate, /*hostileOnly*/ false))
                continue;

            // "Idle": not already in a fight. A camp that is already awake
            // needs no alerting, and pulling a creature that is mid-fight with
            // somebody else would hand this player's affix to a stranger.
            if (candidate->IsInCombat() || candidate->GetVictim())
                continue;

            float const dist = origin->GetDistance(candidate);
            if (dist < bestDist)
            {
                bestDist = dist;
                best     = candidate;
            }
        }

        return best;
    }
}
