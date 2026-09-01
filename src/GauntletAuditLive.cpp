/*
 * mod-gauntlet - reading a live character's footprint
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAudit.h"

#include "GauntletMgr.h"
#include "GauntletScheduler.h"
#include "GauntletSummons.h"

#include "Player.h"
#include "SpellInfo.h"
#include "Unit.h"

#include <algorithm>

// The half of the audit that needs a world. It is a straight read -- nothing
// here writes anything, and that matters more than usual: Capture is called
// three times per mechanic, and a snapshot that perturbed what it measured
// would make every verdict after the first one a lie.

namespace Gauntlet
{
    Footprint Capture(Player* player, RunState const* run)
    {
        Footprint fp;
        if (!player)
            return fp;

        // GetAppliedAuras is a multimap keyed by spell id, so an aura applied
        // twice appears twice, which is exactly the multiset Diff wants.
        //
        // Except every aura whose spell requires an equipped item. Their
        // presence is the core's equipment bookkeeping, not the card's, and
        // it runs on *any* equip or unequip through Player::_ApplyItemMods ->
        // ApplyItemDependentAuras (Player.cpp:6770, 7267) -- in both
        // directions, and both were measured blaming the first denial in id
        // order:
        //
        //   - Unequip removes every self-cast aura whose requirement is unmet
        //     (RemoveItemDependentAurasAndCasts, :12875). The learn path casts
        //     such passives with the full trigger mask, so a troll with no bow
        //     carries Bow Specialization until the first time anything comes
        //     off; three trolls "leaked" 20558/26290 on Bareheaded. A timed
        //     buff that requires its item goes the same way -- Nat Pagle's
        //     Broken Reel's use effect (24610) went with the trinket Charmless
        //     put away, and a use effect cannot be cast again.
        //   - Equip adds every fitting passive the character does not have,
        //     talents included (:7267). A death knight had logged in without
        //     Two-Handed Weapon Specialization and Dark Conviction (55108,
        //     49480); the helm Bareheaded put back brought both, and the audit
        //     read "still applied".
        //
        // A helm taken off and put on by hand does exactly the same. Whether
        // the card gave the item back is read from the equipment slots below,
        // which is the direct question; these auras only ever answered it by
        // accident.
        for (auto const& applied : player->GetAppliedAuras())
        {
            if (applied.second->GetBase()->GetSpellInfo()->EquippedItemClass >= 0)
                continue;
            fp.auras.push_back(applied.first);
        }
        std::sort(fp.auras.begin(), fp.auras.end());

        // Ids only. The remaining duration is not recorded on purpose: it
        // decreases between two readings taken in the same command, so every
        // cooldown the character already had would report as a change.
        //
        // And not an item's. Equipping anything with an on-use effect puts
        // that effect on a thirty-second cooldown (Player::ApplyEquipCooldown,
        // Player.cpp:12008, the anti-swap rule), so a trinket a denial puts
        // back arrives with one -- Fezzik's Pocketwatch's 59658 read "still on
        // cooldown" after Charmless returned it, and would have after the
        // player re-equipped it by hand. The cards that hold cooldowns
        // (TimedLockout, PermanentCooldown) hold spells, and a spell cooldown
        // has no item behind it.
        for (auto const& cd : player->GetSpellCooldownMap())
            if (cd.second.itemid == 0)
                fp.cooldowns.push_back(cd.first);
        std::sort(fp.cooldowns.begin(), fp.cooldowns.end());

        // The guid, not the entry: two of the same ring are two items, and a
        // denial that put one away and brought the other back has not put
        // back what it took.
        fp.equipment.reserve(EQUIPMENT_SLOT_END);
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            fp.equipment.push_back(item ? item->GetGUID().GetCounter() : 0u);
        }

        fp.maxHealth   = player->GetMaxHealth();
        fp.maxPower    = player->GetMaxPower(player->getPowerType());
        fp.freeTalents = player->GetFreeTalentPoints();
        fp.shapeshift  = static_cast<uint8>(player->GetShapeshiftForm());
        fp.speedRun    = player->GetSpeedRate(MOVE_RUN);
        fp.speedSwim   = player->GetSpeedRate(MOVE_SWIM);
        fp.summons     = sGauntletSummons->AliveFor(player);
        fp.carried     = run ? static_cast<uint32>(run->affixes.size()) : 0u;

        if (Scheduler const* clock = sGauntlet->ClockFor(player))
            fp.armed = static_cast<uint32>(clock->Queue().size());

        // The products, not the raw factors: what the caps let through is what
        // the character actually lives with, and a mechanic that leaves a
        // factor behind under a cap that was already clamping is not leaking
        // anything a player could feel.
        for (uint8 k = 0; k < static_cast<uint8>(AggregateKind::MAX); ++k)
            fp.aggregate[k] = sGauntlet->Aggregate(player, static_cast<AggregateKind>(k));

        return fp;
    }
}
