-- mod-gauntlet: the affix `rank` column now holds the card's rarity
-- SPDX-License-Identifier: AGPL-3.0-or-later
--
-- Step 4 of docs/rarity-plan.md removed the rank system. AffixInstance::rank
-- became AffixInstance::rarity, and it is written into gauntlet_affix's `rank`
-- column -- the column the ranks left behind, the same width -- so no column is
-- added or renamed. The worldserver never *reads* the value back: a card's
-- rarity is a fact about its registry row, and Mgr::Load takes it from there.
-- This update exists so the stored column agrees with what the server would
-- write, for anyone reading the table offline.
--
-- Gauntlet::Rarity is Common 0, Uncommon 1, Rare 2, Epic 3, Legendary 4. Every
-- card that existed under the rank system (ids 1-74) is Rare; every row after
-- (ids 75 and up, the commons) is Common. A stored rank of 1-4 would otherwise
-- read as Uncommon, Rare, Epic or Legendary at random.
--
-- Guarded the way every update here is: the updater runs this file before
-- base/gauntlet.sql, including on a fresh install where the table does not yet
-- exist. On an existing install it fires once; on every later pass it finds
-- nothing left to normalise -- the guard is "any row still holding a value the
-- rarity enum does not have for its mechanic", which is what a rank of 1, 3 or
-- 4 on an old row is, and 2 on a common.
--
-- gauntlet_affix_log's `rank` column is left as history: those rows say what a
-- card was worth when it was taken, and rewriting the past is not this file's
-- to do.
SET @stmt := IF(
    (SELECT COUNT(*) FROM information_schema.TABLES
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_affix') > 0
    AND (SELECT COUNT(*) FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'gauntlet_affix'
          AND COLUMN_NAME = 'rank') > 0,
    'UPDATE `gauntlet_affix`
        SET `rank` = CASE WHEN `mechanic` >= 75 THEN 0 ELSE 2 END
        WHERE `rank` <> CASE WHEN `mechanic` >= 75 THEN 0 ELSE 2 END',
    'DO 0');
PREPARE stmt FROM @stmt;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
