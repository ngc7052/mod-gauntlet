--
-- One tier per level (GeneratorVersion 6).
--
-- Every registry tier window was multiplied by five so that an affix unlocks at
-- the level it always did. Stored runs carry the old axis, where a tier was
-- five levels, so they are rescaled the same way: a run at tier 16 was a
-- character at level 80 and is now a run at tier 80.
--
-- gauntlet_affix.slot is the tier an affix was taken at and is half of that
-- table's primary key, so it moves with the axis. It cannot be multiplied in
-- one statement: MySQL updates row by row, and slot 1 becoming 5 collides with
-- the row still sitting at slot 5. So the whole set is shifted clear of itself
-- first and then scaled back down. Slots are at most 16 before this and at most
-- 80 after, and the intermediate range is 101-116, all well inside the
-- TINYINT UNSIGNED the column is.
--
-- Without this a level 80 character sitting at tier 16 would be offered tier 17
-- at its next level-up and would need sixty-four more levels it does not have
-- to reach the tiers its own affixes now live in.
--
-- Idempotent by the gen_version guard: a run already migrated is left alone.

UPDATE `gauntlet_affix` a
  JOIN `gauntlet_run` r ON r.`guid` = a.`guid`
   SET a.`slot` = a.`slot` + 100
 WHERE r.`tier` <= 16 AND r.`gen_version` < 6;

UPDATE `gauntlet_affix` a
  JOIN `gauntlet_run` r ON r.`guid` = a.`guid`
   SET a.`slot` = LEAST(80, (a.`slot` - 100) * 5)
 WHERE r.`tier` <= 16 AND r.`gen_version` < 6 AND a.`slot` > 100;

UPDATE `gauntlet_run`
   SET `tier` = LEAST(80, `tier` * 5),
       `gen_version` = 6
 WHERE `tier` <= 16 AND `gen_version` < 6;
