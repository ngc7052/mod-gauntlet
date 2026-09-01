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
    // Forgetful -- were the four flat scalars and were deleted in Phase 2, and
    // 69 -- Unspent -- was deleted in Phase 6. 25 (Iron Purse) and 34 (No
    // Sanctuary) were retired on 2026-09-01 by docs/greed-redesign.md section
    // 3: the first was the table's weakest row by its own file's admission and
    // the rarity plan's commons took away its one structural argument, and the
    // second never acted at all -- "a card that never acts is
    // indistinguishable from a broken one, and it has nothing to become".
    // Neither id is ever reused.
    //
    // The design's ranges therefore stop describing the table from 74 onward.
    // A new mechanic takes the next free id and not the next id in its family's
    // band, because the band is a description of how the table was first laid
    // out and the no-reuse rule is a promise to every stored row ever written.
    // Killing Floor is an Attrition mechanic at 74, outside A1-A4's 19-22.
    //
    // No row carries MF_NotImplemented any more. Phase 1 brought four, Phase 2
    // fifteen, Phase 3 six across two new families, and Phase 4 finished family
    // C in two waves -- so every one of the sixty-nine rows has an
    // implementation behind it, and every one can be offered.
    //
    // The flag stays in the enum and IsImplemented stays a real test, because
    // the next row added will need both and because a run migrated from a
    // future registry can legitimately carry an id this build has no code for.
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
    //
    // Rarity: every row is Rare, by fiat and not by judgement. Step 1 of
    // docs/rarity-plan.md puts the column, the roll and the display in
    // place with the table changed in nothing else, so the mechanism is
    // proven before the first common is written and before any card is
    // promoted. Which of these sixty-nine are epics is that plan's section
    // 7.4 -- one pass with the whole list in front of you, not a decision
    // taken per row as each happens to be touched -- and
    // Registry.EveryCardIsRareUntilTheEpicPass holds the table to this
    // until that pass is made.
    // ------------------------------------------------------------------
    std::vector<MechanicDef> BuildTable()
    {
        return {

        // --- Family S: things that spawn -------------------------------
        { 1, "shade", "The Shade", Family::Spawn, 0, 20, 80, Rarity::Legendary,
          MF_Timed | MF_Stalker, "stalker", Boon::BonusExperience, 0, 0,
          "A Shade rises behind you every few minutes and hunts you until you kill it or leave it behind." },

        { 2, "echo", "Echo", Family::Spawn, 0, 30, 80, Rarity::Rare,
          MF_Timed | MF_Stalker | MF_RewardShaped, "stalker", Boon::BonusExperience, 0, 0,
          "Every 25th enemy you kill returns as an echo of yourself." },

        { 3, "carrion", "Carrion", Family::Spawn, 0, 1, 50, Rarity::Rare,
          MF_Timed | MF_OnKill | MF_RewardShaped, "", Boon::BonusMoveSpeed, 0, 0,
          "Every 4th corpse you loot draws scavengers." },

        { 4, "reinforcements", "Reinforcements", Family::Spawn, 0, 25, 80, Rarity::Rare,
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
        { 5, "ambush", "Ambush", Family::Spawn, 0, 4, 45, Rarity::Rare,
          MF_Timed | MF_Stalker, "", Boon::BonusMaxHealth, 0, 0,
          "Resting in the wild attracts an ambush." },

        // --- Family E: enemies that behave differently -----------------
        { 6, "champions", "Champions", Family::Enemy, 0, 1, 80, Rarity::Rare,
          MF_RewardShaped, "", Boon::BonusExperience, 0, 0,
          "Every 8th fight you start opens against a Champion: twice the health, harder hits, double the reward." },

        { 7, "craven", "Craven", Family::Enemy, 0, 12, 60, Rarity::Rare,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "Enemies flee at 25% health. Cut one down before it reaches its camp and it pays double." },

        { 8, "call_to_arms", "Call to Arms", Family::Enemy, 0, 25, 65, Rarity::Rare,
          MF_OnKill, "", Boon::BonusExperience, 0, 0,
          "Killing an enemy alerts its nearest kin. The kin that answer are worth more." },

        { 9, "death_rattle", "Death Rattle", Family::Enemy, CM_MELEE, 20, 60, Rarity::Rare,
          MF_Timed | MF_OnKill, "onkill-positional", Boon::BonusDamage, 0, 0,
          "Corpses burst two seconds after death, hurting anyone within five yards." },

        { 10, "grudge", "Grudge", Family::Enemy, CM_MELEE, 8, 50, Rarity::Rare,
          MF_OnKill, "onkill-positional", Boon::BonusMoveSpeed, 0, 0,
          "A spirit rises on each corpse four seconds after the kill. Loot it first and nothing rises." },

        { 11, "nimble", "Nimble", Family::Enemy, 0, 30, 80, Rarity::Rare,
          MF_None, "", Boon::BonusMoveSpeed, 0, 0,
          "Enemies move 30% faster." },

        { 12, "cunning", "Cunning", Family::Enemy, CM_CAST_TIME, 30, 80, Rarity::Rare,
          MF_RoleTax, "roletax", Boon::BonusDamage, 0, 0,
          "Enemies in melee range kick the spell you are casting, once every 12 seconds each." },

        // TODO(design): the card says tiers 3-11. Moved to 2, for the reason
        // above and on the same test: Keen-nosed is a routing rule, answered by
        // hugging the edge of a path and pulling singles, and a level-10
        // character can do both.
        { 13, "keen_nosed", "Keen-nosed", Family::Enemy, 0, 4, 55, Rarity::Rare,
          MF_None, "", Boon::BonusMoveSpeed, 0, 0,
          "Enemies notice you from further away." },

        // --- Family T: tempo and position ------------------------------
        // TODO(design): the card only floats the boon -- "a small speed reward for a
        // clean dodge is worth testing" -- so BonusMoveSpeed is a reading, not a value.
        { 14, "falling_sky", "Falling Sky", Family::Tempo, 0, 25, 80, Rarity::Rare,
          MF_Timed, "", Boon::BonusMoveSpeed, 0, 0,
          "Stand still in combat and the sky marks the ground under you. Keep moving." },

        { 15, "frenzy", "Frenzy", Family::Tempo, 0, 8, 80, Rarity::Rare,
          MF_RewardShaped, "", Boon::BonusDamage, 0, 0,
          "Each kill within 8 seconds stacks Frenzy: +6% damage dealt. Any damage taken breaks the chain." },

        // TODO(design): the card says tiers 3-12. Moved to 1, and this is the
        // largest of the three window changes. Section 4.6 gives the tiers 1-2
        // band "conditional Exposed" -- a scalar the player can play around --
        // and Phase 2 deleted it; section 5 names Overextended, in the same
        // sentence as Frenzy, as the shape a scalar takes when it earns its
        // place ("count-based, not flat ... scalars whose value the player
        // controls each pull"). So this is what fills the hole the deletion
        // left, it is the design's own candidate for it, and its counterplay --
        // pull one at a time -- needs no button at all.
        { 16, "overextended", "Overextended", Family::Tempo, 0, 1, 60, Rarity::Rare,
          MF_None, "", Boon::BonusHealing, 0, 0,
          "Anything hitting you from behind deals 30% more damage. Keep them in front of you." },

        { 17, "falter", "Falter", Family::Tempo, 0, 25, 65, Rarity::Rare,
          MF_Timed | MF_RoleTax, "roletax", Boon::BonusMaxHealth, 0, 0,
          "Every 45 seconds in combat your hands fail you for three seconds." },

        { 18, "hubris", "Hubris", Family::Tempo, 0, 1, 50, Rarity::Rare,
          MF_RewardShaped, "", Boon::BonusDamage, 0, 0,
          "The first enemy in a fight is your duel: it hurts you less, everything else more." },

        // --- Family A: attrition with counterplay -----------------------
        // TODO(design): the card says tiers 4-12; section 4.6's tier table puts
        // Deep Wounds in the 3-5 band, and the two disagree. The band wins
        // here, because tier 3 is the one tier where only three families exist
        // and every one of them is thin -- the Attrition family is empty until
        // this row opens, and with it open tier 3 has four families like every
        // tier above it. Nothing about the mechanic changes: it is answered by
        // taking less damage, which a level-15 character can already do.
        { 19, "deep_wounds", "Deep Wounds", Family::Attrition, 0, 10, 60, Rarity::Epic,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "A third of the damage you take becomes a wound. Only a kill closes one." },

        { 20, "blood_magic", "Blood Magic", Family::Attrition, CM_MANA_USERS, 25, 60, Rarity::Rare,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "Spells cost 3% of your maximum health. Below a third, they cost nothing and hit harder." },


        // Ids 21 and 22 were Exposed and Feeble, the last two flat scalars, and
        // 72 and 73 were Withering and Forgetful. All four were deleted in
        // Phase 2 (see docs/phase-2-report.md): a coefficient with a condition
        // bolted on is still a tax, which is the shape the whole redesign
        // exists to remove. None of the four ids is ever reused, so the table
        // is no longer contiguous and nothing may fill the holes.

        // --- Family R: rules -------------------------------------------
        // Epic, and its window widened from 1-20 to 1-60 to go with it. The
        // two are one decision: RollRarity draws a slot's rarity from the
        // per-tier weights, and Rules::EPIC_PCT is zero below tier 21 -- so an
        // epic whose window ends at 20 is a card the generator can never draw,
        // and promoting it on its own would have deleted it from the game
        // silently. Every promotion in docs/rarity-plan.md 7.4's pass was
        // checked against that, and this is the only row it caught.
        //
        // What the widening costs is the card's "decide at the start"
        // framing: it is a mid-run commitment now rather than an opening one.
        // What it buys is the card existing.
        { 23, "self_found", "Self-found", Family::Rules, 0, 1, 60, Rarity::Epic,
          MF_None, "rule", Boon::BonusDamage, 0, 0,
          "You cannot trade, mail, or use the auction house." },

        // The blurb is not the card's, and the mechanic is not the card's
        // either: Phase 3 decision 1 replaced "you cannot join a group" with a
        // price paid only while in one. The reasoning is at the head of
        // mechanics/rules/LoneWolf.cpp; the short version is that this row's
        // window is levels 5-30, so the card does not make the levelling
        // dungeons harder, it deletes them for the rest of the run.
        { 24, "lone_wolf", "Lone Wolf", Family::Rules, 0, 1, 30, Rarity::Rare,
          MF_None, "rule", Boon::BonusExperience, 0, 0,
          "Half health in a group; more experience alone." },


        // --- Family B: bargains ----------------------------------------
        // The boon is the cheat death itself: a killing blow leaves you at one
        // health, once per level, and the Mark that follows is the price.
        { 26, "last_rites", "Last Rites", Family::Bargain, 0, 40, 80, Rarity::Legendary,
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
        { 27, "cursed_hoard", "Cursed Hoard", Family::Bargain, 0, 30, 80, Rarity::Epic,
          MF_RewardShaped, "", Boon::BonusDamage, 0, 0,
          "Chests give twice as much loot, but opening one makes you take triple damage"
          " until you kill three enemies." },

        // --- Family C: class curses ------------------------------------
        // Every row carries the "" token: section 4.1 allows one
        // class curse per run. The shortcut tokens beside it are the design's
        // "never pay twice" pairs, kept explicit so they survive a change to
        // that cap.

        // Warrior
        { 28, "c01_red_mist", "Red Mist", Family::Class, CM_WARRIOR, 15, 80, Rarity::Rare,
          MF_None, "", Boon::BonusRegen, 0, 0,
          "At 100 rage you lose your mind for three seconds and your rage empties." },

        // TODO(design): requiresSpell picks Shield Wall of the card's three panic buttons.
        { 29, "c02_berserkers_bargain", "Berserker's Bargain", Family::Class, CM_WARRIOR, 25, 80, Rarity::Epic,
          MF_RewardShaped, "", Boon::BonusDamage, 871, 0,
          "Below 35% health you deal 25% more damage, but your panic buttons will not answer." },

        // TODO(design): requiresSpell is Defensive Stance, the second stance a warrior trains.
        { 30, "c03_iron_discipline", "Iron Discipline", Family::Class, CM_WARRIOR, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusRegen, 71, 0,
          "Changing stance has a ten-second cooldown." },

        // Boon changed from BonusRegen in Phase 4. The card's boon is "shouts
        // free and long", which is a bespoke upside to named abilities and is
        // exactly what Boon::BonusAbility is for; BonusRegen would have had the
        // offer promise a regeneration percentage the mechanic never pays.
        // requiresSpell is Battle Shout, and it is a truthful gate rather than a
        // convenience: a warrior who has trained no shout cannot trigger this
        // card at all, so offering it to them is offering a blank. It is also
        // what lets `.gauntlet debug bench` drive the card -- the bench casts
        // whatever spell a row declares, and a row declaring none is a row no
        // probe can reach.
        { 31, "c04_deafening_roar", "Deafening Roar", Family::Class, CM_WARRIOR, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusAbility, 6673, 0,
          "Your shouts wake every enemy within thirty yards." },

        // Paladin
        // Boon changed from BonusRegen in Phase 4: the card's upside is "Holy
        // Light 10% cheaper", which is bespoke to one named ability rather than
        // a regeneration rate, and BonusRegen would have laddered it 15/30/45%
        // against a card that states one number.
        { 32, "c05_long_forbearance", "Long Forbearance", Family::Class, CM_PALADIN, 15, 80, Rarity::Rare,
          MF_None, "shortcut:divine-shield", Boon::BonusAbility, 642, 0,
          "Forbearance lasts three minutes, and Divine Shield empties your mana." },

        // The boon is bespoke: Consecration lasts twice as long and costs half.
        { 33, "c06_consecrated_ground", "Consecrated Ground", Family::Class, CM_PALADIN, 25, 80, Rarity::Epic,
          MF_None, "", Boon::BonusAbility, 26573, 0,
          "You take 25% more damage while not standing in your own Consecration." },


        // The boon halves Hammer of Justice's cooldown.
        { 35, "c08_commitment", "Commitment", Family::Class, CM_PALADIN, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusCooldown, 853, 0,
          "Hammer of Justice roots you for its duration." },

        // Hunter
        // The card's boon is "happy pets deal +10% damage" -- the pet's damage,
        // not the hunter's. BonusDamage was wrong for it in a way that would
        // have paid out: GauntletAggregate reads that value as DamageDone and
        // would have handed the bonus to the player.
        { 36, "c09_half_tamed", "Half-Tamed", Family::Class, CM_HUNTER, 15, 80, Rarity::Rare,
          MF_None, "", Boon::BonusPetDamage, 883, 0,
          "An unhappy pet turns on you." },

        // The boon halves Disengage's cooldown.
        { 37, "c10_dead_weight", "Dead Weight", Family::Class, CM_HUNTER, 20, 80, Rarity::Rare,
          MF_None, "shortcut:kiting", Boon::BonusCooldown, 5384, 0,
          "Feign Death has a three-minute cooldown." },

        { 38, "c11_wide_dead_zone", "Wide Dead Zone", Family::Class, CM_HUNTER, 20, 80, Rarity::Epic,
          MF_None, "shortcut:kiting", Boon::BonusDamage, 0, 0,
          "Ranged attacks cannot be used within ten yards." },

        { 39, "c12_blood_bond", "Blood Bond", Family::Class, CM_HUNTER, 25, 80, Rarity::Rare,
          MF_None, "", Boon::BonusHealing, 883, 0,
          "A fifth of the damage your pet takes is dealt to you." },

        // Rogue
        // The boon halves Sprint's cooldown.
        { 40, "c13_cold_trail", "Cold Trail", Family::Class, CM_ROGUE, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusCooldown, 1856, 0,
          "Vanish has a ten-minute cooldown." },

        // TODO(design): rogue poisons have no spell id in the plan's Appendix A, so no gate.
        { 41, "c14_poisoned_blades", "Poisoned Blades", Family::Class, CM_ROGUE, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "A quarter of the poison damage you deal ticks on you as well." },

        // The boon is +5% dodge.
        { 42, "c15_exposed_back", "Exposed Back", Family::Class, CM_ROGUE, 15, 80, Rarity::Rare,
          MF_None, "", Boon::BonusAvoidance, 0, 0,
          "Attacks from behind you deal 50% more damage." },

        { 43, "c16_slow_hands", "Slow Hands", Family::Class, CM_ROGUE, 20, 80, Rarity::Epic,
          MF_None, "", Boon::BonusRegen, 0, 0,
          "Energy does not regenerate while you move in combat." },

        // Priest
        { 44, "c17_frail_soul", "Frail Soul", Family::Class, CM_PRIEST, 15, 80, Rarity::Rare,
          MF_None, "", Boon::BonusHealing, 17, 0,
          "Weakened Soul lasts 30 seconds." },

        // TODO(design): tree 3 = Shadow. Reading: the card is Shadowform, which is a
        // 30-point Shadow talent, so only a shadow priest can ever feel it. The design
        // names this exact case -- "Faithless Form for a priest without Shadowform" is
        // the example it gives of a curse that must not be offered.
        { 45, "c18_faithless_form", "Faithless Form", Family::Class, CM_PRIEST, 30, 80, Rarity::Rare,
          MF_None, "", Boon::BonusDamage, 15473, 3,
          "Leaving Shadowform has a thirty-second cooldown." },

        { 46, "c19_penance_of_silence", "Penance of Silence", Family::Class, CM_PRIEST, 20, 80, Rarity::Epic,
          MF_None, "", Boon::BonusHealing, 0, 0,
          "Healing yourself silences you for two seconds." },

        // The boon halves Fear Ward's cooldown.
        { 47, "c20_whispers_of_the_deep", "Whispers of the Deep", Family::Class, CM_PRIEST, 25, 80, Rarity::Rare,
          MF_None, "", Boon::BonusCooldown, 0, 0,
          "Below 20% health you lose your mind and flee for three seconds, once per fight." },

        // Death Knight
        { 48, "c21_rune_starved", "Rune-starved", Family::Class, CM_DEATH_KNIGHT, 60, 80, Rarity::Rare,
          MF_None, "", Boon::BonusRegen, 0, 0,
          "While all six runes are on cooldown you take 30% more damage." },

        // The boon halves Raise Dead's cooldown.
        { 49, "c22_grave_call", "Grave Call", Family::Class, CM_DEATH_KNIGHT, 60, 80, Rarity::Rare,
          MF_None, "", Boon::BonusCooldown, 46584, 0,
          "The dead you do not claim rise against you." },

        // The boon is bespoke: every presence is 25% stronger. requiresSpell is
        // Frost Presence, the second presence a death knight trains.
        { 50, "c23_cold_presence", "Cold Presence", Family::Class, CM_DEATH_KNIGHT, 60, 80, Rarity::Rare,
          MF_None, "", Boon::BonusAbility, 48263, 0,
          "Changing presence costs all your runic power and has a ten-second cooldown." },

        // The boon is bespoke: both wards last half again as long. requiresSpell
        // is Icebound Fortitude, the later of the two the card shares a
        // cooldown between.
        { 51, "c24_one_ward", "One Ward", Family::Class, CM_DEATH_KNIGHT, 60, 80, Rarity::Epic,
          MF_None, "", Boon::BonusAbility, 48792, 0,
          "Anti-Magic Shell and Icebound Fortitude share a cooldown." },

        // Shaman
        // The boon is bespoke: the one totem still standing lasts twice as long.
        { 52, "c25_one_totem", "One Totem", Family::Class, CM_SHAMAN, 15, 80, Rarity::Epic,
          MF_None, "", Boon::BonusAbility, 0, 0,
          "Only one totem may stand at a time." },

        // The boon is bespoke: your totems' effects are 30% stronger.
        { 53, "c26_totemic_anchor", "Totemic Anchor", Family::Class, CM_SHAMAN, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusAbility, 0, 0,
          "You take 30% more damage when more than fifteen yards from your totems." },

        // TODO(design): tree 1 = Elemental. Reading: the card taxes casting the same
        // spell twice and its counterplay is "weaving Bolt, shock, Lava Burst and totem
        // drops" -- Lava Burst is the Elemental 51-pointer, and an enhancement shaman
        // alternates anyway, so the curse would be free for them.
        { 54, "c27_elemental_overload", "Elemental Overload", Family::Class, CM_SHAMAN, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusDamage, 0, 1,
          "Casting the same spell twice in a row costs double." },

        // The boon is bespoke: both shields carry three more charges.
        { 55, "c28_spirit_debt", "Spirit Debt", Family::Class, CM_SHAMAN, 25, 80, Rarity::Rare,
          MF_None, "", Boon::BonusAbility, 324, 0,
          "Every hit consumes a shield charge, and each consumed charge costs you 2% health." },

        // Mage
        // The boon takes a quarter off Frost Nova's cooldown.
        { 56, "c29_cold_feet", "Cold Feet", Family::Class, CM_MAGE, 15, 80, Rarity::Rare,
          MF_None, "", Boon::BonusCooldown, 1953, 0,
          "Blink costs 15% of your maximum health." },

        // The boon is bespoke: Polymorph becomes instant.
        { 57, "c30_fickle_sheep", "Fickle Sheep", Family::Class, CM_MAGE, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusAbility, 118, 0,
          "Polymorph breaks after five seconds, and the sheep comes back angry." },

        { 58, "c31_mana_burn", "Mana Burn", Family::Class, CM_MAGE, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "Half the damage you take also burns your mana." },

        { 59, "c32_arcane_frailty", "Arcane Frailty", Family::Class, CM_MAGE, 30, 80, Rarity::Epic,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "Thirty percent less health, thirty percent more spell damage." },

        // Warlock
        // As Half-Tamed: the card's +10% is the demon's damage, not yours.
        { 60, "c33_fel_pact", "Fel Pact", Family::Class, CM_WARLOCK, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusPetDamage, 0, 0,
          "Your demon's binding frays with every kill it makes, and after twenty it turns on you." },

        { 61, "c34_affliction_of_the_self", "Affliction of the Self", Family::Class, CM_WARLOCK, 20, 80, Rarity::Rare,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "Your curses and corruption afflict you too, at a fifth of their strength." },

        { 62, "c35_shard_economy", "Shard Economy", Family::Class, CM_WARLOCK, 20, 80, Rarity::Epic,
          MF_None, "", Boon::BonusRegen, 0, 0,
          "Every summon and every Healthstone costs a Soul Shard, and shards drop only from your level up." },

        // The +40% the blurb names is the demon's, so the boon is its damage.
        { 63, "c36_shared_blood", "Shared Blood", Family::Class, CM_WARLOCK, 25, 80, Rarity::Rare,
          MF_None, "", Boon::BonusPetDamage, 0, 0,
          "While your demon lives you take 25% more damage, and it deals 40% more." },

        // Druid
        { 64, "c37_bound_skin", "Bound Skin", Family::Class, CM_DRUID, 15, 80, Rarity::Epic,
          MF_None, "shortcut:shapeshift", Boon::BonusMaxHealth, 0, 0,
          "Shapeshifting has a six-second cooldown." },

        // TODO(design): tree 2 = Feral Combat. Reading: the curse only fires on a kill
        // made in Cat or Bear and its boon is "+10% feral damage", so a balance or
        // restoration druid would carry it for free.
        { 65, "c38_natures_toll", "Nature's Toll", Family::Class, CM_DRUID, 20, 80, Rarity::Rare,
          MF_None, "shortcut:shapeshift", Boon::BonusDamage, 0, 2,
          "Every kill made as a beast leaves you bleeding until you calm." },

        { 66, "c39_commitment_of_roots", "Commitment of Roots", Family::Class, CM_DRUID, 15, 60, Rarity::Rare,
          MF_None, "", Boon::BonusRegen, 339, 0,
          "Entangling Roots holds you as long as it holds them." },

        { 67, "c40_two_faces", "Two Faces", Family::Class, CM_DRUID, 15, 60, Rarity::Rare,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "By day your spells are weaker; by night your claws are." },

        // Every class
        { 68, "c41_faint", "Faint", Family::Class, CM_MANA_USERS, 15, 80, Rarity::Rare,
          MF_None, "", Boon::BonusRegen, 0, 0,
          "When your mana hits zero in combat you black out for two seconds." },

        // Class bargains. The design files these under family C but says they
        // follow family B's rules -- from tier 8, once per run -- so they are
        // Class here and reward-shaped for the offer builder's guarantee.
        // The boon is the second life: Reincarnation answers once, and burns
        // away every boon the run carries when it does.
        { 70, "c43_ankh_pact", "Ankh Pact", Family::Class, CM_SHAMAN, 40, 80, Rarity::Rare,
          MF_RewardShaped, "", Boon::SecondLife, 20608, 0,
          "Reincarnation works once in this run, and when it does every boon you carry is burned away." },

        // The boon is the second life, as C43.
        { 71, "c44_stone_of_the_damned", "Stone of the Damned", Family::Class, CM_WARLOCK, 40, 80, Rarity::Rare,
          MF_RewardShaped, "", Boon::SecondLife, 693, 0,
          "A Soulstone will bring you back once, and whoever kills you will be waiting." },

        // ---------------------------------------------------------------
        // A5 Killing Floor, at the end of the table because the table is in
        // ascending id order and 74 is the next free id -- not next to its own
        // family's rows, where a reader would look for it.
        //
        // 21 and 22 are Exposed's and Feeble's and are spent forever, so the
        // design's A1-A4 band has no room left. The band describes how the
        // table was first laid out; the no-reuse rule outranks it, because a
        // stored gauntlet_affix row from any past run must still resolve to the
        // mechanic it named.
        //
        // It replaces Unspent (69) in the table and not its number. The case is
        // in docs/unspent-replacement-plan.md -- the short version is that the
        // module had four generally-available MF_RewardShaped rows for eighty
        // tiers, two of which expire at 50, and one more classless row with the
        // flag measured as worth more than twenty-one more curses.
        //
        // Boon::None on purpose. The kill burst is the upside and it is the
        // other half of the curse's own sentence, so a BoonClause would promise
        // a second one. Frenzy and Berserker's Bargain are written the same way.
        //
        // Not MF_OnKill: that flag is "fires on a kill, and counts against
        // CAP_ON_KILL", and the curse here is the continuous healing block. The
        // kill is the release.
        // ---------------------------------------------------------------
        { 74, "a05_killing_floor", "Killing Floor", Family::Attrition, 0, 10, 80, Rarity::Epic,
          MF_RewardShaped, "", Boon::None, 0, 0,
          "Healing is held while something you have wounded lives. A kill hands it back." },

        // ---------------------------------------------------------------
        // The commons (docs/rarity-plan.md section 2): one small trade each,
        // a single axis, no state, readable in one line. Every row from here
        // on is backed by the same class -- SimpleTrade, in
        // src/mechanics/common/ -- reading its own line of the table in
        // src/GauntletTrades.h, which is where the curse's shape, the noun a
        // refusal names and the boon's magnitude live. A common is a table
        // row, not a file, and adding one is a row here and a line there.
        //
        // Filed by lever, as every row is: a denial is a Rule -- "a
        // restriction on what you're allowed to do rather than a number" --
        // and a coefficient trade is Attrition. Spread over two families on
        // purpose: the offer builder still wants three distinct families in a
        // set, and a family holding every common could put only one in it.
        //
        // No exclusive key. The Rules rows' "rule" is the design's one-rule-
        // per-run cap and these are not that kind of rule; whether the
        // commons want a cap of their own is the plan's open question 7.1.
        //
        // maxRank 1: a common has no ladder. The boon column is a promise the
        // trade line pays; BoonTable reads the magnitude from that line so the
        // offer card and the payout cannot disagree.
        //
        // Class masks on the two weapon denials are the classes that can hold
        // the weapon at all in 3.3.5 -- a mage denied axes has lost nothing --
        // and the generator's relevance discount halves their boon, because
        // within the mask a shaman who fights with maces has lost nothing
        // either.
        // ---------------------------------------------------------------
        { 75, "bareheaded", "Bareheaded", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusMaxHealth, 0, 0,
          "You cannot wear a helm." },

        { 76, "cloakless", "Cloakless", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusMoveSpeed, 0, 0,
          "You cannot wear a cloak." },

        { 77, "ringless", "Ringless", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusExperience, 0, 0,
          "You cannot wear rings." },

        { 78, "charmless", "Charmless", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "You cannot carry a trinket." },

        { 79, "bare_necked", "Bare-necked", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusHealing, 0, 0,
          "You cannot wear anything at your neck." },

        { 80, "axeless", "Axeless", Family::Rules,
          CM_WARRIOR | CM_PALADIN | CM_HUNTER | CM_SHAMAN | CM_DEATH_KNIGHT, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "You cannot wield an axe." },

        { 81, "swordless", "Swordless", Family::Rules,
          CM_WARRIOR | CM_PALADIN | CM_HUNTER | CM_ROGUE | CM_MAGE | CM_WARLOCK | CM_DEATH_KNIGHT, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "You cannot wield a sword." },

        { 82, "glass", "Glass", Family::Attrition, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "You take 10% more damage." },

        { 83, "frail", "Frail", Family::Attrition, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusExperience, 0, 0,
          "You have 10% less health." },

        { 84, "thin_blood", "Thin Blood", Family::Attrition, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "Healing on you is 15% weaker." },

        // The first cards paid in loot (docs/greed-redesign.md section 7.2).
        // Loot is the accelerant WoW's own players chase hardest, and none of
        // the older boons was it.
        { 85, "magpie", "Magpie", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusLoot, 0, 0,
          "You cannot wear a belt." },

        { 86, "butterfingers", "Butterfingers", Family::Attrition, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusLoot, 0, 0,
          "You deal 8% less damage." },

        // The first Uncommon: a trade with a condition, the shape
        // docs/rarity-plan.md section 2 gives that tier. The condition is the
        // trade line's (GauntletTrades.h), not rolled.
        { 87, "night_owl", "Night Owl", Family::Attrition, 0, 1, 80, Rarity::Uncommon,
          MF_None, "", Boon::BonusLoot, 0, 0,
          "By night you take 10% more damage." },

        // ---------------------------------------------------------------
        // The three reward-shaped cards of docs/commons.md.
        //
        // Every offer set must contain one row flagged MF_RewardShaped
        // (GauntletGenerator.cpp:810), and until these three, every row that
        // carried the flag was Rare -- at tier 1, filtered by window and
        // class mask, exactly three existed for an arbitrary character.
        // One slot in three was a rare before any rarity weight was read,
        // and the variant sweep showed no quantity of ordinary commons
        // moving it: fifty-two of them left tier 1 at 54% against a 70%
        // target, while three rows like these moved it further than twenty
        // ordinary ones did.
        //
        // So all three are classless and open at tier 1 on purpose -- that
        // availability is the whole point of them -- and each earns the flag
        // rather than being handed it: the card's own mechanic pays out when
        // the player engages with it, the standard Champions and Killing
        // Floor set. Two carry Boon::None for Killing Floor's reason, that
        // the payout is the upside and a BoonClause would promise a second.
        // ---------------------------------------------------------------
        { 88, "scavengers_eye", "Scavenger's Eye", Family::Enemy, 0, 1, 80, Rarity::Uncommon,
          MF_RewardShaped, "", Boon::None, 0, 0,
          "Enemies notice you from further away, and a fight nothing touches you in pays twice." },

        { 89, "blood_for_bread", "Blood for Bread", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_RewardShaped, "", Boon::None, 0, 0,
          "You cannot eat or drink. Every kill feeds you instead." },

        { 90, "waste_not", "Waste Not", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_RewardShaped, "", Boon::None, 0, 0,
          "You cannot drink a potion. Every kill mends you instead." },

        // The uncommon tier proper: nine trades with a condition
        // (docs/commons.md section 3.2). The tier had one card before them,
        // and the condition is the whole of what makes a trade an uncommon.

        { 91, "sunstruck", "Sunstruck", Family::Attrition, 0, 1, 80, Rarity::Uncommon,
          MF_None, "", Boon::BonusLoot, 0, 0,
          "By day you take 10% more damage." },

        { 92, "skittish", "Skittish", Family::Tempo, 0, 1, 80, Rarity::Uncommon,
          MF_None, "", Boon::BonusMoveSpeed, 0, 0,
          "While you are moving you take 15% more damage." },

        { 93, "rooted", "Rooted", Family::Tempo, 0, 1, 80, Rarity::Uncommon,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "While you stand still you take 15% more damage." },

        { 94, "saddle_sore", "Saddle-sore", Family::Tempo, 0, 1, 80, Rarity::Uncommon,
          MF_None, "", Boon::BonusMoveSpeed, 0, 0,
          "While you are mounted you take 25% more damage." },

        { 95, "crowd_shy", "Crowd-shy", Family::Rules, 0, 1, 80, Rarity::Uncommon,
          MF_None, "", Boon::BonusExperience, 0, 0,
          "While you are in a group you take 15% more damage." },

        { 96, "delver", "Delver", Family::Rules, 0, 1, 80, Rarity::Uncommon,
          MF_None, "", Boon::BonusLoot, 0, 0,
          "In a dungeon you take 15% more damage." },

        { 97, "cornered", "Cornered", Family::Attrition, 0, 1, 80, Rarity::Uncommon,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "Below half health, healing on you is 25% weaker." },

        { 98, "fresh_legs", "Fresh Legs", Family::Attrition, 0, 1, 80, Rarity::Uncommon,
          MF_None, "", Boon::BonusMaxHealth, 0, 0,
          "Above half health you deal 10% less damage." },

        { 99, "outlander", "Outlander", Family::Enemy, 0, 1, 80, Rarity::Uncommon,
          MF_None, "", Boon::BonusExperience, 0, 0,
          "In the open world everything chasing you is 15% faster." },

        // Ten more commons (docs/commons.md section 3.3), the half that
        // settles the mix the nine uncommons tipped. The four weapon and
        // shield denials are class-masked but filed Rules, as Axeless is:
        // a Family::Class row would spend CAP_CLASS.

        { 100, "shoulderless", "Shoulderless", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "You cannot wear shoulders." },

        { 101, "barefoot", "Barefoot", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusMoveSpeed, 0, 0,
          "You cannot wear boots." },

        { 102, "bare_handed", "Bare-handed", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusHealing, 0, 0,
          "You cannot wear gloves." },

        { 103, "wristless", "Wristless", Family::Rules, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusExperience, 0, 0,
          "You cannot wear bracers." },

        { 104, "shieldless", "Shieldless", Family::Rules,
          CM_WARRIOR | CM_PALADIN | CM_SHAMAN, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "You cannot carry a shield." },

        { 105, "maceless", "Maceless", Family::Rules,
          CM_WARRIOR | CM_PALADIN | CM_PRIEST | CM_ROGUE | CM_SHAMAN | CM_DRUID | CM_DEATH_KNIGHT, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "You cannot wield a mace." },

        { 106, "daggerless", "Daggerless", Family::Rules,
          CM_WARRIOR | CM_ROGUE | CM_HUNTER | CM_PRIEST | CM_MAGE | CM_WARLOCK | CM_DRUID | CM_SHAMAN, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "You cannot wield a dagger." },

        { 107, "staffless", "Staffless", Family::Rules,
          CM_WARRIOR | CM_HUNTER | CM_PRIEST | CM_SHAMAN | CM_MAGE | CM_WARLOCK | CM_DRUID, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusMaxHealth, 0, 0,
          "You cannot wield a staff." },

        { 108, "hunted", "Hunted", Family::Enemy, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusDamage, 0, 0,
          "Everything chasing you is 10% faster." },

        { 109, "slow_learner", "Slow Learner", Family::Attrition, 0, 1, 80, Rarity::Common,
          MF_None, "", Boon::BonusLoot, 0, 0,
          "You gain 15% less experience." },

        // The first of docs/greed-redesign.md section 7.3's loot cards, and
        // these three first for a measured reason rather than a thematic one:
        // section 7 of docs/handoff.md shows slot B's reward-shaped guarantee
        // drawing from a pool of a dozen cards, thinned again by the
        // distinct-family rule, and handing whatever survives the whole rarity
        // share. More reward-shaped cards in more families is the only lever
        // that moves it. Blood Price is the Attrition family's first that is
        // not an epic; Trophy Hunter is the second reward-shaped uncommon in
        // the table.
        //
        // Fresh Kill carries the "loot-rhythm" exclusive key, which has one
        // member today: The Tenth Corpse rewrites when a corpse pays in the
        // same way, and two cards that do that are one card twice.
        { 110, "fresh_kill", "Fresh Kill", Family::Rules, 0, 1, 80, Rarity::Rare,
          MF_RewardShaped, "loot-rhythm", Boon::BonusMoveSpeed, 0, 0,
          "A corpse opened within eight seconds of the kill is looted twice. After that it holds nothing but the quest." },

        { 111, "blood_price", "Blood Price", Family::Attrition, 0, 1, 80, Rarity::Rare,
          MF_RewardShaped, "", Boon::None, 0, 0,
          "Opening a corpse costs health. A corpse opened while hurt is looted twice." },

        { 112, "trophy_hunter", "Trophy Hunter", Family::Enemy, 0, 1, 80, Rarity::Uncommon,
          MF_RewardShaped, "", Boon::None, 0, 0,
          "A rare creature nearby makes everything hit harder. Killing one leaves a chest and banks a reroll." },

        // docs/commons.md section 4b. The Attrition family's reward-shaped
        // card was Blood Price, a rare, so a tier-1 set whose other two slots
        // took Rules and Enemy had no non-rare answer for slot B's guarantee.
        // This is that answer, and it is Blood Price's opposite on purpose.
        { 113, "scavenge", "Scavenge", Family::Attrition, 0, 1, 80, Rarity::Common,
          MF_RewardShaped, "", Boon::None, 0, 0,
          "You take more damage, and every corpse you loot restores health." },

        // And the Spawn family's, for the same reason. Smaller than Carrion on
        // purpose: one riser every eighth corpse against two scavengers every
        // fourth, and this one pays for itself.
        { 114, "gravedigger", "Gravedigger", Family::Spawn, 0, 1, 80, Rarity::Common,
          MF_RewardShaped, "", Boon::None, 0, 0,
          "Every eighth corpse you loot gets up again, and drops what it was holding back." },

        // More of docs/greed-redesign.md section 7.3's loot cards.
        { 115, "tribute", "Tribute", Family::Spawn, 0, 1, 80, Rarity::Rare,
          MF_OnKill | MF_RewardShaped, "", Boon::None, 0, 0,
          "Every 25th kill leaves a chest. Opening it draws scavengers." },

        // Exclusive with Fresh Kill: two cards that rewrite when a corpse pays
        // are one card twice.
        { 116, "tenth_corpse", "The Tenth Corpse", Family::Rules, 0, 1, 80, Rarity::Epic,
          MF_RewardShaped, "loot-rhythm", Boon::None, 0, 0,
          "Corpses hold nothing until the tenth. That one holds everything the others were carrying." },

        };
    }

    // The table and its two indices, built once on first use. FindMechanic by
    // id sits on the damage path, so neither lookup scans the rows.
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
    // ---------------------------------------------------------------------
    // On the upper tier bounds, and a correction (Phase 4).
    //
    // docs/phase-4-prompt.md says the rows that expire at tier 70 do so
    // because a maxTier of 14 was the old axis's TODO(design) default. That is
    // wrong, and the mistake was mine. The cards state their windows -- "Tiers
    // 3-14" for Red Mist and most of family C -- so 14 x 5 = 70 is the design
    // speaking, not a gap in it.
    //
    // What is actually wrong is that the design's tier curve was written for
    // sixteen tiers and does not stretch. "Tiers 3-14 of 16" means "not the
    // last eighth of the run", which on the old axis was two tiers carrying
    // four or five rows at heavy relaxation. On an eighty-tier axis the same
    // fraction is ten levels, and with a carried cap of sixteen a run has
    // taken every row that reaches them -- so it is not thin, it is empty.
    //
    // So thirty-three rows that closed at 70 now close at 80. Twenty-eight of
    // them are class curses, and the argument for those is the simplest one
    // available: a warrior's rage cap is exactly as interesting at level 80 as
    // at level 40, and a curse aimed at a kit should last as long as the kit.
    // The other five -- Echo, Reinforcements, Nimble, Cunning, Cursed Hoard --
    // have nothing about them that expires either.
    //
    // The same pass found a second thing, and it is why the first one changed
    // nothing on its own.
    //
    // Every class row carried the exclusive key "classcurse", and exclusivity
    // in this generator means "no two carried affixes may share a key". So a
    // run could carry exactly one class curse, ever, and the other forty-four
    // were unreachable the moment the first was taken. Extending twenty-eight
    // windows to tier 80 moved the measured tail by nothing at all, which is
    // what sent me looking.
    //
    // The design does not ask for that. Its rule is "never pay twice", and the
    // examples it gives are pairs: Long Forbearance and No Sanctuary both touch
    // Divine Shield, Bound Skin and Nature's Toll both touch shifting, Dead
    // Weight and Wide Dead Zone both touch kiting. Those three pairs keep their
    // keys -- shortcut:divine-shield, shortcut:shapeshift, shortcut:kiting --
    // and the blanket key is gone. How many a run may carry is a cap, which is
    // what CAP_CLASS is, rather than an exclusion.
    //
    // What deliberately did NOT move: the three Rules rows. Iron Purse ends at
    // 15 because WotLK gold stops mattering, Self-found at 20 because after
    // that it only deletes gear the character already owns, and Lone Wolf at 30
    // because it is written against the levelling dungeons. Those are shelf
    // lives the design argues for, not defaults it failed to fill in.
    // ---------------------------------------------------------------------

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
