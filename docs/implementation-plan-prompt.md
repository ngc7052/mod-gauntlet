# Prompt: plan the implementation of the affix redesign

Copy everything below the line into a fresh Claude Code session opened in
`~/projects/mod-gauntlet`, ideally in plan mode (`/plan` or EnterPlanMode).

---

I want an implementation plan — not code yet — for replacing mod-gauntlet's
affix system with the design in `docs/affix-design.md`. Read that document in
full before doing anything else; it is the specification. Then read the current
module (`src/Gauntlet.h`, `src/GauntletMgr.{h,cpp}`, `src/GauntletAffix.cpp`,
`src/GauntletScripts.cpp`, `data/sql/db-characters/base/gauntlet.sql`,
`addon/GauntletUI/GauntletUI.lua`, `conf/mod_gauntlet.conf.dist`) so the plan
starts from what exists.

## What exists today

- AzerothCore module for WotLK 3.3.5a. Hardcore roguelike: one life, a tier every
  5 levels (16 tiers), choose one of three generated affixes, permanent and
  stacking. Runs are seeded; an affix is regenerated from `(seed, tier, roll)`,
  so the DB stores only the roll index (`gauntlet_affix`).
- Four scalar effects (damage taken, damage dealt, healing received, XP gained)
  aggregated additively in `Mgr::Multiplier` and applied in `ModifyMeleeDamage`,
  `ModifySpellDamageTaken`, `ModifyHealReceived`, `OnPlayerGiveXP`. Conditions
  (`Condition` enum) are evaluated ambiently in `ConditionActive`.
- A Lua addon that parses chat lines to show offers and carried affixes.
- Bots (mod-playerbots is merged into the server core) are excluded via
  `IsEligible`.

## What the design asks for (summary — the doc is authoritative)

- Seven affix families by verb: S spawn, E enemy behaviour, T tempo/position,
  A attrition, R rules, B bargains, C class curses (44 entries, four per class).
  71 mechanics in total, each with mechanic, counterplay, rank ladder, tiers,
  hooks and a build priority (A first, B second wave).
- Generator restructure: `affix = family → mechanic × condition × rank × boon`,
  with `IsRelevant(player, mechanic)` filtering (class, trained spells, talent
  tree), family caps and slots, rank-ups replacing the same mechanic rather than
  duplicating it, swap offers at tiers 4/8/12, bargains from tier 6, a
  reward-shaped offer guaranteed per tier, and a tier-unlock table (§4.6).
  Offers must stay deterministic from the seed; events need not be.
- Runtime framework: one per-player event scheduler with minimum spacing and an
  event budget (§4.2); global caps on aggregate multipliers (§4.3); owner-bound
  summons with custom AI that only ever attack their owner, despawn on
  logout/death/zone change, give reduced XP; a server→addon data channel
  (`LANG_ADDON` whisper, pattern in `AddonChannelCommandHandler::Send`,
  `Chat.cpp` ~1104) carrying countdowns, counters, wound, stacks, active
  conditions; leaderboard records class curses by name and rank.
- Verdict on the current four: keep damage taken (conditional only, never
  `Always`/`InCombat`, min 15%, `VersusElites` evaluated at the damage site);
  demote damage dealt to a boon/bargain price; replace healing received with
  Deep Wounds; cut XP gain from the standalone pool (Hubris replaces it).
- Vertical slice to prove the thesis (§7): Champions (E1), Falling Sky (T1),
  The Shade (S1), Deep Wounds (A1). They exercise counters, the scheduler,
  summons, the addon channel and a max-health modifier.

## Constraints (verified against the core — do not re-derive or invent hooks)

- Core tree the sync script targets: `/mnt/c/Users/3302/azerothcore-wotlk`
  (see `sync-to-server.sh`). Verify any hook you plan to use there; the doc's
  §6 lists what was checked, with file:line.
- Available and confirmed: `SummonCreature`, `CastSpell`/`CastCustomSpell`,
  `AddAura`, `SetSpeedRate`, `InterruptNonMeleeSpells`, `ProhibitSpellSchool`,
  `SetControlled`, `MoveFleeing`, `getAttackers`, `HasStealthAura`, `IsOutdoors`,
  `HasRestFlag`, `GetDynObject`, `ModifyPower`, `GetRuneCooldown`,
  `GetHappinessState`, `RemovePet`, `UnsummonAllTotems`, `AddSpellCooldown`/
  `HasSpellCooldown`, `Aura::SetDuration`; hooks `UnitScript::DealDamage`
  (runs before health is applied — cheat death), `OnBeforeRollMeleeOutcomeAgainst`,
  `OnAuraApply/Remove`, `OnUnitSetShapeshiftForm`, `OnPlayerEnterCombat/
  LeaveCombat`, `OnPlayerCreatureKilledByPet`, `OnPlayerCalculateTalentsPoints`,
  `OnPlayerLearnTalents`, `OnPlayerCanEquipItem`, `OnPlayerCanInitTrade/
  CanSendMail/CanPlaceAuctionBid`, `GroupScript::OnAddMember`,
  `AllCreatureScript::GetCreatureAI` and `OnBeforeCreatureSelectLevel`,
  `GlobalScript::OnItemRoll`/`OnAfterCalculateLootGroupAmount`,
  `OnPlayerBeforeSendLoot`.
- No client patches: custom creatures ship as `creature_template` rows in
  `data/sql/db-world/` using existing display ids; server-side `spell_dbc` rows
  are allowed but show no client icon — state goes through the addon channel.
  Reusable spell ids seen in core scripts: 8599 Enrage, 45204 Clone Me, 41055
  copy weapon, 676 Disarm, 8269 Frenzy; World Trigger NPC 21252; entry 12999
  exists. Player spell ids named in the doc (Blink 1953, Vanish 1856, Weakened
  Soul 6788, Forbearance 25771, …) are from memory and must be verified.
- Real players only; per-player work in `OnPlayerUpdate` throttled to a 500 ms
  accumulator; grid searches never for bots.
- Determinism: offers reproducible from the seed across module versions — plan
  a generator version stamp so old runs keep their affixes when the tables
  change.
- Legibility: the player must always know which affix acted; every event gets a
  chat line and an addon message.

## What I want from the plan

1. **Phases with dependencies**, each shippable and testable on a live server:
   (0) generator + data model + addon protocol, (1) runtime framework +
   vertical slice, (2) all build-priority-A mechanics, (3) build-priority-B,
   (4) class family, (5) pacing/tuning tools. For each phase: files touched,
   new files, order of work, what "done" means, rough size in sessions.
2. **Module layout**: how to split the monolith (`GauntletScripts.cpp`) so each
   mechanic is one file implementing a small interface (e.g. `IMechanic` with
   `OnTick`, `OnKill`, `OnDamageTaken`, `OnEnterCombat`, `Describe`,
   `IsRelevant`, `Rank`), registered in a table the generator reads. Show the
   interface and the registration pattern; show how family caps and slots are
   represented; show how the scheduler is wired.
3. **Data model and SQL migrations**: `gauntlet_affix` changes (mechanic id,
   rank, condition, boon, generator version), per-player mechanic state that
   must survive logout (counters, wound, Shade nemesis rank, bargain charges),
   leaderboard conduct column, `creature_template` rows needed, and a migration
   path for existing runs.
4. **Addon protocol spec**: message prefix, payload grammar, message types
   (offer, pick, state snapshot on login, event warning with countdown, counter
   update, condition active/inactive, death attribution), rate limits, and the
   addon-side changes (icons per family, countdown bar, counters, active
   lights, leaderboard with conducts). Keep the chat fallback working without
   the addon.
5. **Testing and tuning**: unit tests for generator determinism, cap enforcement
   and offer construction (no duplicates, caps, relevance, rank-ups); a
   `.gauntlet debug` command set to force an affix/rank, fire an event, set a
   counter, and dump scheduler state; an in-game checklist per affix (trigger,
   telegraph, counterplay works, attribution line, despawn rules); the pair
   tests named in §4.7; config toggles per family and a global "events off"
   switch.
6. **Risks and open questions** you need me to decide before coding — e.g.
   whether class-curse relevance reads talents or trained spells, how the swap
   offer is stored, whether reduced-XP summons need `flags_extra`, what
   happens to a Shade when its owner enters a dungeon with a group, how Ankh
   Pact interacts with `OnPlayerJustDied` ordering.

Do not write implementation code in this pass. Do not propose new mechanics;
if a doc entry is unimplementable with the verified hooks, say so and mark it
for cut rather than redesigning it. Write the plan to `docs/implementation-plan.md`.
