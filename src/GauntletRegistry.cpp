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
    // Ids are stable forever and are never reused: a stored gauntlet_affix row
    // from any past run must still resolve to the same mechanic. A retired
    // mechanic therefore leaves a hole rather than being renumbered, and
    // nothing may ever fill one. The ranges are the design's: S1-S5 = 1-5,
    // E1-E8 = 6-13, T1-T5 = 14-18, A1-A4 = 19-22, R1-R3 = 23-25, B1-B2 = 26-27,
    // C1-C44 = 28-71. Ids 21, 22, 72 and 73 -- Exposed, Feeble, Withering and
    // Forgetful -- were the four flat scalars and were deleted in Phase 2, so
    // 69 rows carry 71 ids and the table is not contiguous.
    //
    // Twenty-three rows lack MF_NotImplemented: Phase 1's vertical slice --
    // The Shade (1), Champions (6), Falling Sky (14) and Deep Wounds (19) --
    // and Phase 2's fifteen, which is everything left in families S, E and T.
    // What still carries the flag is families R, B and C, which are Phase 3
    // and Phase 4.
    //
    // Clearing the flag is the last switch of a phase and nothing else: it is
    // what puts a mechanic into a live player's offers, so it is thrown only
    // once the dispatch behind it is wired and the file is anchored in
    // GauntletScripts.cpp. tests/compile-check.sh audits the second half of
    // that; RegistryTest's OFFERABLE list audits the first.
    //
    // Boons: the first eight values in Gauntlet.h are effect categories, and
    // BonusRegen is read here as the resource boon generally (rage, mana,
    // runic power, energy, soul shards). Phase 1 appended five more for the
    // cards those eight could not express -- BonusAvoidance, BonusCooldown,
    // BonusAbility, BonusPetDamage and SecondLife.
    //
    // Every implemented row now names one, and the mechanic behind it pays it:
    // with the scalars gone there is no generically-rolled boon left anywhere,
    // so GauntletAggregate.cpp pays none of them and each mechanic answers for
    // its own (see src/mechanics/Boons.h). Eleven of the fifteen Phase 2 cards
    // name no boon at all, so those are chosen and each is justified in
    // docs/phase-2-report.md; four -- Echo, Carrion, Frenzy and Hubris -- have
    // the boon written into the card itself. What still has no boon here is
    // R3 Iron Purse, whose card names none, and A1 Deep Wounds, whose card is
    // pure attrition.
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
        { 1, "shade", "The Shade", Family::Spawn, 0, 20, 80, 3,
          MF_Timed | MF_Stalker, "stalker", Boon::BonusExperience, 0, 0,
          "A Shade rises behind you every few minutes and hunts you until you kill it or leave it behind." },

        { 2, "echo", "Echo", Family::Spawn, 0, 30, 70, 3,
          MF_Timed | MF_Stalker | MF_RewardShaped, "stalker", Boon::BonusExperience, 0, 0,
          "Every 25th enemy you kill returns as an echo of yourself." },

        { 3, "carrion", "Carrion", Family::Spawn, 0, 1, 50, 3,
          MF_Timed | MF_OnKill | MF_RewardShaped, "", Boon::BonusMoney, 0, 0,
          "Every 4th corpse you loot draws scavengers." },

        { 4, "reinforcements", "Reinforcements", Family::Spawn, 0, 25, 70, 3,
          MF_Timed, "", Boon::BonusDamage, 0, 0,
          "Fights longer than 30 seconds draw another enemy every 15 seconds." },

        // TODO(design): the card says tiers 3-9 and section 4.6 puts Ambush in
        // the 3-5 band. Moved to 2. Phase 2 deleted the conditional Exposed
        // that band named, and the Rules family that would have filled the gap
        // is Phase 3, so tiers 1-2 were left with exactly one mechanic per
        // family and no variety at all. The band's own rule for what may be
        // there is "no interrupts, silences or stalkers": Ambush is none of the
        // three -- it is not one of section 4.1's stalkers, which is S1 xor S2
        // and is what the registry's "stalker" exclusive key holds -- and its
        // counterplay is standing up and moving three steps, which needs no
        // button, no cooldown and no crowd control. Tier 2 is level 10.
        { 5, "ambush", "Ambush", Family::Spawn, 0, 4, 45, 3,
          MF_Timed | MF_Stalker, "", Boon::BonusMaxHealth, 0, 0,
          "Resting in the wild attracts an ambush." },

        // --- Family E: enemies that behave differently -----------------
        { 6, "champions", "Champions", Family::Enemy, 0, 1, 80, 3,
          MF_RewardShaped, "", Boon::BonusExperience, 0, 0,
          "Every 8th fight you start opens against a Champion: twice the health, harder hits, double the reward." },

        { 7, "craven", "Craven", Family::Enemy, 0, 12, 60, 3,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "Enemies flee at 25% health, and come back with friends." },

        { 8, "call_to_arms", "Call to Arms", Family::Enemy, 0, 25, 65, 3,
          MF_OnKill, "", Boon::BonusExperience, 0, 0,
          "Killing an enemy alerts its nearest kin." },

        { 9, "death_rattle", "Death Rattle", Family::Enemy, CM_MELEE, 20, 60, 3,
          MF_Timed | MF_OnKill, "onkill-positional", Boon::BonusMoney, 0, 0,
          "Corpses burst two seconds after death, hurting anyone within five yards." },

        { 10, "grudge", "Grudge", Family::Enemy, CM_MELEE, 8, 50, 3,
          MF_OnKill, "onkill-positional", Boon::BonusHealing, 0, 0,
          "Everything you kill leaves a ghost on its corpse that drains your health"
          " while you stand near it." },

        { 11, "nimble", "Nimble", Family::Enemy, 0, 30, 70, 3,
          MF_None, "", Boon::BonusMaxHealth, 0, 0,
          "Enemies move 30% faster." },

        { 12, "cunning", "Cunning", Family::Enemy, CM_CAST_TIME, 30, 70, 3,
          MF_RoleTax, "roletax", Boon::BonusDamage, 0, 0,
          "Enemies in melee range kick the spell you are casting, once every 12 seconds each." },

        // TODO(design): the card says tiers 3-11. Moved to 2, for the reason
        // above and on the same test: Keen-nosed is a routing rule, answered by
        // hugging the edge of a path and pulling singles, and a level-10
        // character can do both.
        { 13, "keen_nosed", "Keen-nosed", Family::Enemy, 0, 4, 55, 3,
          MF_None, "", Boon::BonusMoney, 0, 0,
          "Enemies notice you from further away." },

        // --- Family T: tempo and position ------------------------------
        // TODO(design): the card only floats the boon -- "a small speed reward for a
        // clean dodge is worth testing" -- so BonusMoveSpeed is a reading, not a value.
        { 14, "falling_sky", "Falling Sky", Family::Tempo, 0, 25, 80, 3,
          MF_Timed, "", Boon::BonusMoveSpeed, 0, 0,
          "In combat, every 20 seconds the sky marks your spot; three seconds later it strikes." },

        { 15, "frenzy", "Frenzy", Family::Tempo, 0, 8, 80, 3,
          MF_RewardShaped, "", Boon::BonusDamage, 0, 0,
          "Each kill within 8 seconds of the last stacks Frenzy: +6% damage dealt and +6% damage taken per stack." },

        // TODO(design): the card says tiers 3-12. Moved to 1, and this is the
        // largest of the three window changes. Section 4.6 gives the tiers 1-2
        // band "conditional Exposed" -- a scalar the player can play around --
        // and Phase 2 deleted it; section 5 names Overextended, in the same
        // sentence as Frenzy, as the shape a scalar takes when it earns its
        // place ("count-based, not flat ... scalars whose value the player
        // controls each pull"). So this is what fills the hole the deletion
        // left, it is the design's own candidate for it, and its counterplay --
        // pull one at a time -- needs no button at all.
        { 16, "overextended", "Overextended", Family::Tempo, 0, 1, 60, 3,
          MF_None, "", Boon::BonusHealing, 0, 0,
          "Each enemy attacking you beyond the first increases the damage you take by 20%." },

        { 17, "falter", "Falter", Family::Tempo, 0, 25, 65, 3,
          MF_Timed | MF_RoleTax, "roletax", Boon::BonusMaxHealth, 0, 0,
          "Every 45 seconds in combat your hands fail you for three seconds." },

        { 18, "hubris", "Hubris", Family::Tempo, 0, 1, 50, 3,
          MF_RewardShaped, "", Boon::BonusExperience, 0, 0,
          "Enemies below your level give no experience; enemies above give 40% more." },

        // --- Family A: attrition with counterplay -----------------------
        // TODO(design): the card says tiers 4-12; section 4.6's tier table puts
        // Deep Wounds in the 3-5 band, and the two disagree. The band wins
        // here, because tier 3 is the one tier where only three families exist
        // and every one of them is thin -- the Attrition family is empty until
        // this row opens, and with it open tier 3 has four families like every
        // tier above it. Nothing about the mechanic changes: it is answered by
        // taking less damage, which a level-15 character can already do.
        { 19, "deep_wounds", "Deep Wounds", Family::Attrition, 0, 10, 60, 3,
          MF_None, "", Boon::None, 0, 0,
          "A third of the damage you take becomes a wound that only rest can heal." },

        { 20, "blood_magic", "Blood Magic", Family::Attrition, CM_MANA_USERS, 25, 60, 3,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "Spells cost 3% of your maximum health in addition to mana." },

        // Ids 21 and 22 were Exposed and Feeble, the last two flat scalars, and
        // 72 and 73 were Withering and Forgetful. All four were deleted in
        // Phase 2 (see docs/phase-2-report.md): a coefficient with a condition
        // bolted on is still a tax, which is the shape the whole redesign
        // exists to remove. None of the four ids is ever reused, so the table
        // is no longer contiguous and nothing may fill the holes.

        // --- Family R: rules -------------------------------------------
        { 23, "self_found", "Self-found", Family::Rules, 0, 1, 20, 1,
          MF_None, "rule", Boon::BonusMoney, 0, 0,
          "You cannot trade, mail, or use the auction house." },

        // The blurb is not the card's, and the mechanic is not the card's
        // either: Phase 3 decision 1 replaced "you cannot join a group" with a
        // price paid only while in one. The reasoning is at the head of
        // mechanics/rules/LoneWolf.cpp; the short version is that this row's
        // window is levels 5-30, so the card does not make the levelling
        // dungeons harder, it deletes them for the rest of the run.
        { 24, "lone_wolf", "Lone Wolf", Family::Rules, 0, 1, 30, 1,
          MF_None, "rule", Boon::BonusExperience, 0, 0,
          "Half health in a group; more experience alone." },

        // TODO(design): the family says rules always come with a boon; this
        // card names none, and it is left that way on purpose. The obvious
        // boon for a gold tax is more gold, which would make the affix a wash
        // -- a curse and a boon that cancel are worse than either alone. See
        // the head of mechanics/rules/IronPurse.cpp for why this row is kept
        // anyway despite being the weakest in the table.
        { 25, "iron_purse", "Iron Purse", Family::Rules, 0, 1, 15, 1,
          MF_None, "rule", Boon::None, 0, 0,
          "Repairs cost double." },

        // --- Family B: bargains ----------------------------------------
        // The boon is the cheat death itself: a killing blow leaves you at one
        // health, once per level, and the Mark that follows is the price.
        { 26, "last_rites", "Last Rites", Family::Bargain, 0, 40, 80, 3,
          MF_RewardShaped, "", Boon::SecondLife, 0, 0,
          "A hit that would kill you leaves you at 1 health instead, once per level." },

        // The card's window is tiers 4-14; the family rule that bargains are
        // only offered from tier 6 (sections 3 and 4.6) is the offer builder's
        // to apply, and is not folded into minTier here.
        // Tier 6 and not the card's 4. The design's family-B header says
        // bargains open at tier 6 and the generator has enforced that with
        // BARGAIN_MIN_TIER since Phase 0, so the card's window was four tiers
        // of dead letter: the row said 4, the constant said 6, and the
        // constant won every time. Registry.BargainsOpenWhereTheGeneratorSaysThey
        // Do keeps the two from drifting apart again.
        { 27, "cursed_hoard", "Cursed Hoard", Family::Bargain, 0, 30, 70, 3,
          MF_RewardShaped, "", Boon::BonusMoney, 0, 0,
          "Chests give twice as much loot, but opening one makes you take triple damage"
          " until you kill three enemies." },

        // --- Family C: class curses ------------------------------------
        // Every row carries the "classcurse" token: section 4.1 allows one
        // class curse per run. The shortcut tokens beside it are the design's
        // "never pay twice" pairs, kept explicit so they survive a change to
        // that cap.

        // Warrior
        { 28, "c01_red_mist", "Red Mist", Family::Class, CM_WARRIOR, 15, 70, 3,
          MF_None, "classcurse", Boon::BonusRegen, 0, 0,
          "At 100 rage you lose your mind for three seconds and your rage empties." },

        // TODO(design): requiresSpell picks Shield Wall of the card's three panic buttons.
        { 29, "c02_berserkers_bargain", "Berserker's Bargain", Family::Class, CM_WARRIOR, 25, 80, 3,
          MF_RewardShaped, "classcurse", Boon::BonusDamage, 871, 0,
          "Below 35% health you deal 25% more damage, but your panic buttons will not answer." },

        // TODO(design): requiresSpell is Defensive Stance, the second stance a warrior trains.
        { 30, "c03_iron_discipline", "Iron Discipline", Family::Class, CM_WARRIOR, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 71, 0,
          "Changing stance has a ten-second cooldown." },

        // Boon changed from BonusRegen in Phase 4. The card's boon is "shouts
        // free and long", which is a bespoke upside to named abilities and is
        // exactly what Boon::BonusAbility is for; BonusRegen would have had the
        // offer promise a regeneration percentage the mechanic never pays.
        { 31, "c04_deafening_roar", "Deafening Roar", Family::Class, CM_WARRIOR, 20, 70, 3,
          MF_None, "classcurse", Boon::BonusAbility, 0, 0,
          "Your shouts wake every enemy within thirty yards." },

        // Paladin
        // Boon changed from BonusRegen in Phase 4: the card's upside is "Holy
        // Light 10% cheaper", which is bespoke to one named ability rather than
        // a regeneration rate, and BonusRegen would have laddered it 15/30/45%
        // against a card that states one number.
        { 32, "c05_long_forbearance", "Long Forbearance", Family::Class, CM_PALADIN, 15, 70, 3,
          MF_None, "classcurse|shortcut:divine-shield", Boon::BonusAbility, 642, 0,
          "Forbearance lasts three minutes, and Divine Shield empties your mana." },

        // The boon is bespoke: Consecration lasts twice as long and costs half.
        { 33, "c06_consecrated_ground", "Consecrated Ground", Family::Class, CM_PALADIN, 25, 70, 3,
          MF_None, "classcurse", Boon::BonusAbility, 26573, 0,
          "You take 25% more damage while not standing in your own Consecration." },

        // The boon takes a minute off Divine Shield's cooldown.
        { 34, "c07_no_sanctuary", "No Sanctuary", Family::Class, CM_PALADIN, 15, 60, 3,
          MF_NotImplemented, "classcurse|shortcut:divine-shield", Boon::BonusCooldown, 642, 0,
          "Your Hearthstone will not answer under Divine Shield." },

        // The boon halves Hammer of Justice's cooldown.
        { 35, "c08_commitment", "Commitment", Family::Class, CM_PALADIN, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusCooldown, 853, 0,
          "Hammer of Justice roots you for its duration." },

        // Hunter
        // The card's boon is "happy pets deal +10% damage" -- the pet's damage,
        // not the hunter's. BonusDamage was wrong for it in a way that would
        // have paid out: GauntletAggregate reads that value as DamageDone and
        // would have handed the bonus to the player.
        { 36, "c09_half_tamed", "Half-Tamed", Family::Class, CM_HUNTER, 15, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusPetDamage, 883, 0,
          "An unhappy pet turns on you." },

        // The boon halves Disengage's cooldown.
        { 37, "c10_dead_weight", "Dead Weight", Family::Class, CM_HUNTER, 20, 80, 3,
          MF_NotImplemented, "classcurse|shortcut:kiting", Boon::BonusCooldown, 5384, 0,
          "Feign Death has a three-minute cooldown." },

        { 38, "c11_wide_dead_zone", "Wide Dead Zone", Family::Class, CM_HUNTER, 20, 70, 3,
          MF_NotImplemented, "classcurse|shortcut:kiting", Boon::BonusDamage, 0, 0,
          "Ranged attacks cannot be used within ten yards." },

        { 39, "c12_blood_bond", "Blood Bond", Family::Class, CM_HUNTER, 25, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusHealing, 883, 0,
          "A fifth of the damage your pet takes is dealt to you." },

        // Rogue
        // The boon halves Sprint's cooldown.
        { 40, "c13_cold_trail", "Cold Trail", Family::Class, CM_ROGUE, 20, 80, 3,
          MF_NotImplemented, "classcurse", Boon::BonusCooldown, 1856, 0,
          "Vanish has a ten-minute cooldown." },

        // TODO(design): rogue poisons have no spell id in the plan's Appendix A, so no gate.
        { 41, "c14_poisoned_blades", "Poisoned Blades", Family::Class, CM_ROGUE, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "A quarter of the poison damage you deal ticks on you as well." },

        // The boon is +5% dodge.
        { 42, "c15_exposed_back", "Exposed Back", Family::Class, CM_ROGUE, 15, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAvoidance, 0, 0,
          "Attacks from behind you deal 50% more damage." },

        { 43, "c16_slow_hands", "Slow Hands", Family::Class, CM_ROGUE, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 0, 0,
          "Energy does not regenerate while you move in combat." },

        // Priest
        { 44, "c17_frail_soul", "Frail Soul", Family::Class, CM_PRIEST, 15, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusHealing, 17, 0,
          "Weakened Soul lasts 30 seconds." },

        // TODO(design): tree 3 = Shadow. Reading: the card is Shadowform, which is a
        // 30-point Shadow talent, so only a shadow priest can ever feel it. The design
        // names this exact case -- "Faithless Form for a priest without Shadowform" is
        // the example it gives of a curse that must not be offered.
        { 45, "c18_faithless_form", "Faithless Form", Family::Class, CM_PRIEST, 30, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 15473, 3,
          "Leaving Shadowform has a thirty-second cooldown." },

        { 46, "c19_penance_of_silence", "Penance of Silence", Family::Class, CM_PRIEST, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusHealing, 0, 0,
          "Healing yourself silences you for two seconds." },

        // The boon halves Fear Ward's cooldown.
        { 47, "c20_whispers_of_the_deep", "Whispers of the Deep", Family::Class, CM_PRIEST, 25, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusCooldown, 0, 0,
          "Below 20% health you lose your mind and flee for three seconds, once per fight." },

        // Death Knight
        { 48, "c21_rune_starved", "Rune-starved", Family::Class, CM_DEATH_KNIGHT, 60, 80, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 0, 0,
          "While all six runes are on cooldown you take 30% more damage." },

        // The boon halves Raise Dead's cooldown.
        { 49, "c22_grave_call", "Grave Call", Family::Class, CM_DEATH_KNIGHT, 60, 80, 3,
          MF_NotImplemented, "classcurse", Boon::BonusCooldown, 46584, 0,
          "The dead you do not claim rise against you." },

        // The boon is bespoke: every presence is 25% stronger. requiresSpell is
        // Frost Presence, the second presence a death knight trains.
        { 50, "c23_cold_presence", "Cold Presence", Family::Class, CM_DEATH_KNIGHT, 60, 80, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 48263, 0,
          "Changing presence costs all your runic power and has a ten-second cooldown." },

        // The boon is bespoke: both wards last half again as long. requiresSpell
        // is Icebound Fortitude, the later of the two the card shares a
        // cooldown between.
        { 51, "c24_one_ward", "One Ward", Family::Class, CM_DEATH_KNIGHT, 60, 80, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 48792, 0,
          "Anti-Magic Shell and Icebound Fortitude share a cooldown." },

        // Shaman
        // The boon is bespoke: the one totem still standing lasts twice as long.
        { 52, "c25_one_totem", "One Totem", Family::Class, CM_SHAMAN, 15, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 0, 0,
          "Only one totem may stand at a time." },

        // The boon is bespoke: your totems' effects are 30% stronger.
        { 53, "c26_totemic_anchor", "Totemic Anchor", Family::Class, CM_SHAMAN, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 0, 0,
          "You take 30% more damage when more than fifteen yards from your totems." },

        // TODO(design): tree 1 = Elemental. Reading: the card taxes casting the same
        // spell twice and its counterplay is "weaving Bolt, shock, Lava Burst and totem
        // drops" -- Lava Burst is the Elemental 51-pointer, and an enhancement shaman
        // alternates anyway, so the curse would be free for them.
        { 54, "c27_elemental_overload", "Elemental Overload", Family::Class, CM_SHAMAN, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 1,
          "Casting the same spell twice in a row costs double." },

        // The boon is bespoke: both shields carry three more charges.
        { 55, "c28_spirit_debt", "Spirit Debt", Family::Class, CM_SHAMAN, 25, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 324, 0,
          "Every hit consumes a shield charge, and each consumed charge costs you 2% health." },

        // Mage
        // The boon takes a quarter off Frost Nova's cooldown.
        { 56, "c29_cold_feet", "Cold Feet", Family::Class, CM_MAGE, 15, 80, 3,
          MF_NotImplemented, "classcurse", Boon::BonusCooldown, 1953, 0,
          "Blink costs 15% of your maximum health." },

        // The boon is bespoke: Polymorph becomes instant.
        { 57, "c30_fickle_sheep", "Fickle Sheep", Family::Class, CM_MAGE, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusAbility, 118, 0,
          "Polymorph breaks after five seconds, and the sheep comes back angry." },

        { 58, "c31_mana_burn", "Mana Burn", Family::Class, CM_MAGE, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "Half the damage you take also burns your mana." },

        { 59, "c32_arcane_frailty", "Arcane Frailty", Family::Class, CM_MAGE, 30, 80, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "Thirty percent less health, thirty percent more spell damage." },

        // Warlock
        // As Half-Tamed: the card's +10% is the demon's damage, not yours.
        { 60, "c33_fel_pact", "Fel Pact", Family::Class, CM_WARLOCK, 20, 80, 3,
          MF_NotImplemented, "classcurse", Boon::BonusPetDamage, 0, 0,
          "Your demon's binding frays with every kill it makes, and after twenty it turns on you." },

        { 61, "c34_affliction_of_the_self", "Affliction of the Self", Family::Class, CM_WARLOCK, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "Your curses and corruption afflict you too, at a fifth of their strength." },

        { 62, "c35_shard_economy", "Shard Economy", Family::Class, CM_WARLOCK, 20, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 0, 0,
          "Every summon and every Healthstone costs a Soul Shard, and shards drop only from your level up." },

        // The +40% the blurb names is the demon's, so the boon is its damage.
        { 63, "c36_shared_blood", "Shared Blood", Family::Class, CM_WARLOCK, 25, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusPetDamage, 0, 0,
          "While your demon lives you take 25% more damage, and it deals 40% more." },

        // Druid
        { 64, "c37_bound_skin", "Bound Skin", Family::Class, CM_DRUID, 15, 70, 3,
          MF_NotImplemented, "classcurse|shortcut:shapeshift", Boon::BonusMaxHealth, 0, 0,
          "Shapeshifting has a six-second cooldown." },

        // TODO(design): tree 2 = Feral Combat. Reading: the curse only fires on a kill
        // made in Cat or Bear and its boon is "+10% feral damage", so a balance or
        // restoration druid would carry it for free.
        { 65, "c38_natures_toll", "Nature's Toll", Family::Class, CM_DRUID, 20, 70, 3,
          MF_NotImplemented, "classcurse|shortcut:shapeshift", Boon::BonusDamage, 0, 2,
          "Every kill made as a beast leaves you bleeding until you calm." },

        // TODO(design): the card gives no severity ladder, so this is binary at rank 1.
        { 66, "c39_commitment_of_roots", "Commitment of Roots", Family::Class, CM_DRUID, 15, 60, 1,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 339, 0,
          "Entangling Roots holds you as long as it holds them." },

        { 67, "c40_two_faces", "Two Faces", Family::Class, CM_DRUID, 15, 60, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "By day your spells are weaker; by night your claws are." },

        // Every class
        { 68, "c41_faint", "Faint", Family::Class, CM_MANA_USERS, 15, 70, 3,
          MF_NotImplemented, "classcurse", Boon::BonusRegen, 0, 0,
          "When your mana hits zero in combat you black out for two seconds." },

        { 69, "c42_unspent", "Unspent", Family::Class, 0, 10, 40, 3,
          MF_NotImplemented, "classcurse", Boon::BonusDamage, 0, 0,
          "You receive a talent point every second level, and each unspent point makes you 2% stronger." },

        // Class bargains. The design files these under family C but says they
        // follow family B's rules -- from tier 8, once per run -- so they are
        // Class here and reward-shaped for the offer builder's guarantee.
        // The boon is the second life: Reincarnation answers once, and burns
        // away every boon the run carries when it does.
        { 70, "c43_ankh_pact", "Ankh Pact", Family::Class, CM_SHAMAN, 40, 80, 3,
          MF_RewardShaped | MF_NotImplemented, "classcurse", Boon::SecondLife, 20608, 0,
          "Reincarnation works once in this run, and when it does every boon you carry is burned away." },

        // The boon is the second life, as C43. TODO(design): the card gives no
        // ladder of its own, saying only "as C43", so maxRank follows Ankh
        // Pact's three.
        { 71, "c44_stone_of_the_damned", "Stone of the Damned", Family::Class, CM_WARLOCK, 40, 80, 3,
          MF_RewardShaped | MF_NotImplemented, "classcurse", Boon::SecondLife, 693, 0,
          "A Soulstone will bring you back once, and whoever kills you will be waiting." },

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
