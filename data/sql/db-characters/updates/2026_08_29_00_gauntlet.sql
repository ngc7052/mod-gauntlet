-- mod-gauntlet: drop legacy_mag, and key a run to the character that owns it
-- SPDX-License-Identifier: AGPL-3.0-or-later
--
-- Two Phase 2 changes to an existing install. The same three properties of the
-- core's updater that shape 2026_08_28_00_gauntlet.sql shape this file:
--
--   * files are ordered by filename alone across base/ and updates/ alike
--     (UpdateFetcher::FillFileListRecursively, PathCompare), so '2026_08_29'
--     runs after '2026_08_28' and both run BEFORE base/gauntlet.sql -- on a
--     fresh install too, where none of the tables exist yet;
--   * a file whose hash changes is re-applied, so nothing here may destroy
--     data on a second pass;
--   * MySQL 8.4 has no DROP COLUMN IF EXISTS, so conditional DDL is an
--     information_schema test feeding a prepared statement, with 'DO 0' as the
--     no-op arm.
--
-- On a fresh database every guard below finds no table and does nothing, and
-- base/gauntlet.sql then creates the final schema outright. On this realm the
-- guards fire once and never again.

-- ---------------------------------------------------------------------------
-- 1. gauntlet_affix.legacy_mag goes.
--
-- It carried the free percentage generator 1 rolled (2..115), so that an affix
-- migrated out of the old schema kept its exact strength instead of being
-- rounded onto the redesign's three-step rank ladder. Only the four flat
-- scalars ever read it -- Exposed, Feeble, Withering and Forgetful -- and
-- Phase 2 deleted all four along with the one-shot migration that was the
-- column's only writer. Nothing in the module names it any more.
-- ---------------------------------------------------------------------------
SET @g := (SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
           WHERE `TABLE_SCHEMA` = DATABASE()
             AND `TABLE_NAME`   = 'gauntlet_affix'
             AND `COLUMN_NAME`  = 'legacy_mag');
SET @s := IF(@g > 0, 'ALTER TABLE `gauntlet_affix` DROP COLUMN `legacy_mag`', 'DO 0');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- ---------------------------------------------------------------------------
-- 2. gauntlet_run.char_created arrives.
--
-- The core reuses the guids of deleted characters, so a run keyed on the guid
-- alone is inherited by whoever is created next -- retired flag, tier and every
-- affix (docs/phase-0-report.md section 3.7). Phase 0 answered that with two
-- heuristics on login: the class must match, and the character must have at
-- least as many levels as the run has tiers. The second one also purges any
-- character a game master has levelled *down* for testing, which is the shape
-- every deliberately-built test character has.
--
-- A creation date cannot be inherited: a character created into a recycled guid
-- has a new one. UNIX_TIMESTAMP is used rather than the DATETIME itself so the
-- column is a plain integer the module can read with Field::Get<uint32> and
-- compare exactly, with no timezone or format in the way.
--
-- Existing rows are backfilled from `characters` where the character is still
-- there, and left at 0 -- "unknown" -- where it is not. Zero is deliberately
-- NOT treated as a mismatch: a run that predates the column has no evidence
-- either way, and purging a live hardcore run on no evidence is worse than
-- keeping one that should have gone.
-- ---------------------------------------------------------------------------
SET @g := (SELECT COUNT(*) FROM `information_schema`.`TABLES`
           WHERE `TABLE_SCHEMA` = DATABASE() AND `TABLE_NAME` = 'gauntlet_run');
SET @c := (SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
           WHERE `TABLE_SCHEMA` = DATABASE()
             AND `TABLE_NAME`   = 'gauntlet_run'
             AND `COLUMN_NAME`  = 'char_created');
SET @s := IF(@g > 0 AND @c = 0,
             'ALTER TABLE `gauntlet_run` ADD COLUMN `char_created` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `dead`',
             'DO 0');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @s := IF(@g > 0 AND @c = 0,
             'UPDATE `gauntlet_run` r JOIN `characters` c ON c.`guid` = r.`guid`
                 SET r.`char_created` = UNIX_TIMESTAMP(c.`creation_date`)
                 WHERE r.`char_created` = 0',
             'DO 0');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;
