# Phase 3 prompt

Paste the block below into a fresh session.

---

Implement Phase 3 of `docs/implementation-plan.md` for mod-gauntlet, an
AzerothCore (WotLK 3.3.5a) module. The plan is the contract; the design it
implements is `docs/affix-design.md`. Read both in full before touching code,
then read `docs/phase-0-report.md` and `docs/phase-2-report.md` — they record
the decisions these phases inherit and several traps that already cost a build
each. Then read the current module end to end (`src/`, `tests/`, `data/sql/`,
`addon/GauntletUI/`, `conf/mod_gauntlet.conf.dist`, `mod-gauntlet.cmake`,
`sync-to-server.sh`).

## The point of this phase

Two families that have never existed, and the first affixes that change the
outcome of a fight instead of adding pressure to it.

Six rows: Blood Magic (20), Self-found (23), Lone Wolf (24), Iron Purse (25),
Last Rites (26), Cursed Hoard (27). That is a small number of mechanics and a
large amount of shared machinery, because five of the seven core hooks they need
are not wired, `IMechanic::OnLethal` and `OnSpellCast` are declared and
dispatched from nowhere, and the module has no `GroupScript` at all.

Two structural facts worth knowing before you start:

- **The bargain slot is already built and has never fired.** `OfferKind::Bargain`,
  the 1-in-6 roll at `GauntletGenerator.cpp:521`, `CAP_BARGAIN = 2` and
  `BARGAIN_MIN_TIER = 6` have been in the generator since Phase 0 and are dead
  code because both Bargain rows are `MF_NotImplemented`. You are turning on an
  existing path, not adding one.
- **The pending-death seam is finished and waiting.** `BeginPendingDeath`, the
  60-second timer, `Mgr::CancelPendingDeath` and an `OnPlayerResurrect` override
  left deliberately empty with a comment naming this phase. Last Rites does not
  use it — a cheat death never dies — so this phase's job there is to build the
  *charge* abstraction that Phase 4's Ankh Pact and Stone of the Damned will
  spend at that seam, and to leave the seam still unspent.

### What it is worth, measured

Family availability per tier over the real registry, class mask ignored:

```
tier          1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   16
families now  3    3    4    4    4    4    4    4    4    4    4    4    3    3    3    3
        after 4    4    5    6    6    6    5    5    5    5    5    5    4    4    4    4
rows    now   4    6    9   12   16   19   19   19   19   18   15   14   10    8    4    4
        after 7    9   12   15   19   22   21   22   22   21   18   17   12   10    5    5
```

The win is tiers 1–2, which are the tightest in the table: four rows and exactly
three families against three distinct-family slots, with zero slack, at the tier
every run passes through.

**`docs/phase-2-report.md` §8.4 claims Rules and Bargains fix the tier 13–16
tail. That claim is wrong and this phase should retract it.** Rules stop at tier
6; of the six new rows exactly one, Last Rites, reaches tier 15. The tail goes
from four rows to five. Do not widen a window to move that number — report it
and leave it to Phase 4's forty-four class curses.

## Decisions already taken — do not re-open; flag if one turns out impossible

1. **Lone Wolf is a live penalty, not a cage.** You may group. **While in a
   group your maximum health is halved; while solo you gain +20% experience.**
   This overrides the card's "You cannot join a group" and the design's
   "offer it as a server option" note. The reason: the row's window is tiers
   1–6, which is levels 5–30, which is RFC, Deadmines, Shadowfang Keep, the
   Stockade and Gnomeregan — the card removes a third of the levelling game from
   the run, and design §2.9 already rejects affixes whose only instruction is
   "don't". A penalty the player can end by leaving the group is a standing
   decision instead of a brick. **Any group counts, bots included**; a bot party
   must not be a free bypass. Both halves are live and reversible — the health
   changes the moment the group does.
2. **Cursed Hoard's curse is a real ×3, and leaving combat ends it.** It clears
   on three kills *or* after `Gauntlet.Bargain.CursedHoard.EscapeSeconds`
   (default 10) out of combat. Be plain in the report about what that costs: at
   ten seconds the bargain is close to free, because the player can open the
   chest, walk away and wait. Ship the default at 10 anyway — the realm is
   hardcore and a ×3 with no exit is a run-ender from one unlucky pull — and
   make `0` mean "never clears out of combat", which is the strict card, so the
   dial exists after it has been played.
3. **Caps are relaxed by the mechanic that needs it, never bypassed.** Both
   bargain-shaped penalties above break a clamp: ×3 hits
   `Gauntlet.Caps.DamageTaken = 2.0` and would silently become ×2, and half
   health hits `Gauntlet.Caps.MaxHealth = 0.6` and would silently become −40%.
   A blurb that promises a number the aggregate then eats is exactly the lie
   this module has refused everywhere else. Add
   `IMechanic::RelaxCaps(AffixInstance const&, AggregateKind, AggregateCaps&) const`,
   called by `Mgr::AggregateAt` on each carried affix before the clamp. Cursed
   Hoard raises `damageTakenMax` to 3.0 while cursed; Lone Wolf lowers
   `maxHealthMin` to 0.5 while grouped. **One clamp, still applied exactly once
   to the product** — nothing is applied after it. An aggregate test asserts
   that no other mechanic relaxes anything, and that the relaxation lapses the
   moment the state does.
4. **No aura, for anything.** Consistent with the whole module and with "no
   client patches". Last Rites' Mark is run state plus hooks plus HUD, not a
   spell. A reused spell id would show the DBC's own tooltip and lie about both
   numbers, the way Falling Sky's dodge buff already does.
5. **Blood Magic's self-damage is the module's own and is invisible to the
   module's other hooks.** It makes no Deep Wound, spends no Last Rites charge,
   feeds no Grudge and stacks no Frenzy, and it floors at 1 health. Guard it
   with an explicit "this is our own damage" flag on the run rather than by
   inspecting the attacker, which will be the player either way.
6. **The two bargain tier gates disagree; fix the row, keep the constant.**
   Cursed Hoard's registry row reads 4–14, `BARGAIN_MIN_TIER` reads 6, and the
   design's family-B header says "only from tier 6". Move the row to **6–14** and
   add a registry test that no `Family::Bargain` row has a `minTier` below
   `BARGAIN_MIN_TIER`, so the two can never drift again.
7. **No Phase 2 retrospective.** The six unverified visuals in
   `docs/phase-2-report.md` §7 (Death Rattle's circle, Falter's disarm and
   silence, Reinforcements' copies, Craven's flee, Nimble's speed, Echo's clone)
   stay with the user's live testing; the user's call. Do not gate this phase on
   them and do not go looking for them — but do not regress them either.
8. **No SQL migration is expected.** Every piece of state these six need — the
   Last Rites charge, the Mark's remaining time, Cursed Hoard's kill debt — is a
   key/value pair in `gauntlet_state`, which already exists. If you find you
   need a column, that is a signal you have put run data in the wrong place;
   say so before adding one.

## Scope, in this order

### 1. The shared machinery, before any mechanic

None of the six can be written until this exists, and writing them first is how
Phase 1 produced three mechanics that could not act.

- **Wire `IMechanic::OnLethal`.** `GauntletUnitScript::DealDamage`
  (`src/GauntletScripts.cpp:392`) currently observes and returns the damage
  unchanged. `Unit::DealDamage` honours the return value at
  `$CORE/src/server/game/Entities/Unit/Unit.cpp:984`, and `ScriptMgr::DealDamage`
  chains every registered script's return
  (`ScriptDefines/UnitScript.cpp:52-64`), so a clamp here is real. **Order
  matters: dispatch `OnLethal` first and `OnDamageTaken` after, with the
  post-clamp figure**, so Deep Wounds wounds off the damage the player actually
  took rather than off the overkill Last Rites just refused.
- **Wire `OnPlayerSpellCast`** (`PlayerScript.h:331`) to `IMechanic::OnSpellCast`.
- **A `GauntletGroupScript`** — the module has none. `GroupScript::OnAddMember`,
  `OnRemoveMember`, `OnDisband` (`ScriptDefines/GroupScript.h:48,54,60`), each
  resolving the guid to a live player and calling a new
  `Mgr::OnGroupChanged(Player*)` that runs `RefreshStats` and republishes.
  Note that `OnPlayerCanGroupInvite` is the *inviter's* hook and cannot see a
  player being invited, which is why the group state has to be observed rather
  than vetoed even in the card's original form.
- **`IMechanic::RelaxCaps` and its use in `Mgr::AggregateAt`**, per decision 3.
- **`src/mechanics/Charges.{h,cpp}`**, beside `Boons.h` and `Nearby.h`: per-level
  charge accounting over the state store, so Last Rites and Phase 4's two
  resurrect bargains share one implementation of "once per level, per two
  levels, per three". Leave `OnPlayerResurrect` empty and say in its comment
  what Phase 4 spends there.
- **Run `tests/compile-check.sh` on every file you touch, including this step.**
  It is 0.5–7 s and its anchor audit is what catches "the mechanic is offered
  and does nothing" before a Docker build does.

### 2. The Rules family — three cheap rows that fix tiers 1–2

`src/mechanics/rules/{IronPurse,SelfFound,LoneWolf}.cpp`. Cheapest first, so the
family is registered and anchored before the hard one.

- **Iron Purse (25)**, tiers 1–3 = levels 5–15: `OnPlayerBeforeDurabilityRepair`
  (`PlayerScript.h:463`), `discountMod` halved. Say plainly in the report that
  this is the weakest row in the table — repair bills at level 5–15 are copper,
  and on a hardcore realm the player dies once. It earns its place as a third
  Rules row at the tightest tiers, not as an affix anyone will feel. Its boon is
  `Boon::None`; if you think it needs one, propose it, do not roll one.
- **Self-found (23)**, tiers 1–4: `OnPlayerCanInitTrade` (`:628`),
  `OnPlayerCanSendMail` (`:512`), `OnPlayerCanPlaceAuctionBid` (`:448`), and
  `Boon::BonusMoney` at the loot-money site. Each refusal is its own telegraph —
  a chat line naming the affix, because a veto with no explanation reads as a
  bug. Decide and state what happens to mail already in the box and to an
  auction already bid on; do not silently eat either.
- **Lone Wolf (24)**, tiers 1–6, per decision 1. The health half rides
  `OnPlayerAfterUpdateMaxHealth`, which is already wired for Deep Wounds, and
  needs `Mgr::RefreshStats` on every group change — `Player::UpdateMaxHealth` is
  the only thing that fires that hook and the core calls it only on level and
  stamina changes. The XP half rides `OnPlayerGiveXP`. Watch for a playerbots
  auto-invite path putting a player in a group without a client action.

### 3. Blood Magic (20) — `src/mechanics/attrition/BloodMagic.cpp`

Tiers 5–12, mana users, 2/3/5% of maximum health per spell with a power cost,
heals included. Decision 5 governs the self-damage. Pair it with its
`Boon::BonusDamage` at the damage site, and note that this is the second row in
`Family::Attrition` — Deep Wounds has been alone there since Phase 1, which is
why tiers 13–14 currently show that family empty.

### 4. The Bargains — `src/mechanics/bargain/{LastRites,CursedHoard}.cpp`

Last Rites first: it is the row Phase 4 reuses, and the plan says so.

- **Last Rites (26)**, tiers 8–16. `OnLethal` returns `health - 1` when the
  charge is up; the Mark then runs ten minutes at ×1.5 damage taken and clamps
  heals so they cannot carry the player above half. The heal clamp is an
  absolute ceiling expressed through `HealTakenMult`, which has the `Ctx` it
  needs to compute the ratio. Charge and Mark both persist through logout.
  Telegraph it like the event it is: a `CTR` for charges remaining, a 600-second
  `EVT` countdown for the Mark, a `STAT` naming both of its effects, and a loud
  chat line and sound at the moment of the save. The Mark is the most memorable
  ten minutes in the design; a player who cannot see it running has not been
  given it.
- **Cursed Hoard (27)**, tiers 6–14 after decision 6. `OnPlayerBeforeSendLoot`
  is already wired and dispatched (Carrion uses it) — gate on the loot guid
  being a game object, which is the part Carrion does not do.
  `GlobalScript::OnAfterCalculateLootGroupAmount` (`GlobalScript.h:66`) doubles
  the loot. `CTR` for kills owed, `EVT` for the escape window, and the chest
  must be identifiable as cursed *before* it is opened or there is no decision
  in the affix at all.

### 5. Registry, generator, sweep

Clear `MF_NotImplemented` from all six rows, add the anchors, fix Cursed Hoard's
window and add the bargain-gate test from decision 6. Then re-run the invariant
sweep and report **both** tables — the live view and the full table — against
`docs/phase-2-report.md` §4 as the baseline, at the same sample sizes, with the
same negative controls plus one for `RelaxCaps`. Regenerate
`addon/GauntletUI/Data.lua` with `.gauntlet debug export-addon`; never hand-edit
it. New config keys go in `conf/mod_gauntlet.conf.dist` with the same comment
style as the existing ones.

### 6. Report

`docs/phase-3-report.md`: what was built per step, the build and test commands
and their results, every deviation with its reason, the `TODO(design)` list, the
measured sweep, the retraction of §8.4, and what Phase 4 should know. Be plain
about anything that does not work — `docs/phase-2-report.md` §7 is the standard.

## Environment and guardrails

- The core is `/mnt/c/Users/3302/azerothcore-wotlk`; `sync-to-server.sh` mirrors
  the module into its `modules/` because Docker will not follow symlinks. Read
  that tree's `AGENTS.md` and its compose files.
- **The realm is live and the user plays on it.** Build with
  `docker compose build ac-worldserver ac-db-import`, apply SQL with
  `docker compose up ac-db-import`, deploy with
  `docker compose up -d ac-worldserver`. Never run any `docker … prune`, never
  touch a container outside that compose project.
- Verify every core API by reading its header in that tree and quote file:line.
  Two names collide with core globals outside `namespace Gauntlet`: `Condition`
  and `MECHANIC_NONE`.
- Work on `feature/affix-redesign`, commit per numbered step, do not push. The
  repo commits as `nero <26593322+ngc7052@users.noreply.github.com>`; do not
  change the identity and never put an email address in a commit or file.
- No client patches: no DBC edits, no new spell visuals, existing spell ids and
  display ids only.
- Real players only; `IsEligible` stays and nothing runs for bots.
- The user's account is `gmlevel 3`. Several mechanics refuse while
  `IsGameMaster()` is true, which cost most of an evening in Phase 2 — check
  `.gm off` before concluding a mechanic is broken, and say so in any handover.
- Ids 21, 22, 72 and 73 are spent forever;
  `Registry.TheDeletedScalarIdsAreGoneAndStayGone` enforces it.

## Definition of done

All six rows are offerable, implemented and anchored; `Family::Rules` and
`Family::Bargain` appear in real offers; the bargain slot fires; Last Rites
turns one certain death into a chase and the Mark is visible on the HUD for its
full ten minutes; Cursed Hoard's ×3 is genuinely ×3 and the cap relaxation
lapses with it; Lone Wolf's health moves the instant the group does; the
invariant sweep passes with no relaxation where it passed before and the tier
1–2 numbers improve; the unit tests pass; and the worldserver starts clean.
