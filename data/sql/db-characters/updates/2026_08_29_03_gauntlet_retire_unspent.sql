-- mod-gauntlet: retire Unspent (mechanic id 69)
--
-- Phase 6 deletes C42 Unspent from the registry. The reasoning is in
-- docs/unspent-replacement-plan.md: it was a character-sheet tax of the kind
-- the redesign exists to delete, half its card described a state the game never
-- reached because nobody banks talent points, and what was left was close to a
-- free damage boon.
--
-- The id is retired, not reused -- a stored row must never resolve to a
-- different mechanic than the one it was written for -- so Killing Floor takes
-- id 74 and this clears the rows that still name 69. Without it, a character
-- carrying Unspent would load an affix the registry no longer describes: the
-- module handles that (MakeMechanic returns null and the instance goes
-- dormant), but it would sit in the carried set forever, occupying one of the
-- sixteen slots and showing as a blank row.
--
-- Deleting it frees the slot and takes the boon with it, which is the point.

DELETE FROM `gauntlet_affix` WHERE `mechanic` = 69;
