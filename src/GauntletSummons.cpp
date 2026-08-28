/*
 * mod-gauntlet - owner-bound summons, and the guarantee they come back out
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletSummons.h"
#include "Config.h"
#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "TemporarySummon.h"
#include <algorithm>
#include <cstddef>

namespace Gauntlet
{
    namespace
    {
        // Which affix owns which of the five reserved entries, for a caller
        // that did not say. Phase 2's Reinforcements and Echo summon copies of
        // existing entries and must pass their own id instead -- there is no
        // entry to read it off.
        uint16 MechanicForEntry(uint32 entry)
        {
            switch (entry)
            {
                case ENTRY_SHADE:        return 1;    // S1 The Shade
                case ENTRY_DOPPELGANGER: return 2;    // S2 Echo
                case ENTRY_SCAVENGER:    return 3;    // S3 Carrion
                case ENTRY_AMBUSHER:     return 5;    // S5 Ambush
                case ENTRY_RESTLESS:     return 10;   // E5 Grudge
                default:                 return MECHANIC_NONE;
            }
        }
    }

    Summons* Summons::instance()
    {
        static Summons inst;
        return &inst;
    }

    void Summons::LoadConfig()
    {
        // Only the total is a config key today. The stalker cap, the leash and
        // the backstop lifetime are constants in the header; see the report for
        // the keys worth adding to mod_gauntlet.conf.dist.
        _maxAlive = sConfigMgr->GetOption<uint32>("Gauntlet.Summons.MaxAlive", SUMMON_CAP_TOTAL);
        if (_maxAlive == 0 || _maxAlive > 16)
            _maxAlive = SUMMON_CAP_TOTAL;

        _configLoaded = true;
    }

    void Summons::EnsureConfig()
    {
        if (!_configLoaded)
            LoadConfig();
    }

    // -----------------------------------------------------------------------
    // Bookkeeping
    // -----------------------------------------------------------------------

    Creature* Summons::Resolve(Record const& r) const
    {
        // Through the map rather than through the owner, because the owner is
        // exactly the thing that may already be gone.
        Map* map = sMapMgr->FindMap(r.mapId, r.instanceId);
        if (!map)
            return nullptr;

        Creature* c = map->GetCreature(r.guid);
        return (c && c->IsInWorld()) ? c : nullptr;
    }

    void Summons::Drop(Records& list, std::size_t index)
    {
        _ownerOf.erase(list[index].guid);
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void Summons::Prune(ObjectGuid ownerGuid)
    {
        auto const it = _byOwner.find(ownerGuid);
        if (it == _byOwner.end())
            return;

        Records& list = it->second;
        for (std::size_t i = list.size(); i-- > 0; )
        {
            Creature* c = Resolve(list[i]);
            if (!c || !c->IsAlive())
                Drop(list, i);
        }

        if (list.empty())
            _byOwner.erase(it);
    }

    // -----------------------------------------------------------------------
    // Summoning
    // -----------------------------------------------------------------------

    Creature* Summons::Summon(Player* owner, uint32 entry, Position const& at,
                             uint32 despawnMs, bool countsAsStalker, uint16 mechanic)
    {
        if (!owner || !owner->IsInWorld() || !owner->IsAlive())
            return nullptr;

        EnsureConfig();

        ObjectGuid const ownerGuid = owner->GetGUID();
        Prune(ownerGuid);

        auto const existing = _byOwner.find(ownerGuid);
        if (existing != _byOwner.end())
        {
            if (existing->second.size() >= _maxAlive)
                return nullptr;

            if (countsAsStalker)
            {
                uint32 stalkers = 0;
                for (Record const& r : existing->second)
                    if (r.stalker)
                        ++stalkers;

                if (stalkers >= SUMMON_CAP_STALKER)
                    return nullptr;
            }
        }

        // Level to the owner. The creature's summoner GUID is set by
        // TempSummon's constructor, before Creature::Create runs, so the
        // OnBeforeCreatureSelectLevel hook in GauntletSummonAI.cpp can already
        // recognise it -- but only if it can tell which spawn is ours, which is
        // what this pair of fields is for. See the report for why the hook is
        // used rather than the plan's SetLevel fallback: SelectLevel derives
        // health, mana and weapon damage from the level it is given, and
        // Creature::UpdateAllStats does not (StatSystem.cpp:1070 reads back the
        // stat modifier SelectLevel wrote, so it cannot rescale on its own).
        uint8 const wanted = owner->GetLevel();

        _pendingOwner = ownerGuid;
        _pendingLevel = wanted;

        TempSummon* c = owner->SummonCreature(entry, at, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, despawnMs);

        _pendingOwner.Clear();

        if (!c)
            return nullptr;

        if (c->GetLevel() != wanted)
        {
            // The hook did not fire. Re-run the core's own level selection with
            // the pending record restored: SelectLevel is public
            // (Creature.h:66) and the core itself calls it on a live creature
            // when one respawns (Creature.cpp:2091).
            _pendingOwner = ownerGuid;
            c->SelectLevel();
            _pendingOwner.Clear();
        }

        if (c->GetLevel() != wanted)
        {
            // Plan section 6's fallback, and the last resort. It gets the level
            // right and leaves the health and damage at the template's, which
            // is wrong but visible; the hook not being registered is the only
            // way to arrive here, so say so once rather than silently.
            c->SetLevel(wanted);
            c->UpdateAllStats();

            static bool warned = false;
            if (!warned)
            {
                warned = true;
                LOG_ERROR("module", "Gauntlet: summon {} could not be levelled through "
                                    "OnBeforeCreatureSelectLevel, so its health and damage are the "
                                    "template's rather than the owner's. Is AddSC_gauntlet_summons() "
                                    "called from Addmod_gauntletScripts()?", entry);
            }
        }

        c->SetFullHealth();

        Record rec;
        rec.guid       = c->GetGUID();
        rec.entry      = entry;
        rec.mechanic   = (mechanic != MECHANIC_NONE) ? mechanic : MechanicForEntry(entry);
        rec.mapId      = c->GetMapId();
        rec.instanceId = c->GetInstanceId();
        rec.stalker    = countsAsStalker;

        _byOwner[ownerGuid].push_back(rec);
        _ownerOf[rec.guid] = ownerGuid;

        if (_observe)
            _observe(ownerGuid, rec.mechanic, rec.entry, true);

        return c;
    }

    // -----------------------------------------------------------------------
    // Despawning
    // -----------------------------------------------------------------------

    void Summons::DespawnAll(Player* owner)
    {
        if (owner)
            Forget(owner->GetGUID());
    }

    void Summons::DespawnFor(Player* owner, uint16 mechanic)
    {
        if (!owner)
            return;

        ObjectGuid const ownerGuid = owner->GetGUID();

        auto const it = _byOwner.find(ownerGuid);
        if (it == _byOwner.end())
            return;

        // Unhook the records first and despawn afterwards. DespawnOrUnsummon
        // reaches Creature::RemoveFromWorld and therefore comes straight back
        // into NoteRemoved, which erases from the very containers this is
        // walking; doing the bookkeeping up front makes that call a no-op
        // instead of a dangling reference and a second observer notification.
        Records doomed;
        Records& list = it->second;
        for (std::size_t i = list.size(); i-- > 0; )
        {
            if (list[i].mechanic != mechanic)
                continue;

            doomed.push_back(list[i]);
            Drop(list, i);
        }

        if (list.empty())
            _byOwner.erase(it);

        for (Record const& r : doomed)
        {
            if (Creature* c = Resolve(r))
                c->DespawnOrUnsummon();

            if (!r.gone && _observe)
                _observe(ownerGuid, r.mechanic, r.entry, false);
        }
    }

    void Summons::Forget(ObjectGuid ownerGuid)
    {
        auto const it = _byOwner.find(ownerGuid);
        if (it == _byOwner.end())
            return;

        // Same order as DespawnFor, for the same reason.
        Records const doomed = it->second;
        for (Record const& r : doomed)
            _ownerOf.erase(r.guid);
        _byOwner.erase(it);

        for (Record const& r : doomed)
        {
            if (Creature* c = Resolve(r))
                c->DespawnOrUnsummon();

            if (!r.gone && _observe)
                _observe(ownerGuid, r.mechanic, r.entry, false);
        }
    }

    void Summons::NoteDied(Creature* c)
    {
        if (!c)
            return;

        auto const owned = _ownerOf.find(c->GetGUID());
        if (owned == _ownerOf.end())
            return;

        auto const it = _byOwner.find(owned->second);
        if (it == _byOwner.end())
            return;

        for (Record& r : it->second)
        {
            if (r.guid != c->GetGUID() || r.gone)
                continue;

            r.gone = true;
            if (_observe)
                _observe(owned->second, r.mechanic, r.entry, false);
            return;
        }
    }

    void Summons::NoteRemoved(Creature* c)
    {
        if (!c)
            return;

        auto const owned = _ownerOf.find(c->GetGUID());
        if (owned == _ownerOf.end())
            return;

        ObjectGuid const ownerGuid = owned->second;
        uint16 mechanic = MECHANIC_NONE;
        uint32 entry    = c->GetEntry();
        bool   announce = true;

        auto const it = _byOwner.find(ownerGuid);
        if (it != _byOwner.end())
        {
            Records& list = it->second;
            auto const pos = std::find_if(list.begin(), list.end(),
                                          [c](Record const& e) { return e.guid == c->GetGUID(); });
            if (pos != list.end())
            {
                mechanic = pos->mechanic;
                entry    = pos->entry;
                announce = !pos->gone;
                list.erase(pos);
            }

            if (list.empty())
                _byOwner.erase(it);
        }

        _ownerOf.erase(c->GetGUID());

        if (announce && _observe)
            _observe(ownerGuid, mechanic, entry, false);
    }

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    uint32 Summons::AliveFor(Player* owner) const
    {
        if (!owner)
            return 0;

        auto const it = _byOwner.find(owner->GetGUID());
        if (it == _byOwner.end())
            return 0;

        uint32 alive = 0;
        for (Record const& r : it->second)
            if (!r.gone)
                if (Creature* c = Resolve(r))
                    if (c->IsAlive())
                        ++alive;

        return alive;
    }

    bool Summons::HasStalker(Player* owner) const
    {
        if (!owner)
            return false;

        auto const it = _byOwner.find(owner->GetGUID());
        if (it == _byOwner.end())
            return false;

        for (Record const& r : it->second)
            if (r.stalker && !r.gone)
                if (Creature* c = Resolve(r))
                    if (c->IsAlive())
                        return true;

        return false;
    }

    bool Summons::IsGauntletSummon(Creature* c) const
    {
        return c && _ownerOf.find(c->GetGUID()) != _ownerOf.end();
    }

    uint16 Summons::MechanicOf(Creature* c) const
    {
        if (!c)
            return MECHANIC_NONE;

        auto const owned = _ownerOf.find(c->GetGUID());
        if (owned == _ownerOf.end())
            return MECHANIC_NONE;

        auto const it = _byOwner.find(owned->second);
        if (it == _byOwner.end())
            return MECHANIC_NONE;

        for (Record const& r : it->second)
            if (r.guid == c->GetGUID())
                return r.mechanic;

        return MECHANIC_NONE;
    }

    Player* Summons::OwnerOf(Creature* c) const
    {
        if (!c)
            return nullptr;

        auto const owned = _ownerOf.find(c->GetGUID());
        if (owned == _ownerOf.end())
            return nullptr;

        // Same map only: an owner who has zoned away is, for every purpose
        // this answers, not the owner of a creature standing in the old zone.
        return ObjectAccessor::GetPlayer(*c, owned->second);
    }

    bool Summons::IsPendingSummon(Creature* c) const
    {
        if (_pendingOwner.IsEmpty() || !c || !c->IsSummon())
            return false;

        return c->ToTempSummon()->GetSummonerGUID() == _pendingOwner;
    }

    bool Summons::PendingLevel(Creature* c, uint8& level) const
    {
        if (!IsPendingSummon(c))
            return false;

        level = _pendingLevel;
        return true;
    }

    // -----------------------------------------------------------------------
    // The rank ladders
    // -----------------------------------------------------------------------

    void Summons::Scale(Creature* c, float healthMult, float damageMult) const
    {
        if (!c)
            return;

        if (healthMult > 0.0f && healthMult != 1.0f)
        {
            uint32 const health = std::max<uint32>(1, uint32(c->GetMaxHealth() * healthMult));

            // Three writes, not one: SetMaxHealth alone leaves UNIT_MOD_HEALTH
            // holding the old figure, and the next UpdateMaxHealth --
            // any aura application will do it -- reads it back and undoes this
            // (StatSystem.cpp:1070).
            c->SetCreateHealth(health);
            c->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
            c->SetMaxHealth(health);
            c->SetFullHealth();
        }

        if (damageMult > 0.0f && damageMult != 1.0f)
        {
            for (uint8 att = BASE_ATTACK; att < MAX_ATTACK; ++att)
            {
                WeaponAttackType const type = WeaponAttackType(att);

                float const minDamage = c->GetWeaponDamageRange(type, MINDAMAGE);
                float const maxDamage = c->GetWeaponDamageRange(type, MAXDAMAGE);

                // Zero means there is no such weapon rather than a weapon that
                // does nothing: GetWeaponDamageRange returns 0 for the offhand
                // of a creature that has none (Unit.cpp), and writing the
                // scaled zero back would erase what SelectLevel put there.
                if (minDamage <= 0.0f && maxDamage <= 0.0f)
                    continue;

                c->SetBaseWeaponDamage(type, MINDAMAGE, minDamage * damageMult);
                c->SetBaseWeaponDamage(type, MAXDAMAGE, maxDamage * damageMult);
                c->UpdateDamagePhysical(type);
            }
        }
    }
}
