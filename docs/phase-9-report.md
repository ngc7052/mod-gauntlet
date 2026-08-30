# Phase 9 report — the audit that mostly came back clean

Branch `feature/affix-redesign`. Three commits, `e58bbb2` through this report.

A short phase, and most of it is negative results. That is the finding: the
classes of fault a machine can catch here have largely been caught, and what
this phase mainly produced is a guard against the next one rather than a fix for
the last one.

---

## 0. What was checked, and what it said

| Checked | Result |
|---|---|
| Every rank ladder moves in one direction | **clean** — 78 ladders, 3 documented sentinels |
| Every `PermanentCooldown` user restores on detach | **clean** |
| Every self-damage site floors at 1 health | **clean** (re-confirmed from Phase 7) |
| Summon records, the concurrent cap, and pruning | **clean** |
| A refused summon does not burn Reinforcements' per-fight budget | **clean** |
| Logout teardown | **hardened** — see §2, and it was not the bug it looked like |
| Rank IV on the leaderboard | fixed in Phase 8 |

---

## 1. The guard: ladders are now checked, not just counted

Phase 6 hand-wrote eighty fourth ranks across thirty-four files. The
`static_assert`s added first made a *missing* value a compile error — which is
why that phase was safe — but **a transposed digit is not missing**. `1.15`
where `1.55` was meant compiles, links, passes every test, and ships a rank IV
weaker than its rank III on a card promising an escalation.

`tests/compile-check.sh --anchors` now runs a ladder audit beside the anchor one:
every rank table must be monotonic. No Docker, no build, hundredths of a second.

**78 ladders, all monotonic** — so the eighty values were written correctly,
which is worth knowing rather than assuming.

The audit was verified to bite before being trusted: Death Rattle's severity was
inverted to `{8, 12, 18, 15}`, the audit failed, and it was put back. An audit
that has never failed is a claim, not a check.

Three tables change direction deliberately and now say so with
`LADDER-SENTINEL`. All three are the same shape — `0` is not a smaller number on
those ladders, it is *the button is gone*: Feign Death, Vanish and Blink at their
top rank. A sentinel has to be **marked** rather than tolerated, or the next real
inversion hides among the three that are fine.

---

## 2. A bug I talked myself into and then out of

`OnPlayerLogout` returned early when `IsEligible` said no. `IsEligible` answers
from `Gauntlet.PlayersOnly` and the session's bot flag — a *config-dependent*
answer that can change under a character that already has state. That is exactly
what the addon's `Forget`, three lines above, says in its own comment and this
handler did not act on.

It reads as a leak, and a specific one: run with `PlayersOnly = 0` so bots take
affixes, let a bot draw a Shade, set `PlayersOnly = 1`, reload, log the bot out.
`DespawnAll` never runs and the Shade stands in the world with an owner who is
gone — which the comment below it calls the worst failure this can produce.
mod-playerbots is installed on this realm.

**It is not a leak.** `WorldSession::LogoutPlayer` calls `OnPlayerLogout` at
`WorldSession.cpp:857` and `RemovePlayerFromMap` at `:876`, and the second fires
`OnPlayerLeaveAll`, where `GauntletMapScript` despawns everything
unconditionally. The map script was covering this handler's early return.

So the fault is real and is not the one it looked like: **a teardown path whose
correctness depended on a second teardown path nineteen lines of core apart**,
with nothing in either place saying so. Every call below the gate is already a
no-op for a character with no run, so making them unconditional costs a hash
lookup and stops the two leaning on each other.

The comment in the source records the wrong reading as well as the right one,
because the next person to look at that early return will have the same first
thought.

---

## 3. What the clean results are worth

Four subsystems were read looking for a specific failure and did not have it:

- **`PermanentCooldown` symmetry.** The fear was a swap leaving Vanish on a
  seven-day cooldown forever. Berserker's Bargain — the only one that holds and
  releases *dynamically*, on a health line — restores unconditionally in
  `OnDetach`, with a comment saying why. The other three hold on attach and
  release on detach.
- **Summon pruning.** `Prune` drops records whose creature is dead or gone
  *before* the concurrent cap is counted, so a dead Shade cannot block a live
  one.
- **Reinforcements' per-fight budget.** A summon refused by the cap does not
  increment `_spawned`; it re-arms and tries again. Rank IV's cap of six is
  therefore reachable across a fight even though only four may stand at once.
- **Self-damage floors.** Re-confirmed: all six sites clamp to one health, and
  the three world-damage sites deliberately do not.

Two of those were things I expected to find broken. Writing down that they are
not is what stops the next audit re-reading them.

---

## 4. What Phase 10 should know

1. **The machine-checkable surface is thinning.** Nine phases in, an audit pass
   now mostly confirms. That is a reason to weight the next phase toward content
   or toward the in-game checklist, not toward more reading.
2. **`tests/compile-check.sh --anchors` is two audits now**, and both cost
   nothing. Anything else that can be checked by reading the source belongs
   there rather than in a phase report.
3. **Verify a new audit fails before trusting it.** §1.
4. **Still not playtested**, and `docs/checklists.md` is now almost entirely
   things that need a screen. That is the honest bottleneck.
5. **`CAP_CLASS` is still 3 and still `TODO(design)`.** Six phases.
