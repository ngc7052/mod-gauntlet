--
-- C22 Grave Call's Risen (Phase 4).
--
-- A corpse the death knight did not claim, standing up on its own. Ordinary in
-- every way: the card's point is the corpse economy it forces, not the strength
-- of what rises, and the first two ranks make it weaker at runtime rather than
-- needing a second template.
--
-- Display 570 is Slavering Ghoul's, in use by creature 1791 in the world today,
-- so it is present in CreatureDisplayInfo.dbc on any client that can see
-- Duskwood. No DBC edit and no client patch.
--
-- The columns and the shared flag values are the base file's; see
-- data/sql/db-world/base/gauntlet_creatures.sql for what each one means.

DELETE FROM `creature_template` WHERE `entry` = 900006;
INSERT INTO `creature_template`
    (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`,
     `speed_walk`, `speed_run`, `detection_range`, `rank`, `dmgschool`, `DamageModifier`,
     `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`,
     `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`, `lootid`,
     `mingold`, `maxgold`, `AIName`, `MovementType`, `HoverHeight`, `HealthModifier`,
     `ManaModifier`, `ArmorModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`,
     `RegenHealth`, `flags_extra`, `ScriptName`)
VALUES
    (900006, 'Risen', NULL, 1, 1, 0, 14, 0,
     1.0, 1.14286, 20, 0, 0, 1.0,
     2000, 2000, 1, 1, 1,
     0, 2048, 0, 0, 7, 0, 0,
     0, 0, '', 0, 1, 1.0,
     1, 1, 1, 0, 0,
     1, 102760704, 'gauntlet_summon');

DELETE FROM `creature_template_model` WHERE `CreatureID` = 900006;
INSERT INTO `creature_template_model`
    (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`)
VALUES
    (900006, 0, 570, 1, 1);
