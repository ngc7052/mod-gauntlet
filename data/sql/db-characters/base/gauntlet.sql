-- mod-gauntlet: per-character run state
-- SPDX-License-Identifier: AGPL-3.0-or-later
--
-- This file is the single definition of the module's schema. Two properties of
-- the core's updater shape everything below.
--
-- It re-applies a file whose hash changed (UpdateFetcher.cpp, ">> Reapplying
-- update ... (it changed)"), so rewriting this file makes it run again against
-- realms that already hold live runs. Nothing here may therefore destroy data:
-- no DROP, no TRUNCATE, no unguarded ALTER.
--
-- It also orders files by filename alone, recursively and across base/ and
-- updates/ alike (FillFileListRecursively, PathCompare), so
-- updates/2026_08_28_00_gauntlet.sql executes BEFORE this file. That update only
-- ever ALTERs tables it can already see, so on a fresh install it finds nothing
-- and the definitions here are what actually build the schema; on an existing
-- install it has already reshaped the tables and every CREATE here is a no-op.
-- The two definitions it repeats verbatim -- gauntlet_affix_log and
-- gauntlet_state -- must be kept in step with the ones below.

CREATE TABLE IF NOT EXISTS `gauntlet_run` (
    `guid`        INT UNSIGNED NOT NULL,
    `seed`        INT UNSIGNED NOT NULL,
    `tier`        INT UNSIGNED NOT NULL DEFAULT 0,
    `dead`        TINYINT UNSIGNED NOT NULL DEFAULT 0,
    -- The generator the run was created under; informational. The default is 1
    -- because every row that predates this schema was rolled by generator 1 --
    -- a new run must write Gauntlet::GeneratorVersion explicitly.
    `gen_version` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
    `class`       TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- What was picked, not how it was rolled. The old table stored only
-- (tier, roll) and regenerated the affix on every login, which meant any
-- change to the generator silently rewrote every live run; these columns are
-- written once and never move again.
CREATE TABLE IF NOT EXISTS `gauntlet_affix` (
    `guid`        INT UNSIGNED NOT NULL,
    `slot`        TINYINT UNSIGNED NOT NULL DEFAULT 0,
    -- 0 is Gauntlet::MECHANIC_NONE and doubles as the "not yet converted"
    -- marker the one-shot startup routine looks for on a migrated realm.
    `mechanic`    SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `rank`        TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `cond`        TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `boon`        TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `boon_mag`    TINYINT UNSIGNED NOT NULL DEFAULT 0,
    -- Generator 1 expressed a curse as a percentage drawn from a severity band
    -- and scaled by the condition, so it lands anywhere in 1..115 and does not
    -- fit the redesign's three-step rank ladder. Migrated rows keep the exact
    -- number here rather than being rounded onto a rank; 0 means "no legacy
    -- magnitude, take the strength from `rank`", which is every generator 2 row.
    `legacy_mag`  SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `gen_version` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
    `picked_at`   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`guid`, `slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Append-only. The surrogate key is what makes it append-only: a swap writes
-- swap_out and swap_in at the same tier in the same instant, so neither
-- (guid, tier) nor (guid, tier, at) can be a key, and only an auto-increment
-- gives the run's story a total order at one-second timestamp resolution.
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

-- Mechanic state that must survive a logout. Keys are `<mechanic key>.<field>`
-- ("champions.count", "shade.deadUntilTier"); `v` is signed because some of
-- them count down.
CREATE TABLE IF NOT EXISTS `gauntlet_state` (
    `guid` INT UNSIGNED NOT NULL,
    `k`    VARCHAR(32) NOT NULL,
    `v`    INT NOT NULL,
    PRIMARY KEY (`guid`, `k`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `gauntlet_leaderboard` (
    `guid`     INT UNSIGNED NOT NULL,
    `name`     VARCHAR(12) NOT NULL,
    `tier`     INT UNSIGNED NOT NULL DEFAULT 0,
    `level`    INT UNSIGNED NOT NULL DEFAULT 1,
    `cause`    VARCHAR(32) NOT NULL DEFAULT '',
    `conducts` VARCHAR(255) NOT NULL DEFAULT '',
    `affixes`  TEXT,
    `ended`    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`guid`),
    KEY `idx_rank` (`tier` DESC, `level` DESC)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
