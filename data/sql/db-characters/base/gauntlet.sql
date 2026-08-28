-- mod-gauntlet: per-character run state
-- SPDX-License-Identifier: AGPL-3.0-or-later

CREATE TABLE IF NOT EXISTS `gauntlet_run` (
    `guid` INT UNSIGNED NOT NULL,
    `seed` INT UNSIGNED NOT NULL,
    `tier` INT UNSIGNED NOT NULL DEFAULT 0,
    `dead` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Only the roll index is stored; the affix itself is regenerated from
-- (seed, tier, roll), so a run is fully reproducible from its seed.
CREATE TABLE IF NOT EXISTS `gauntlet_affix` (
    `guid` INT UNSIGNED NOT NULL,
    `tier` INT UNSIGNED NOT NULL,
    `roll` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`guid`, `tier`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `gauntlet_leaderboard` (
    `guid`  INT UNSIGNED NOT NULL,
    `name`  VARCHAR(12) NOT NULL,
    `tier`  INT UNSIGNED NOT NULL DEFAULT 0,
    `level` INT UNSIGNED NOT NULL DEFAULT 1,
    `cause` VARCHAR(32) NOT NULL DEFAULT '',
    `ended` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`guid`),
    KEY `idx_rank` (`tier` DESC, `level` DESC)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
