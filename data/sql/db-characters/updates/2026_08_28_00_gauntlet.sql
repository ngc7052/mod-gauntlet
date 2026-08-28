-- mod-gauntlet: convert an existing install to the picked-affix schema
-- SPDX-License-Identifier: AGPL-3.0-or-later
--
-- The core's updater orders SQL files by filename alone, recursively and across
-- base/ and updates/ alike (UpdateFetcher::FillFileListRecursively and
-- UpdateFetcher::PathCompare, which compares only path.filename()). '2' sorts
-- before 'g', so this file runs BEFORE base/gauntlet.sql -- including on a
-- fresh install, where none of the tables exist yet.
--
-- Every statement below is therefore guarded on what it can actually see. On a
-- fresh database each guard finds no table and does nothing, and base/gauntlet.sql
-- then creates the final schema outright. On an existing database the guards fire
-- once, and on every later pass they find the work already done. That keeps every
-- altered table defined in exactly one place -- base/gauntlet.sql -- rather than
-- duplicated across two files that would drift, and it survives the updater
-- re-applying a changed file. Only the two entirely new tables at the foot of
-- this file are repeated, and deliberately; see the note there.
--
-- MySQL 8.4 has no ADD COLUMN IF NOT EXISTS, so conditional DDL is an
-- information_schema test feeding a prepared statement. 'DO 0' is the no-op arm.
--
-- What this file deliberately does NOT do: drop `gauntlet_affix`.`roll` or
-- `tier`. The legacy affix was never stored, only the (seed, tier, roll) that
-- regenerated it, and unrolling that needs splitmix64 -- so the values must
-- survive until the one-shot startup routine in the worldserver has read them.
-- When that routine has written a non-zero `mechanic` to every row it finishes
-- the job itself with
--
--     ALTER TABLE `gauntlet_affix` DROP COLUMN `roll`, DROP COLUMN `tier`;
--
-- after which the table matches base/gauntlet.sql exactly and the guards below
-- never fire again.

-- ---------------------------------------------------------------------------
-- gauntlet_affix: the picked columns.
--
-- `slot` is the sentinel for the whole set, so the columns arrive together or
-- not at all. The AFTER clauses matter: once the routine has dropped `tier` and
-- `roll` the column order here is identical to the one base/gauntlet.sql
-- produces on a fresh install, so the two paths converge on the same DDL.
-- ---------------------------------------------------------------------------
SET @stmt := IF(
    (SELECT COUNT(*) FROM information_schema.TABLES
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_affix') > 0
    AND (SELECT COUNT(*) FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_affix'
          AND COLUMN_NAME = 'slot') = 0,
    'ALTER TABLE `gauntlet_affix`
        ADD COLUMN `slot`        TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `guid`,
        ADD COLUMN `mechanic`    SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `slot`,
        ADD COLUMN `rank`        TINYINT UNSIGNED NOT NULL DEFAULT 1 AFTER `mechanic`,
        ADD COLUMN `cond`        TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `rank`,
        ADD COLUMN `boon`        TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `cond`,
        ADD COLUMN `boon_mag`    TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `boon`,
        ADD COLUMN `legacy_mag`  SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `boon_mag`,
        ADD COLUMN `gen_version` SMALLINT UNSIGNED NOT NULL DEFAULT 1 AFTER `legacy_mag`,
        ADD COLUMN `picked_at`   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP AFTER `gen_version`',
    'DO 0');
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- `slot` is the tier the affix was taken at, which is what the old `tier`
-- column already held, so the one part of the conversion that is not splitmix
-- can be done here. Rows the startup routine has since inserted carry a
-- non-zero slot already and are left alone.
--
-- `slot` is TINYINT because Gauntlet::AffixInstance::slot is a uint8; a tier
-- above 255 is unrepresentable at runtime, not merely here. The old `tier` was
-- INT UNSIGNED and uncapped, so on a realm that somehow holds one this UPDATE
-- stops the whole file with ERROR 1264 rather than truncating a live player's
-- affix onto the wrong slot. That is deliberate: the remedy is to look at the
-- offending rows by hand, not to let the migration guess.
SET @stmt := IF(
    (SELECT COUNT(*) FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_affix'
          AND COLUMN_NAME IN ('slot', 'tier')) = 2,
    'UPDATE `gauntlet_affix` SET `slot` = `tier` WHERE `slot` = 0 AND `tier` > 0',
    'DO 0');
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Re-key on (guid, slot). Safe only after the backfill above, and safe at all
-- only because slot = tier for every legacy row, so the new key is unique
-- wherever the old one was. Done here rather than left to the startup routine
-- because MySQL narrows a primary key to whatever columns survive: dropping
-- `tier` while the old key still names it would leave (guid) alone, and the
-- ALTER would abort with ERROR 1062 on the first run holding two affixes.
SET @stmt := IF(
    (SELECT COUNT(*) FROM information_schema.STATISTICS
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_affix'
          AND INDEX_NAME = 'PRIMARY' AND COLUMN_NAME = 'tier') > 0,
    'ALTER TABLE `gauntlet_affix` DROP PRIMARY KEY, ADD PRIMARY KEY (`guid`, `slot`)',
    'DO 0');
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ---------------------------------------------------------------------------
-- gauntlet_run: which generator made the run, and the class it was made for.
--
-- gen_version defaults to 1 so that every row predating this file is labelled
-- with the generator that actually produced it.
-- ---------------------------------------------------------------------------
SET @stmt := IF(
    (SELECT COUNT(*) FROM information_schema.TABLES
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_run') > 0
    AND (SELECT COUNT(*) FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_run'
          AND COLUMN_NAME = 'gen_version') = 0,
    'ALTER TABLE `gauntlet_run`
        ADD COLUMN `gen_version` SMALLINT UNSIGNED NOT NULL DEFAULT 1 AFTER `dead`,
        ADD COLUMN `class`       TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `gen_version`',
    'DO 0');
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ---------------------------------------------------------------------------
-- gauntlet_leaderboard: the conducts and the affix list EndRun now writes.
-- Both stay empty for runs that ended before this file; there is nothing left
-- to reconstruct them from.
-- ---------------------------------------------------------------------------
SET @stmt := IF(
    (SELECT COUNT(*) FROM information_schema.TABLES
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_leaderboard') > 0
    AND (SELECT COUNT(*) FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_leaderboard'
          AND COLUMN_NAME = 'conducts') = 0,
    'ALTER TABLE `gauntlet_leaderboard`
        ADD COLUMN `conducts` VARCHAR(255) NOT NULL DEFAULT '''' AFTER `cause`,
        ADD COLUMN `affixes`  TEXT AFTER `conducts`',
    'DO 0');
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ---------------------------------------------------------------------------
-- The two genuinely new tables.
--
-- base/gauntlet.sql creates these too and always runs after this file, so these
-- statements are strictly redundant in the normal path. They are here so the
-- file stands on its own if an administrator applies it by hand, and so a realm
-- whose `updates` row for gauntlet.sql is already current still gets them.
-- Keep the two definitions byte-identical.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `gauntlet_affix_log` (
    `id`          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `guid`        INT UNSIGNED NOT NULL,
    `tier`        TINYINT UNSIGNED NOT NULL,
    `action`      ENUM('pick', 'rankup', 'swap_out', 'swap_in', 'bargain') NOT NULL,
    `mechanic`    SMALLINT UNSIGNED NOT NULL,
    `rank`        TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `gen_version` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
    `at`          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_guid` (`guid`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `gauntlet_state` (
    `guid` INT UNSIGNED NOT NULL,
    `k`    VARCHAR(32) NOT NULL,
    `v`    INT NOT NULL,
    PRIMARY KEY (`guid`, `k`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
