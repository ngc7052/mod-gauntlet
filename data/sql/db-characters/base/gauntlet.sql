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
    -- UNIX_TIMESTAMP(characters.creation_date) for the character this run was
    -- created for, and the module's answer to GUID reuse. The core hands out
    -- the guids of deleted characters again, so a run keyed on the guid alone
    -- is inherited by whoever is created next; a creation date cannot be
    -- inherited, because a new character has a new one. 0 means "unknown",
    -- which is every row written before this column existed and is treated as
    -- "cannot say" rather than as a mismatch. See GauntletMgr::Load.
    `char_created` INT UNSIGNED NOT NULL DEFAULT 0,
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
    -- The card's rarity (Gauntlet::Rarity) since the rank system went, kept in
    -- the column that held its rank. Written on every pick, never read back:
    -- the registry is the authority. updates/2026_09_01_01 normalises old rows.
    `rank`        TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `cond`        TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `boon`        TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `boon_mag`    TINYINT UNSIGNED NOT NULL DEFAULT 0,
    -- `legacy_mag` was here until Phase 2. It carried the free percentage
    -- generator 1 rolled, so that a migrated affix kept its exact strength
    -- instead of being rounded onto the redesign's three-step rank ladder.
    -- The last generator-1 row was converted in Phase 0 and the four mechanics
    -- that read the column were deleted in Phase 2, so the column went with
    -- them; updates/2026_08_29_00_gauntlet.sql drops it from an existing
    -- install.
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
    `action`      ENUM('pick', 'rankup', 'swap_out', 'swap_in', 'bargain', 'skip', 'reroll') NOT NULL,
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
