-- mod-gauntlet: the creatures the affixes put into the world
-- SPDX-License-Identifier: AGPL-3.0-or-later
--
-- Five templates in the module's reserved range 900001-900005. Nothing else
-- ever writes into 900000-900999.
--
-- Three properties of the core's updater shape this file.
--
-- It applies module files in a second pass, after every RELEASED and ARCHIVED
-- core file, and orders that pass by filename alone across base/ and updates/
-- alike (UpdateFetcher.cpp:393-405 and PathCompare at :521). So nothing here
-- may depend on running before or after any other module's world SQL, and the
-- filename must be unique across every world .sql the server can see --
-- a duplicate filename is fatal (UpdateFetcher.cpp:92-99).
--
-- It re-applies a file whose hash changed (UpdateFetcher.cpp:350-354), so
-- every statement below must be safe to run again on a live realm. The three
-- deletes only ever name this module's own reserved entries, and each is
-- immediately followed by the insert that replaces it; nothing else in the
-- world database is touched, and no table is created, altered or dropped.
--
-- And it finds this directory at all only because "db-world" contains the
-- world database's module name (UpdateFetcher.cpp:177-182), which is what
-- makes data/sql/db-world/** picked up without any registration.
--
-- Remember that ac-worldserver runs with AC_UPDATES_ENABLE_DATABASES=0 on this
-- realm: this file reaches the database through ac-db-import and nowhere else,
-- and that image must contain the module (see docs/phase-0-report.md section 5).

-- ---------------------------------------------------------------------------
-- creature_template
-- ---------------------------------------------------------------------------
--
-- `exp` is 0 on every row and that is load bearing, not a default left alone.
-- SelectLevel reads BaseHealth[expansion] out of creature_classlevelstats
-- (CreatureData.h:313-316), and on this realm's data basehp1 and basehp2 are
-- literally 1 below levels 60 and 70 respectively. A summon that levels with
-- its owner covers 1 to 80, so any value but 0 gives a one-hit creature for
-- most of a run. The consequence is that a level 80 summon is sized like a
-- classic mob (5342 base health); the rank ladders scale from there in code.
--
-- `minlevel` and `maxlevel` are both 1 so the level the template picks is
-- deterministic. Every one of these is levelled to its owner by the module's
-- OnBeforeCreatureSelectLevel hook, and a fixed 1 makes the failure obvious
-- rather than random if that hook is ever missing.
--
-- faction 14 is the standard monster template: hostile to the player mask and
-- neutral to everything else (FactionTemplate.dbc row 14: EnemyGroup 1). The
-- Restless Spirit uses 35, friendly to all, because it is scenery.
--
-- flags_extra 102760704 = NO_TAUNT 0x100 (taunt immunity lives here and not in
-- the AI, CreatureData.h:54) | NO_PLAYER_DAMAGE_REQ 0x200000 (a summon killed
-- by a pet or a totem still counts) | DONT_CALL_ASSISTANCE 0x2000000 |
-- IGNORE_ALL_ASSISTANCE_CALLS 0x4000000 (it is one player's problem, and must
-- never pull the zone into it).
--
-- Every display id below exists in CreatureDisplayInfo.dbc and is already used
-- by a creature spawned in the live world, so no client patch is needed.

DELETE FROM `creature_template` WHERE `entry` BETWEEN 900001 AND 900005;
INSERT INTO `creature_template`
    (`entry`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`,
     `speed_walk`, `speed_run`, `detection_range`, `rank`, `dmgschool`, `DamageModifier`,
     `BaseAttackTime`, `RangeAttackTime`, `BaseVariance`, `RangeVariance`, `unit_class`,
     `unit_flags`, `unit_flags2`, `dynamicflags`, `family`, `type`, `type_flags`, `lootid`,
     `mingold`, `maxgold`, `AIName`, `MovementType`, `HoverHeight`, `HealthModifier`,
     `ManaModifier`, `ArmorModifier`, `ExperienceModifier`, `RacialLeader`, `movementId`,
     `RegenHealth`, `flags_extra`, `ScriptName`)
VALUES
    -- S1 The Shade. Undead, 1.5x health and 1.2x damage at rank I (the card's
    -- ranks II and III are applied in code, from the same base), and 0.85 run
    -- speed because the card's whole counterplay is that you can outpace it:
    -- speed_run is a rate against the 7.0 yd/s base, so 0.85 is 85% of an
    -- unmounted player exactly as the card asks.
    (900001, 'Shade', NULL, 1, 1, 0, 14, 0,
     -- type 6 UNDEAD: the card calls the Shade a "humanoid-typed undead", which
     -- no single `type` column can be. Undead keeps fear, stun and root working
     -- as the counterplay promises and costs only Polymorph. -- TODO(design)
     1.0, 0.85, 20, 0, 0, 1.2,
     2000, 2000, 1, 1, 1,
     0, 2048, 0, 0, 6, 0, 0,
     0, 0, '', 0, 1, 1.5,
     1, 1, 1, 0, 0,
     1, 102760704, 'gauntlet_summon'),

    -- S3 Carrion's scavengers: fast and fragile, two or three at a time. The
    -- card says "fast, fragile" and gives no numbers, so 0.6 health and 1.3 run
    -- speed are chosen: fast enough that outrunning them unmounted fails, soft
    -- enough that two of them are a fight and not a death. -- TODO(design)
    (900002, 'Scavenger', NULL, 1, 1, 0, 14, 0,
     1.0, 1.3, 20, 0, 0, 0.9,
     1600, 2000, 1, 1, 1,
     0, 2048, 0, 0, 1, 0, 0,
     0, 0, '', 0, 1, 0.6,
     1, 1, 1, 0, 0,
     1, 102760704, 'gauntlet_summon'),

    -- S5 Ambush: deliberately just a normal mob of the owner's level. Fighting
    -- it at full health is supposed to be free; being surprised at 40% is not.
    (900003, 'Ambusher', NULL, 1, 1, 0, 14, 0,
     1.0, 1.14286, 20, 0, 0, 1.0,
     2000, 2000, 1, 1, 1,
     0, 2048, 0, 0, 7, 0, 0,
     0, 0, '', 0, 1, 1.0,
     1, 1, 1, 0, 0,
     1, 102760704, 'gauntlet_summon'),

    -- E5 Grudge's Restless Spirit. Scenery: it stands on a corpse for 25 s and
    -- the aura that hurts you is the affix's, not the creature's. unit_flags
    -- 33555202 = NON_ATTACKABLE 0x2 | IMMUNE_TO_PC 0x100 | IMMUNE_TO_NPC 0x200
    -- | NOT_SELECTABLE 0x2000000, so it cannot be clicked, hit or aggroed.
    -- flags_extra adds NO_XP 0x40 and CANNOT_ENTER_COMBAT 0x2000 to the shared
    -- set, minus NO_PLAYER_DAMAGE_REQ, which is meaningless for something that
    -- cannot be killed.
    (900004, 'Restless Spirit', NULL, 1, 1, 0, 35, 0,
     1.0, 1.0, 0, 0, 0, 1.0,
     2000, 2000, 1, 1, 1,
     33555202, 2048, 0, 0, 6, 0, 0,
     0, 0, '', 0, 1, 1.0,
     1, 1, 1, 0, 0,
     1, 100671808, 'gauntlet_summon'),

    -- S2 Echo's Doppelganger. Twice a normal mob's health at rank I. The
    -- appearance is replaced at runtime by 45204 Clone Me, exactly as the
    -- core's own Mirror Image does; the base display is a visible humanoid
    -- rather than Mirror Image's invisible one so that a clone that fails to
    -- land leaves a person standing there and not a floating name. Phase 2
    -- owns Echo and may prefer the invisible model. -- TODO(design)
    (900005, 'Echo', NULL, 1, 1, 0, 14, 0,
     1.0, 1.14286, 20, 0, 0, 1.0,
     2000, 2000, 1, 1, 1,
     0, 2048, 0, 0, 7, 0, 0,
     0, 0, '', 0, 1, 2.0,
     1, 1, 1, 0, 0,
     1, 102760704, 'gauntlet_summon');

-- ---------------------------------------------------------------------------
-- creature_template_model
-- ---------------------------------------------------------------------------
--
-- Display ids only; no DBC and no client patch. Each one is verified present
-- in env/dist/data/dbc/CreatureDisplayInfo.dbc and is in use by a creature
-- that is spawned in the world today:
--
--   10483  Spectral Citizen  (Stratholme's translucent ghost)  -- the Shade
--    2710  Hecklefang Hyena                                    -- Scavenger
--    5035  Defias Thug                                         -- Ambusher
--   10045  Wisp                                                -- Restless Spirit
--    2357  Defias Bandit                                       -- Echo

DELETE FROM `creature_template_model` WHERE `CreatureID` BETWEEN 900001 AND 900005;
INSERT INTO `creature_template_model`
    (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`)
VALUES
    (900001, 0, 10483, 1, 1),
    (900002, 0,  2710, 1, 1),
    (900003, 0,  5035, 1, 1),
    (900004, 0, 10045, 1, 1),
    (900005, 0,  2357, 1, 1);

-- ---------------------------------------------------------------------------
-- creature_template_movement
-- ---------------------------------------------------------------------------
--
-- Only the Restless Spirit needs a row: it is rooted where the corpse fell and
-- must not drift, and the others take the defaults the LEFT JOIN supplies when
-- there is no row at all (WorldDatabase.cpp:82).

DELETE FROM `creature_template_movement` WHERE `CreatureId` BETWEEN 900001 AND 900005;
INSERT INTO `creature_template_movement`
    (`CreatureId`, `Ground`, `Swim`, `Flight`, `Rooted`, `Chase`, `Random`)
VALUES
    (900004, 0, 0, 0, 1, 0, 0);
