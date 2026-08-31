-- mod-gauntlet: the affix log learns 'skip' and 'reroll'
-- SPDX-License-Identifier: AGPL-3.0-or-later
--
-- The rarity plan's step 3 adds reroll and skip to the offer economy, and both
-- are decisions the run's story should record: which tiers were declined, and
-- which had their offers rebuilt and how often, is exactly what a "why did this
-- run get these offers" question needs. Both rows carry mechanic 0 and rank 0,
-- because no card was involved.
--
-- The updater orders files by filename alone, so this runs BEFORE
-- base/gauntlet.sql -- including on a fresh install, where the table does not
-- exist yet. Guarded the way 2026_08_28_00_gauntlet.sql guards everything: on a
-- fresh database the guard finds no table and does nothing, and the base file
-- creates the final ENUM outright; on an existing database it fires once and
-- then finds the values already present. The two values are APPENDED, never
-- reordered: MySQL stores ENUM values by index, and reordering would relabel
-- every row already written.
SET @stmt := IF(
    (SELECT COUNT(*) FROM information_schema.TABLES
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_affix_log') > 0
    AND (SELECT COUNT(*) FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_affix_log'
          AND COLUMN_NAME = 'action' AND COLUMN_TYPE NOT LIKE '%''skip''%') > 0,
    'ALTER TABLE `gauntlet_affix_log`
        MODIFY `action` ENUM(''pick'', ''rankup'', ''swap_out'', ''swap_in'', ''bargain'', ''skip'', ''reroll'') NOT NULL',
    'DO 0');
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
