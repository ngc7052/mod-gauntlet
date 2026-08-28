/*
 * mod-gauntlet - procedurally generated hardcore affix challenge for AzerothCore
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletRegistry.h"

#include <algorithm>
#include <unordered_map>

namespace
{
    using namespace Gauntlet;

    // The core's class ids and its 1 << (class - 1) mask convention, from
    // $CORE/src/server/shared/SharedDefines.h:126 (enum Classes) and :151
    // (CLASSMASK_WAND_USERS). SharedDefines.h is a game header and the
    // registry compiles without those, so the bits are spelled out here
    // rather than included.
    constexpr uint32 CM_WARRIOR      = 1u << (1 - 1);
    constexpr uint32 CM_PALADIN      = 1u << (2 - 1);
    constexpr uint32 CM_HUNTER       = 1u << (3 - 1);
    constexpr uint32 CM_ROGUE        = 1u << (4 - 1);
    constexpr uint32 CM_PRIEST       = 1u << (5 - 1);
    constexpr uint32 CM_DEATH_KNIGHT = 1u << (6 - 1);
    constexpr uint32 CM_SHAMAN       = 1u << (7 - 1);
    constexpr uint32 CM_MAGE         = 1u << (8 - 1);
    constexpr uint32 CM_WARLOCK      = 1u << (9 - 1);
    constexpr uint32 CM_DRUID        = 1u << (11 - 1);

    // Every class that spends mana in 3.3.5, which includes the hunter.
    // Faint (C41) is offered to "all mana users" and Blood Magic (A2) is
    // "only for mana users"; neither is CLASSMASK_WAND_USERS, which is the
    // narrower priest/mage/warlock set.
    constexpr uint32 CM_MANA_USERS = CM_PALADIN | CM_HUNTER | CM_PRIEST | CM_SHAMAN |
                                     CM_MAGE | CM_WARLOCK | CM_DRUID;

    // Cunning kicks a cast, so it needs a class with cast-time spells: every
    // class but the warrior, the rogue and the death knight, whose 3.3.5
    // kits are entirely instant. That happens to be the same set as above.
    constexpr uint32 CM_CAST_TIME = CM_MANA_USERS;

    // Death Rattle and Grudge are positional on-kill rules, so design section
    // 4.5 restricts them to the classes that finish a fight standing in it.
    constexpr uint32 CM_MELEE = CM_WARRIOR | CM_PALADIN | CM_ROGUE | CM_DEATH_KNIGHT |
                                CM_SHAMAN | CM_DRUID;

    // ------------------------------------------------------------------
    // The table.
    //
    // Ids are stable forever and are never reused: a stored gauntlet_affix
    // row from any past run must still resolve to the same mechanic, so a
    // retired mechanic keeps its id and gains MF_NotImplemented rather than
    // leaving a hole for a later one to fill. The ranges are the design's:
    // S1-S5 = 1-5, E1-E8 = 6-13, T1-T5 = 14-18, A1-A4 = 19-22, R1-R3 = 23-25,
    // B1-B2 = 26-27, C1-C44 = 28-71, Withering = 72, Forgetful = 73.
    //
    // Only Exposed (21) and Feeble (22) lack MF_NotImplemented. Withering
    // (72) and Forgetful (73) do have working mechanics -- migrated runs
    // still carry them and must keep functioning -- but they are cut from
    // the pool (design section 5), and MF_NotImplemented is what the offer
    // builder filters on, so they carry it too. IsImplemented() is therefore
    // an "may this be offered" question, not "does code exist".
    //
    // Boons: the first eight values in Gauntlet.h are effect categories, and
    // BonusRegen is read here as the resource boon generally (rage, mana,
    // runic power, energy, soul shards). Phase 1 appended five more for the
    // cards those eight could not express -- BonusAvoidance, BonusCooldown,
    // BonusAbility, BonusPetDamage and SecondLife -- which is what closed the
    // eighteen rows that used to carry Boon::None with a TODO(design) beside
    // them. The five are fixed by this table and are never rolled; see the
    // note on the enum. What still has no boon here is R3 Iron Purse, whose
    // card names none at all, and the rows in families S, E, T and A whose
    // cards do not promise one.
    //
    // Where a boon is bespoke -- "Consecration doubled and halved",
    // "Polymorph is instant" -- the type says only that it is bespoke and the
    // comment above the row records the design's exact words, because the
    // blurb describes the curse and has nowhere to put the gift.
    //
    // requiresTree is almost always 0. A class curse is spec-gated only
    // where the card would otherwise be free for the other two trees --
    // the design's own "a curse that is cheap for this spec is not
    // offered" rule -- which is three rows: C18, C27 and C38. Everything
    // else in family C keys on a class resource or a trained spell that
    // every spec of that class owns, and requiresSpell already covers it.
    // ------------------------------------------------------------------
    std::vector<MechanicDef> BuildTable()
    {
        return {

        // --- Family S: things that spawn -------------------------------
        { 1, "shade", "The Shade", Family::Spawn, 0, 4, 16, 3,
          MF_Timed | MF_Stalker | MF_NotImplemented, "stalker", Boon::BonusExperience, 0, 0,
          "A Shade rises behind you every few minutes and hunts you until you kill it or leave it behind." },

        { 2, "echo", "Echo", Family::Spawn, 0, 6, 14, 3,
          MF_Timed | MF_Stalker | MF_RewardShaped | MF_NotImplemented, "stalker", Boon::BonusExperience, 0, 0,
          "Every 25th enemy you kill returns as an echo of yourself." },

        { 3, "carrion", "Carrion", Family::Spawn, 0, 1, 10, 3,
          MF_Timed | MF_OnKill | MF_RewardShaped | MF_NotImplemented, "", Boon::BonusMoney, 0, 0,
          "Every 4th corpse you loot draws scavengers." },

        { 4, "reinforcements", "Reinforcements", Family::Spawn, 0, 5, 14, 3,
          MF_Timed | MF_NotImplemented, "", Boon::None, 0, 0,
          "Fights longer than 30 seconds draw another enemy every 15 seconds." },

        { 5, "ambush", "Ambush", Family::Spawn, 0, 3, 9, 3,
          MF_Timed | MF_NotImplemented, "", Boon::None, 0, 0,
          "Resting in the wild attracts an ambush." },

        // --- Family E: enemies that behave differently -----------------
        { 6, "champions", "Champions", Family::Enemy, 0, 1, 16, 3,
          MF_RewardShaped | MF_NotImplemented, "", Boon::BonusExperience, 0, 0,
          "Every 8th fight you start opens against a Champion: twice the health, harder hits, double the reward." },

        { 7, "craven", "Craven", Family::Enemy, 0, 4, 12, 3,
          MF_NotImplemented, "", Boon::None, 0, 0,
          "Enemies flee at 25% health, and come back with friends." },

        { 8, "call_to_arms", "Call to Arms", Family::Enemy, 0, 5, 13, 3,
          MF_OnKill | MF_NotImplemented, "", Boon::None, 0, 0,
          "Killing an enemy alerts its nearest kin." },

        { 9, "death_rattle", "Death Rattle", Family::Enemy, CM_MELEE, 4, 12, 3,
          MF_Timed | MF_OnKill | MF_NotImplemented, "onkill-positional", Boon::None, 0, 0,
          "Corpses burst two seconds after death, hurting anyone within five yards." },

        { 10, "grudge", "Grudge", Family::Enemy, CM_MELEE, 3, 10, 3,
          MF_OnKill | MF_NotImplemented, "onkill-positional", Boon::None, 0, 0,
          "The dead linger: standing where an enemy died saps you." },

        { 11, "nimble", "Nimble", Family::Enemy, 0, 6, 14, 3,
          MF_NotImplemented, "", Boon::None, 0, 0,
          "Enemies move 30% faster." },

        { 12, "cunning", "Cunning", Family::Enemy, CM_CAST_TIME, 6, 14, 3,
          MF_RoleTax | MF_NotImplemented, "roletax", Boon::None, 0, 0,
          "Enemies in melee range kick the spell you are casting, once every 12 seconds each." },

        { 13, "keen_nosed", "Keen-nosed", Family::Enemy, 0, 3, 11, 3,
          MF_NotImplemented, "", Boon::None, 0, 0,
          "Enemies notice you from further away." },

        // --- Family T: tempo and position ------------------------------
        // TODO(design): the card only floats the boon -- "a small speed reward for a
        // clean dodge is worth testing" -- so BonusMoveSpeed is a reading, not a value.
        { 14, "falling_sky", "Falling Sky", Family::Tempo, 0, 5, 16, 3,
          MF_Timed | MF_NotImplemented, "", Boon::BonusMoveSpeed, 0, 0,
          "In combat, every 20 seconds the sky marks your spot; three seconds later it strikes." },

        { 15, "frenzy", "Frenzy", Family::Tempo, 0, 3, 16, 3,
          MF_RewardShaped | MF_NotImplemented, "", Boon::BonusDamage, 0, 0,
          "Each kill within 8 seconds of the last stacks Frenzy: +6% damage dealt and +6% damage taken per stack." },

        { 16, "overextended", "Overextended", Family::Tempo, 0, 3, 12, 3,
          MF_NotImplemented, "", Boon::None, 0, 0,
          "Each enemy attacking you beyond the first increases the damage you take by 20%." },

        { 17, "falter", "Falter", Family::Tempo, 0, 5, 13, 3,
          MF_Timed | MF_RoleTax | MF_NotImplemented, "roletax", Boon::None, 0, 0,
          "Every 45 seconds in combat your hands fail you for three seconds." },

        { 18, "hubris", "Hubris", Family::Tempo, 0, 1, 10, 3,
          MF_RewardShaped | MF_NotImplemented, "", Boon::BonusExperience, 0, 0,
          "Enemies below your level give no experience; enemies above give 40% more." },

        // --- Family A: attrition with counterplay -----------------------
        { 19, "deep_wounds", "Deep Wounds", Family::Attrition, 0, 4, 12, 3,
          MF_NotImplemented, "", Boon::None, 0, 0,
          "A third of the damage you take becomes a wound that only rest can heal." },

        { 20, "blood_magic", "Blood Magic", Family::Attrition, CM_MANA_USERS, 5, 12, 3,
          MF_NotImplemented, "", Boon::BonusDamage, 0, 0,
          "Spells cost 3% of your maximum health in addition to mana." },

        // Exposed and Feeble are the only two mechanics Phase 0 offers. Both
        // are scalars: the generator draws their condition from the condition
        // axis and their boon with it, which is why the boon here is None
        // rather than a fixed type (design section 5, "always a trade").
        // TODO(design): no tier range on the card; section 4.6 puts conditional
        // Exposed at tiers 1-2 and both are evergreen, so the window is 1-16.
        { 21, "exposed", "Exposed", Family::Attrition, 0, 1, 16, 3,
          MF_Scalar, "", Boon::None, 0, 0,
          "You take more damage." },

        // TODO(design): as Exposed -- no tier range on the card, evergreen here.
        { 22, "feeble", "Feeble", Family::Attrition, 0, 1, 16, 3,
          MF_Scalar, "", Boon::None, 0, 0,
          "You deal less damage." },

        // --- Family R: rules -------------------------------------------
        { 23, "self_found", "Self-found", Family::Rules, 0, 1, 4, 1,
          MF_NotImplemented, "rule", Boon::BonusMoney, 0, 0,
          "You cannot trade, mail, or use the auction house." },

        { 24, "lone_wolf", "Lone Wolf", Family::Rules, 0, 1, 6, 1,
          MF_NotImplemented, "rule", Boon::BonusExperience, 0, 0,
          "You cannot join a group." },

        // TODO(design): the family says rules always come with a boon; this card names none.
        { 25, "iron_purse", "Iron Purse", Family::Rules, 0, 1, 3, 1,
          MF_NotImplemented, "rule", Boon::None, 0, 0,
          "Repairs cost double." },

        // --- Family B: bargains ----------------------------------------
        // The boon is the cheat death itself: a killing blow leaves you at one
        // health, once per level, and the Mark that follows is the price.
        { 26, "last_rites", "Last Rites", Family::Bargain, 0, 8, 16, 3,
          MF_RewardShaped | MF_NotImplemented, "", Boon::SecondLife, 0, 0,
          "Once per level, a killing blow leaves you at 1 health instead." },

        // The card's window is tiers 4-14; the family rule that bargains are
        // only offered from tier 6 (sections 3 and 4.6) is the offer builder's
        // to apply, and is not folded into minTier here.
        { 27, "cursed_hoard", "Cursed Hoard", Family::Bargain, 0, 4, 14, 3,
          MF_RewardShaped | MF_NotImplemented, "", Boon::BonusMoney, 0, 0,
          "Chests hold twice the loot, but opening one curses you until you kill three enemies." },

        // --- Family C: class curses ------------------------------------
        // Every row carries the "classcurse" token: section 4.1 allows one
        // class curse per run. The shortcut tokens beside it are the design's
        // "never pay twice" pairs, kept explicit so they survive a change to
        // that cap.

        // Warrior
        { 28, "c01_red_mist", "Red Mist", Family::Class, CM_WARRIOR, 3, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 0, 0,
          "At 100 rage you lose your mind for three seconds and your rage empties." },

        // TODO(design): requiresSpell picks Shield Wall of the card's three panic buttons.
        { 29, "c02_berserkers_bargain", "Berserker's Bargain", Family::Class, CM_WARRIOR, 5, 16, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 871, 0,
          "Below 35% health you deal 25% more damage, but your panic buttons will not answer." },

        // TODO(design): requiresSpell is Defensive Stance, the second stance a warrior trains.
        { 30, "c03_iron_discipline", "Iron Discipline", Family::Class, CM_WARRIOR, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 71, 0,
          "Changing stance has a ten-second cooldown." },

        { 31, "c04_deafening_roar", "Deafening Roar", Family::Class, CM_WARRIOR, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 0, 0,
          "Your shouts wake every enemy within thirty yards." },

        // Paladin
        { 32, "c05_long_forbearance", "Long Forbearance", Family::Class, CM_PALADIN, 3, 14, 3,
          MF_NotImplemented, "classcurse|shortcut:divine-shield", Boon::BonusRegen, 642, 0,
          "Forbearance lasts three minutes, and Divine Shield empties your mana." },

        // The boon is bespoke: Consecration lasts twice as long and costs half.
        { 33, "c06_consecrated_ground", "Consecrated Ground", Family::Class, CM_PALADIN, 5, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 26573, 0,
          "You take 25% more damage while not standing in your own Consecration." },

        // The boon takes a minute off Divine Shield's cooldown.
        { 34, "c07_no_sanctuary", "No Sanctuary", Family::Class, CM_PALADIN, 3, 12, 3,
          MF_NotImplemented, "classcurse|shortcut:divine-shield", Boon::BonusCooldown, 642, 0,
          "Your Hearthstone will not answer under Divine Shield." },

        // The boon halves Hammer of Justice's cooldown.
        { 35, "c08_commitment", "Commitment", Family::Class, CM_PALADIN, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusCooldown, 853, 0,
          "Hammer of Justice roots you for its duration." },

        // Hunter
        // The card's boon is "happy pets deal +10% damage" -- the pet's damage,
        // not the hunter's. BonusDamage was wrong for it in a way that would
        // have paid out: GauntletAggregate reads that value as DamageDone and
        // would have handed the bonus to the player.
        { 36, "c09_half_tamed", "Half-Tamed", Family::Class, CM_HUNTER, 3, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusPetDamage, 883, 0,
          "An unhappy pet turns on you." },

        // The boon halves Disengage's cooldown.
        { 37, "c10_dead_weight", "Dead Weight", Family::Class, CM_HUNTER, 4, 16, 3,
          MF_NotImplemented, "classcurse|shortcut:kiting", Boon::BonusCooldown, 5384, 0,
          "Feign Death has a three-minute cooldown." },

        { 38, "c11_wide_dead_zone", "Wide Dead Zone", Family::Class, CM_HUNTER, 4, 14, 3,
          MF_NotImplemented, "classcurse|shortcut:kiting", Boon::BonusDamage, 0, 0,
          "Ranged attacks cannot be used within ten yards." },

        { 39, "c12_blood_bond", "Blood Bond", Family::Class, CM_HUNTER, 5, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusHealing, 883, 0,
          "A fifth of the damage your pet takes is dealt to you." },

        // Rogue
        // The boon halves Sprint's cooldown.
        { 40, "c13_cold_trail", "Cold Trail", Family::Class, CM_ROGUE, 4, 16, 3,
          MF_NotImplemented, "classcurse", Boon::BonusCooldown, 1856, 0,
          "Vanish has a ten-minute cooldown." },

        // TODO(design): rogue poisons have no spell id in the plan's Appendix A, so no gate.
        { 41, "c14_poisoned_blades", "Poisoned Blades", Family::Class, CM_ROGUE, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "A quarter of the poison damage you deal ticks on you as well." },

        // The boon is +5% dodge.
        { 42, "c15_exposed_back", "Exposed Back", Family::Class, CM_ROGUE, 3, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAvoidance, 0, 0,
          "Attacks from behind you deal 50% more damage." },

        { 43, "c16_slow_hands", "Slow Hands", Family::Class, CM_ROGUE, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 0, 0,
          "Energy does not regenerate while you move in combat." },

        // Priest
        { 44, "c17_frail_soul", "Frail Soul", Family::Class, CM_PRIEST, 3, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusHealing, 17, 0,
          "Weakened Soul lasts 30 seconds." },

        // TODO(design): tree 3 = Shadow. Reading: the card is Shadowform, which is a
        // 30-point Shadow talent, so only a shadow priest can ever feel it. The design
        // names this exact case -- "Faithless Form for a priest without Shadowform" is
        // the example it gives of a curse that must not be offered.
        { 45, "c18_faithless_form", "Faithless Form", Family::Class, CM_PRIEST, 6, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 15473, 3,
          "Leaving Shadowform has a thirty-second cooldown." },

        { 46, "c19_penance_of_silence", "Penance of Silence", Family::Class, CM_PRIEST, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusHealing, 0, 0,
          "Healing yourself silences you for two seconds." },

        // The boon halves Fear Ward's cooldown.
        { 47, "c20_whispers_of_the_deep", "Whispers of the Deep", Family::Class, CM_PRIEST, 5, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusCooldown, 0, 0,
          "Below 20% health you lose your mind and flee for three seconds, once per fight." },

        // Death Knight
        { 48, "c21_rune_starved", "Rune-starved", Family::Class, CM_DEATH_KNIGHT, 12, 16, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 0, 0,
          "While all six runes are on cooldown you take 30% more damage." },

        // The boon halves Raise Dead's cooldown.
        { 49, "c22_grave_call", "Grave Call", Family::Class, CM_DEATH_KNIGHT, 12, 16, 3,
          MF_NotImplemented, "classcurse", Boon::BonusCooldown, 46584, 0,
          "The dead you do not claim rise against you." },

        // The boon is bespoke: every presence is 25% stronger. requiresSpell is
        // Frost Presence, the second presence a death knight trains.
        { 50, "c23_cold_presence", "Cold Presence", Family::Class, CM_DEATH_KNIGHT, 12, 16, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 48263, 0,
          "Changing presence costs all your runic power and has a ten-second cooldown." },

        // The boon is bespoke: both wards last half again as long. requiresSpell
        // is Icebound Fortitude, the later of the two the card shares a
        // cooldown between.
        { 51, "c24_one_ward", "One Ward", Family::Class, CM_DEATH_KNIGHT, 12, 16, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 48792, 0,
          "Anti-Magic Shell and Icebound Fortitude share a cooldown." },

        // Shaman
        // The boon is bespoke: the one totem still standing lasts twice as long.
        { 52, "c25_one_totem", "One Totem", Family::Class, CM_SHAMAN, 3, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 0, 0,
          "Only one totem may stand at a time." },

        // The boon is bespoke: your totems' effects are 30% stronger.
        { 53, "c26_totemic_anchor", "Totemic Anchor", Family::Class, CM_SHAMAN, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 0, 0,
          "You take 30% more damage when more than fifteen yards from your totems." },

        // TODO(design): tree 1 = Elemental. Reading: the card taxes casting the same
        // spell twice and its counterplay is "weaving Bolt, shock, Lava Burst and totem
        // drops" -- Lava Burst is the Elemental 51-pointer, and an enhancement shaman
        // alternates anyway, so the curse would be free for them.
        { 54, "c27_elemental_overload", "Elemental Overload", Family::Class, CM_SHAMAN, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 1,
          "Casting the same spell twice in a row costs double." },

        // The boon is bespoke: both shields carry three more charges.
        { 55, "c28_spirit_debt", "Spirit Debt", Family::Class, CM_SHAMAN, 5, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 324, 0,
          "Every hit consumes a shield charge, and each consumed charge costs you 2% health." },

        // Mage
        // The boon takes a quarter off Frost Nova's cooldown.
        { 56, "c29_cold_feet", "Cold Feet", Family::Class, CM_MAGE, 3, 16, 3,
          MF_NotImplemented, "classcurse", Boon::BonusCooldown, 1953, 0,
          "Blink costs 15% of your maximum health." },

        // The boon is bespoke: Polymorph becomes instant.
        { 57, "c30_fickle_sheep", "Fickle Sheep", Family::Class, CM_MAGE, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 118, 0,
          "Polymorph breaks after five seconds, and the sheep comes back angry." },

        { 58, "c31_mana_burn", "Mana Burn", Family::Class, CM_MAGE, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "Half the damage you take also burns your mana." },

        { 59, "c32_arcane_frailty", "Arcane Frailty", Family::Class, CM_MAGE, 6, 16, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "Thirty percent less health, thirty percent more spell damage." },

        // Warlock
        // As Half-Tamed: the card's +10% is the demon's damage, not yours.
        { 60, "c33_fel_pact", "Fel Pact", Family::Class, CM_WARLOCK, 4, 16, 3,
          MF_NotImplemented, "classcurse", Boon::BonusPetDamage, 0, 0,
          "Your demon's binding frays with every kill it makes, and after twenty it turns on you." },

        { 61, "c34_affliction_of_the_self", "Affliction of the Self", Family::Class, CM_WARLOCK, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "Your curses and corruption afflict you too, at a fifth of their strength." },

        { 62, "c35_shard_economy", "Shard Economy", Family::Class, CM_WARLOCK, 4, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 0, 0,
          "Every summon and every Healthstone costs a Soul Shard, and shards drop only from your level up." },

        // The +40% the blurb names is the demon's, so the boon is its damage.
        { 63, "c36_shared_blood", "Shared Blood", Family::Class, CM_WARLOCK, 5, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusPetDamage, 0, 0,
          "While your demon lives you take 25% more damage, and it deals 40% more." },

        // Druid
        { 64, "c37_bound_skin", "Bound Skin", Family::Class, CM_DRUID, 3, 14, 3,
          MF_NotImplemented, "classcurse|shortcut:shapeshift", Boon::BonusMaxHealth, 0, 0,
          "Shapeshifting has a six-second cooldown." },

        // TODO(design): tree 2 = Feral Combat. Reading: the curse only fires on a kill
        // made in Cat or Bear and its boon is "+10% feral damage", so a balance or
        // restoration druid would carry it for free.
        { 65, "c38_natures_toll", "Nature's Toll", Family::Class, CM_DRUID, 4, 14, 3,
          MF_NotImplemented, "classcurse|shortcut:shapeshift", Boon::BonusDamage, 0, 2,
          "Every kill made as a beast leaves you bleeding until you calm." },

        // TODO(design): the card gives no severity ladder, so this is binary at rank 1.
        { 66, "c39_commitment_of_roots", "Commitment of Roots", Family::Class, CM_DRUID, 3, 12, 1,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 339, 0,
          "Entangling Roots holds you as long as it holds them." },

        { 67, "c40_two_faces", "Two Faces", Family::Class, CM_DRUID, 3, 12, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "By day your spells are weaker; by night your claws are." },

        // Every class
        { 68, "c41_faint", "Faint", Family::Class, CM_MANA_USERS, 3, 14, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 0, 0,
          "When your mana hits zero in combat you black out for two seconds." },

        { 69, "c42_unspent", "Unspent", Family::Class, 0, 2, 8, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "You receive a talent point every second level, and each unspent point makes you 2% stronger." },

        // Class bargains. The design files these under family C but says they
        // follow family B's rules -- from tier 8, once per run -- so they are
        // Class here and reward-shaped for the offer builder's guarantee.
        // The boon is the second life: Reincarnation answers once, and burns
        // away every boon the run carries when it does.
        { 70, "c43_ankh_pact", "Ankh Pact", Family::Class, CM_SHAMAN, 8, 16, 3,
          MF_RewardShaped | MF_NotImplemented, "classcurse", Boon::SecondLife, 20608, 0,
          "Reincarnation works once in this run, and when it does every boon you carry is burned away." },

        // The boon is the second life, as C43. TODO(design): the card gives no
        // ladder of its own, saying only "as C43", so maxRank follows Ankh
        // Pact's three.
        { 71, "c44_stone_of_the_damned", "Stone of the Damned", Family::Class, CM_WARLOCK, 8, 16, 3,
          MF_RewardShaped | MF_NotImplemented, "classcurse", Boon::SecondLife, 693, 0,
          "A Soulstone will bring you back once, and whoever kills you will be waiting." },

        // --- Legacy scalars, kept for migrated runs only -----------------
        // Cut from the pool by design section 5 but still implemented, so a
        // run that already carries one keeps working. TODO(design): neither
        // has a design letter or a tier window; Attrition and 1-16 are chosen
        // because that is where the other two legacy scalars sit and because
        // the values are inert while MF_NotImplemented holds.
        { 72, "withering", "Withering", Family::Attrition, 0, 1, 16, 3,
          MF_Scalar | MF_NotImplemented, "", Boon::None, 0, 0,
          "Healing on you is less effective." },

        { 73, "forgetful", "Forgetful", Family::Attrition, 0, 1, 16, 3,
          MF_Scalar | MF_NotImplemented, "", Boon::None, 0, 0,
          "You gain less experience." },

        };
    }

    // The table and its two indices, built once on first use. FindMechanic by
    // id sits on the damage path, so neither lookup scans the 73 rows.
    struct Registry
    {
        std::vector<MechanicDef>                                 table;
        std::vector<MechanicDef const*>                          byId;
        std::unordered_map<std::string_view, MechanicDef const*> byKey;

        Registry() : table(BuildTable())
        {
            uint16 highest = 0;
            for (MechanicDef const& def : table)
                highest = std::max(highest, def.id);

            byId.assign(static_cast<size_t>(highest) + 1, nullptr);
            for (MechanicDef const& def : table)
            {
                byId[def.id] = &def;
                byKey.emplace(std::string_view(def.key), &def);
            }
        }
    };

    Registry const& Table()
    {
        static Registry const registry;
        return registry;
    }
}

namespace Gauntlet
{
    MechanicDef const* FindMechanic(uint16 id)
    {
        Registry const& reg = Table();
        return id < reg.byId.size() ? reg.byId[id] : nullptr;
    }

    MechanicDef const* FindMechanic(std::string_view key)
    {
        Registry const& reg = Table();
        auto const it = reg.byKey.find(key);
        return it != reg.byKey.end() ? it->second : nullptr;
    }

    std::vector<MechanicDef> const& AllMechanics()
    {
        return Table().table;
    }

    bool IsImplemented(MechanicDef const& def)
    {
        return (def.flags & MF_NotImplemented) == 0;
    }
}
