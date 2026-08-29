# Phase 3 report — attrition, rules, bargains

Branch `feature/affix-redesign`, four commits: `4201403`, `e8db1c7`, `9bec623`,
`fa978cd`.

---

## 0. Definition of done, item by item

| Asked | Result |
|---|---|
| All six rows offerable, implemented, anchored | **yes** — 25 mechanics registered, all anchored |
| `Family::Rules` and `Family::Bargain` appear in real offers | **yes** |
| The bargain slot fires | **yes**, and turning it on found two faults (§3.3, §3.4) |
| Last Rites converts a death into a chase; the Mark is visible for its ten minutes | **built and telegraphed; not seen on a screen** (§6) |
| Cursed Hoard's ×3 is genuinely ×3 and the relaxation lapses with it | **yes**, `RelaxCaps` + five tests |
| Lone Wolf's health moves the instant the group does | **yes**, `GroupScript` + a tick reconciler |
| Sweep passes with no relaxation where it passed before; tiers 1–2 improve | **yes**; every one of the sixteen tiers improved (§4) |
| Unit tests pass | **103 pass**, up from 97 |
| Worldserver starts clean | **see §7** |

---

## 1. What was built

### Step 1 — the machinery (`4201403`)

None of the six could be written first. Five of the seven core hooks they need
were unwired, `IMechanic::OnLethal` and `OnSpellCast` had been declared since
Phase 0 and dispatched from nowhere, and the module had no `GroupScript` at all.

- **`OnLethal` wired.** `GauntletUnitScript::DealDamage` observed and returned
  the damage unchanged; `Unit.cpp:984` takes the return value straight back and
  `ScriptDefines/UnitScript.cpp:52-64` chains every script's answer, so a clamp
  there is real. **`OnLethal` runs first and `OnDamageTaken` sees what is left.**
  The other order would hand Deep Wounds a wound made out of overkill that never
  happened — turning the affix that saved the run into the one that ends it.
- **`OnPlayerSpellCast` wired**, for Blood Magic.
- **`GauntletGroupScript`.** Observation, not a veto:
  `OnPlayerCanGroupInvite` is the *inviter's* hook (`PlayerScript.h:506`) and
  never sees the player being invited, so even the card's original wording could
  not have been a refusal on the joining side. `OnDisband` is covered separately
  because the last member leaving takes the group with it and sends no
  `OnRemoveMember` for whoever is left holding it.
- **`IMechanic::RelaxCaps` + `Mgr::EffectiveCaps`.** See §3.1.
- **`RunState::selfDamage`.** See §3.2.
- **`src/mechanics/Charges.{h,cpp}`.** "Once per level" over one integer in the
  state store, shared with Phase 4's two resurrect bargains. A charge is derived
  from the current level rather than granted and stored, so no grant hook can be
  missed. A stored level *above* the player's current one reads as never spent —
  a GM level-down is exactly the shape that made Deep Wounds look broken for an
  evening in Phase 2.

### Step 2 — the Rules family (`e8db1c7`)

`rules/{IronPurse,SelfFound,LoneWolf}.cpp`, plus four adapters:
`OnPlayerCanInitTrade`, `OnPlayerCanSendMail`, `OnPlayerCanPlaceAuctionBid`,
`OnPlayerBeforeDurabilityRepair`. Two new dispatch points,
`IMechanic::Allows(Ctx&, Restricted)` and `OnRepair`.

Self-found's three vetoes are one callback with a switch rather than three
near-identical virtuals. Each refusal writes its own chat line, because the core
answers a vetoed trade with a generic client error and a vetoed mail with
nothing at all — a veto the player cannot account for is indistinguishable from
a broken button.

### Step 3 — Blood Magic (`9bec623`)

Two narrowings the card does not state, both argued in the file: **mana cost,
not any power cost** (otherwise a feral druid pays health for every rage ability
in bear form), and **through `Unit::DealDamage`, not `ModifyHealth`**, so the
player gets a red number and a combat log line instead of a bar that silently
drops.

### Step 4 — the bargains, and the slot (`fa978cd`)

`bargain/{LastRites,CursedHoard}.cpp`, one new dispatch point each
(`OnHeal`, `OnLootGroupAmount`), the tier-gate fix, the slot-loop fix, the
re-cut invariant ceilings, `GeneratorVersion` 4 → 5.

---

## 2. Build and test commands, and their results

```
$ ./tests/compile-check.sh
ANCHOR  PASS  25 registered mechanic(s), every one anchored in AnchorMechanics()
COMPILE PASS  41 object(s)
LINK    PASS  41 objects, no duplicate definitions
```

Warning-free. Six new translation units; 35 objects at the end of step 1, 41 now.

```
$ ./tests/run-tests.sh
[==========] 103 tests from 16 test suites ran. (37337 ms total)
[  PASSED  ] 103 tests.
```

97 → 103: five `Aggregate.*` tests for `RelaxCaps` and one
`Registry.BargainsOpenWhereTheGeneratorSaysTheyDo`.

`luac -p` passes on all four addon files. `Data.lua` regenerated through the
standalone exporter; the only content diff is Lone Wolf's blurb.

Deployment: `sync-to-server.sh`, `docker compose build ac-worldserver`,
`docker compose up -d ac-worldserver`. **No `ac-db-import` run and none needed** —
every piece of state these six hold is a key/value pair in `gauntlet_state`,
which already exists. No migration, no schema change.

**One deployment gap found, and it is not specific to this phase.** A rebuild
does not update the realm's config. `docker-compose.yml:47` bind-mounts
`./env/dist/etc` over the image's own, so `conf/mod_gauntlet.conf.dist` is baked
into the image and then covered by whatever is on the host — the container's copy
was four hours stale and had none of this phase's key in it. The module still
behaved correctly, because `GetOption` falls back to the compiled default, but
the key the user is meant to turn was not there to turn.

Fixed by hand for this deploy: the live `.conf` differed from the old `.dist` by
exactly one line (`Gauntlet.Debug.Enable = 1`), so both were regenerated from the
new `.dist` and that line re-applied. **Any phase that adds a config key must copy
`conf/mod_gauntlet.conf.dist` to `<core>/env/dist/etc/modules/` as a separate
step** and diff the live `.conf` first to see what has been hand-edited.

---

## 3. Deviations, with reasons

### 3.1 Two rows promise a number the clamp would have eaten

Cursed Hoard's curse is a triple against `Gauntlet.Caps.DamageTaken = 2.0`.
Lone Wolf halves the pool against `Gauntlet.Caps.MaxHealth = 0.6`. Left alone,
the first delivers ×2 and the second −40%, both behind a blurb naming a
different number — the same unfelt, misstated scalar this redesign exists to
delete.

`IMechanic::RelaxCaps(self, kind, caps)` lets the mechanic that needs the room
ask for it. **It widens a bound; it is not a bypass.** The product is still
clamped exactly once, so the curse plus five Frenzy stacks plus a Champion reach
3.0 together rather than each being paid on top of it. `Mgr::EffectiveCaps`
evaluates it per query under the same condition gate the factors get, so the
ceiling returns on its own the moment the state does.

Exactly two mechanics override it. Five tests in `AggregateTest` cover the
default no-op, the widening, clamp-once with three stacking factors, that a
`DamageTaken` relaxation does not touch the `MaxHealth` floor, and the
−50%/−40% case directly.

### 3.2 Blood Magic's own damage is not the world's

Nothing downstream can tell a self-inflicted spell cost from a blow the world
landed — the attacker is the player either way. `RunState::selfDamage` is raised
across the one `DealDamage` call, and `Mgr::OnDamageTaken` and `Mgr::OnLethal`
both return early while it is up. Without it a caster would pay for a spell and
then have the payment taxed a second time as a Deep Wound: a tax on a tax, which
is the pattern the design's own note on Feeble rejects.

### 3.3 The two bargain tier gates disagreed, from Phase 0

Cursed Hoard's registry row said tier 4. `BARGAIN_MIN_TIER` said 6. The
generator checks both, so the constant won and two tiers of the card's window
were dead letter — the quietest kind of wrong: nothing fails, the affix is
simply never offered where its own row says it should be.

The row moved to 6 (the design's family-B header says bargains open at tier 6),
`BARGAIN_MIN_TIER` moved to `GauntletGenerator.h` so a test can see it, and
`Registry.BargainsOpenWhereTheGeneratorSaysTheyDo` now holds them together.

### 3.4 The bargain slot was buying itself with the offer set's guarantees

**The largest finding of the phase, and it only appeared because the slot had
never run before.**

Family B has two rows, one of them in window below tier 8, and `CAP_BARGAIN`
retires both for the rest of the run. So a slot that demands a bargain one time
in six very often found one it could place only by relaxing a rule — and took
it. Turning the slot on moved tiers 6–14 from an exact zero to 3–7% relaxed.

A negative control settled it in one run: disabling the 1-in-6 roll took tiers
1–11 straight back to zero, so every point of the new relaxation was that slot
and none of it was the pool.

The slot loop now prefers an ordinary new mechanic in a clean family over a
bargain it can only place by relaxing. A bargain is a bonus, never a
requirement, and it must not be bought with the distinct-family guarantee. This
is the same preference Phase 2 added for New over RankUp, for the same reason.

### 3.5 A heal ceiling is not a multiplier

`HealTakenMult` returns a ratio and never sees the heal, so "cannot be healed
above half" comes out as all-or-nothing — and "all" at 49% health takes the
player to full, straight past the line the card draws.

`IMechanic::OnHeal(Ctx&, uint32& heal)` is handed the amount after the aggregate
and after plan §2.5's floor, and lowers it to exactly the gap. Post-cap on
purpose: the floor exists to stop curses stacking into "you cannot heal at all",
and an absolute ceiling is not curses stacking. Healing from 10% to 50% is the
whole of what a player can do about the Mark, and the card takes away the half
above, not the half below.

### 3.6 Lone Wolf does not implement its card

Taken as a decision before the phase, at the user's direction.

The card is "you cannot join a group, +20% experience". What shipped: **you may
group; while you are in one your maximum health is halved; alone you gain the
card's 20%.**

The row's window is tiers 1–6, which with `TierInterval = 5` is levels 5 to 30 —
Ragefire Chasm, Wailing Caverns, Deadmines, Shadowfang Keep, the Stockade and
Gnomeregan. The card does not make those levels harder, it removes them, for the
remaining seventy levels of the run. The design's own note says as much and
offers a server switch as the answer, which trades a bad affix for an absent
one. Design §2.9 rejects affixes whose only instruction is "don't"; a permanent
veto is the purest example of one.

Any group counts, bots included — a bot party on a realm running five hundred of
them would otherwise be a free bypass.

The curse rides `OnMaxHealth` rather than `AggregateFactor`, which is
Player-free and cannot see a group, and it sets the flag `RelaxCaps` reads *in
the same call it is read in*, so the floor can never be a refresh behind. The
`GroupScript` makes the change instant; a tick reconciler makes it certain,
because a realm running playerbots has paths that add a member without going
through all of the core's own.

### 3.7 Cursed Hoard gains an exit the card does not have

Taken as a decision before the phase, at the user's direction. The curse lifts
on three kills **or** after `Gauntlet.Bargain.CursedHoard.EscapeSeconds` out of
combat, default 10.

**Said plainly, because it is the most consequential number in the phase: at ten
seconds the bargain is close to free.** A player can open the chest, walk away,
wait, and keep the doubled loot. What buys that is a hardcore realm where a
genuine ×3 with no exit ends a run on one unlucky pull. The key is there to be
turned once it has been played; `0` restores the card exactly — three kills or
nothing — and is a one-line config change with no rebuild.

Opening a second chest while cursed refreshes the debt rather than stacking it.
Stacking would make the affix a death sentence for the one mistake it exists to
make survivable.

### 3.8 Iron Purse is the weakest row in the table, and stays

Tiers 1–3 is levels 5 to 15. A repair bill there is a few silver, doubling it is
a rounding error against the first quest reward, and on a hardcore realm the
player only ever dies once, so the durability loss that makes repairs matter
hardly arrives.

It earns its place on an argument that has nothing to do with how it feels: tier
1 carried four offerable rows across exactly three families against three
distinct-family slots, with no slack at all, and this is one of three cheap rows
that widen it. **That is a real argument and it is not the same as the affix
being good.** If a later phase wants an id back, this is the row to spend.

Its boon stays `Boon::None` against the family rule that says rules always carry
one. The obvious boon for a gold tax is more gold, and a curse and boon that
cancel are worse than either alone.

One trap worth recording: `discountMod` is named like a discount and spent like
a multiplier (`NPCHandler.cpp:780-782` → `Player.cpp:4955`), so the bill is
doubled by *raising* it. Reading the name instead of the call site turns the
affix into a 50% repair discount — a curse that is quietly a reward, invisible
until someone reads the gold. It was written the wrong way round first and
caught by reading the call site.

### 3.9 Smaller ones

- **Self-found leaves existing property alone.** Mail already in the box and an
  auction already bid on both resolve normally. Confiscating what a character
  owned before the rule existed is a ruder affix than the card, and the card
  names three verbs — trade, mail, bid — not a seizure.
- **The Mark persists paused, not as an expiry.** `State` holds integers and the
  module has no wall clock. A player who logs out at nine minutes comes back
  owing nine minutes, which is the *strictest* reading, not the cheese the other
  direction would have been.
- **Only one Rules affix can be carried at a time.** The three share the
  exclusive key `"rule"`, from Phase 0. Design says "at most one or two"; one is
  inside that. Untouched this phase.
- **No sound for Last Rites.** Ambush needed one because its telegraph is four
  seconds of counterplay that can be missed. A cheat death cannot be missed —
  the player is at 1 health — so the two chat lines, the `EVT` and the `STAT`
  are the whole telegraph, and no core sound id had to be guessed at.

---

## 4. The invariant sweep

### Live view — what a player is actually offered

25 mechanics across 6 families, 160,000 sets (1,000 seeds × 10 classes × 16
tiers), each tier's carried set built from the run's own previous picks.

```
tier      1     2     3     4     5     6     7     8
P2    0.000 0.000 0.000 0.000 0.050 0.120 0.460 13.61
P3    0.000 0.000 0.000 0.000 0.040 0.000 0.000 0.000
tier      9    10    11    12    13    14    15    16
P2    1.100 1.210 8.670 45.66 40.94 63.38 95.19 99.08
P3    0.000 0.000 0.140 4.300 0.930 3.480 57.23 86.14
```

**Every one of the sixteen tiers improved.** Tier 8 — a swap tier — went from
13.61% to nothing at all. Tier 12 fell from 45.66% to 4.30%, tier 13 from 40.94%
to 0.93%, tier 14 from 63.38% to 3.48%.

**I was too pessimistic about the tail in `docs/phase-3-prompt.md`.** It
predicted tiers 15–16 would go from four rows to five and "barely move". They
went 95.19% → 57.23% and 99.08% → 86.14%. Two reasons the prediction was wrong:
the six new rows are rank-3 rows that keep supplying rank-ups long after they
are carried, and §3.4's slot fix helped at every tier rather than only where the
new rows sit. The tail is still the worst part of the table and is still Phase
4's to close.

The ceilings in `OfferInvariantsTest` are re-cut to match — headroom for a
registry edit, none for a regression. Leaving them at Phase 2's values would
have let the whole improvement be given back silently by a later phase.

**Exact zero is now asserted for tiers 1–4 only, and that is a deliberate
loosening from the draft.** Tiers 5–9 measured exact zero before
`GeneratorVersion` moved to 5 and tier 5 then measured 0.04% — four sets in ten
thousand. That is a property of those seeds, not of the pool, and asserting zero
where it is not structural makes the suite fail the next time the version moves.
Tiers 5–10 sit under a 0.5% ceiling instead.

### Full table — 1.6 M sets over all 69 rows

Unchanged from Phase 2 (2.436% at tier 6, 78.465% at tier 16, and so on) — that
census includes unimplemented rows by construction, so implementing six of them
moves nothing. It is recorded here only to say it was checked.

### Negative controls — three, all firing

| Control | Caught by |
|---|---|
| The 1-in-6 bargain roll disabled | tiers 6–14 fall to exact zero, proving §3.4's cause |
| §3.4's strict-New preference removed | live tiers 6, 7, 9 (exact-zero) and 10, 11, 13 (ceilings) |
| Cursed Hoard's window put back to the card's tier 4 | `Registry.BargainsOpenWhereTheGeneratorSaysTheyDo`, naming id and key |

---

## 5. The `TODO(design)` list

**One new marker**, against Phase 2's thirty:
`Gauntlet.Bargain.CursedHoard.EscapeSeconds`, the ten seconds out of combat that
lift the curse (§3.7). Every other number in the six mechanics comes from a card.

The 17 in `GauntletGenerator.cpp` and 14 in `GauntletRegistry.cpp` are inherited
and unchanged.

---

## 6. What does not work, and what is unverified

**Nothing has been seen on a screen.** I cannot drive a WoW client. Everything
below reads correct, compiles, links, and passes its tests, and none of that is
a playtest.

**In priority order for a first session:**

1. **Last Rites' save.** Whether `OnLethal`'s return really reaches
   `Unit::DealDamage` and leaves the character at 1 health rather than dead.
   This is the single highest-stakes line in the phase: if the return is
   discarded somewhere between the two, a player is told they were saved and
   then dies, on a hardcore realm. `.gauntlet debug dump` prints the charge and
   the Mark's remaining seconds.
2. **The Mark's heal ceiling.** Whether `OnHeal` clamps a heal that would cross
   50%, and whether a heal *below* the line still lands in full.
3. **Cursed Hoard's ×3.** Whether the relaxation actually lifts the ceiling in
   the live aggregate — `.gauntlet status` prints the products — and whether it
   lapses when the curse does. Test at low level with something survivable.
4. **The loot doubling.** Whether `OnAfterCalculateLootGroupAmount` visibly
   doubles a chest. This is the half that makes anyone open one.
5. **Lone Wolf's health.** Whether the pool halves on joining and returns on
   leaving, within half a second, and whether the HUD row appears and clears.
6. **Blood Magic's red number.** Whether `Unit::DealDamage` on self produces a
   visible hit and does not interrupt anything it should not.
7. **Self-found's three refusals.** Trade, mail, auction bid, each with its line.

**Known-imperfect, by decision:** Cursed Hoard's ten-second exit (§3.7); Iron
Purse (§3.8); the tier 15–16 tail (§4); `docs/checklists.md` still unwritten.

**Carried over from Phase 2 and still unverified**, deliberately not this
phase's job: Death Rattle's circle, Falter's disarm and silence, Reinforcements'
copies, Craven's flee, Nimble's speed, Echo's clone.

**Remember `.gm off`.** The account is `gmlevel 3`, and several mechanics refuse
while `IsGameMaster()` is true. This cost most of an evening in Phase 2.

---

## 7. What Phase 4 should know

1. **`mechanics/Charges.h` is yours.** Ankh Pact and Stone of the Damned are
   Last Rites' sentence with a different verb, which is why the per-level
   accounting is a shared helper. `Charges::Available/Spend/ReturnsAtLevel/Clear`.
2. **The resurrect seam is still unspent, on purpose.**
   `OnPlayerResurrect` remains empty and `Mgr::CancelPendingDeath` remains
   uncalled. Last Rites is a cheat death and never enters the pending-death
   window; the two class bargains are the ones that resurrect, and they are
   yours. The comment in `GauntletScripts.cpp` says so.
3. **`RelaxCaps` is not a general escape hatch.** Two mechanics use it, both
   because a card names a number a clamp would eat, both state-dependent and
   self-lapsing. If a class curse wants one, the test to apply is "does the
   blurb promise a number the player will not receive" — not "is this affix
   meant to be strong".
4. **The offer builder now has three preferences, and they interact.** New over
   RankUp (Phase 2), and strict-New over relaxed-Bargain (§3.4). A fourth will
   need the same treatment: measure, add a negative control, re-cut the
   ceilings. Do not add one without re-running the sweep.
5. **Forty-four class curses is the tail.** Tiers 15–16 are at 57% and 86%
   relaxed with 25 rows. They are the only thing left that closes it, and the
   numbers in §4 are the baseline to beat.
6. **Every new dispatch point this phase added is generic**, not bargain-specific:
   `OnLethal`, `OnSpellCast`, `OnHeal`, `OnRepair`, `OnLootGroupAmount`,
   `Allows`, `RelaxCaps`, `OnGroupChanged`. Several class curses want them.
7. **Run `tests/compile-check.sh` on every file.** 0.5–7 s, and the anchor audit
   caught both new families before the first Docker build. Still the highest
   leverage thing in the repo.
8. **A rebuild does not deploy a config key.** See §2. Copy the `.dist` to
   `<core>/env/dist/etc/modules/` yourself, and diff the live `.conf` before you
   overwrite it — `Gauntlet.Debug.Enable = 1` is a hand edit on this realm.
