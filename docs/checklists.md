# In-game checklists

Plan §5.3 asked for these in Phase 0 and no phase wrote them. That mattered
less while the module had four mechanics; it matters now, because **sixty-nine
mechanics have been written, compiled, linked, unit-tested and deployed, and
almost none of them has been seen working on a screen.** Everything below reads
correct. None of that is a playtest.

The file is meant to be worked through, not read. Each entry is short on
purpose: the generic checks are stated once, at the top, and each mechanic gets
only what is specific to it — the setup line, what to look for, what changes at
rank III, and the one thing most likely to be wrong.

---

## 0. Setting up

`Gauntlet.Debug.Enable = 1` in `mod_gauntlet.conf`, and a `SEC_GAMEMASTER`
account. Nothing below stakes a real run.

```
.gauntlet debug give <key|id> [rank]   # attach an affix at a rank
.gauntlet debug give-class [rank] [family]  # every affix your class can be offered, at once
.gauntlet debug rank <slot> <n>        # move a carried affix to rank n
.gauntlet debug remove <slot>          # detach one
.gauntlet debug dump                   # affixes, aggregate products, scheduler, summons, state
.gauntlet debug fire <key>             # skip the clock, keep the telegraph
.gauntlet debug events on|off          # silence the scheduler while testing something else
.gauntlet debug offers <tier>          # what the generator would offer, committing nothing
.gauntlet debug set <state-key> <n>    # push a counter to the edge of its trigger
.gauntlet debug hurt <percent>         # test a death path without staking a run
.gauntlet debug cards [key]            # every card at every rank: dead ranks, and words the addon cannot split
.gauntlet debug leaks [what] [rank]    # attach and detach every affix: what did not come back
.gauntlet debug leaks self             # check the audit can see this character before trusting it
.gauntlet status                       # what the player sees
.gauntlet top                          # the leaderboard, and now the conducts
```

Keys are the registry's own (`shade`, `c13_cold_trail`, …) and are listed in
`src/GauntletRegistry.cpp` next to each id.

**Run `.gauntlet debug leaks self`, then `.gauntlet debug leaks`, before any of
the sections below.** The first answers whether the audit can see the character
at all -- Capture reads nine getters and a wrong one would return an empty
footprint, which compares equal to every other empty footprint and reports every
mechanic clean. The second attaches all sixty-nine affixes at their top rank,
detaches each one, and prints whatever the character did not get back: a
cooldown still held, an aura still applied, a summon still standing, a
multiplier still bent.

It is the cheapest check in this file that needs a character, and it covers, in
one command, the "does detaching it put everything back" half of S1-S8 for every
mechanic at once. What it cannot answer is whether the effect was *right* while
it was on -- that is the rest of this file.

Three verdicts, and the middle one is the one to read carefully:

| Verdict | Meaning |
|---|---|
| `LEAK` | The character is not the way it was found. The lines below it say how. |
| clean | The affix changed something at attach and put all of it back. |
| inert | Nothing measurable changed at attach. A hook-driven mechanic, or a class curse for another class. **Not a pass** -- it means the audit had nothing to look at. |

**Run `.gauntlet debug cards` next, once, and read the tail.** It prints every
mechanic's offer text at every rank and shouts at two things: two consecutive
ranks that read identically — a rank-up that costs a tier and changes nothing —
and a word longer than the addon's 200-byte chunk, which the wire splits
mid-word and rejoins with a space that was never there. It found one on
its first run (Half-Tamed I and II) and it is the cheapest check in this file:
one command, no character, no run staked.

**`.gauntlet debug give-class` is how you set up a class's section below.**
`give-class` alone attaches every class curse your character can be offered at
rank I; `give-class 4` does it at rank IV; `give-class 3 all` attaches every
relevant affix of every family. It reports which of them are gated on a spell or
talent tree you have not got — those are carried and inert until you train it,
which looks exactly like a broken curse and is not one.

**Every mechanic's `Diagnose()` is in `.gauntlet debug dump`.** It reports what
that mechanic has actually done this run — fires, kills counted, spells refused
— and it is the fastest way to tell "the trigger never ran" from "the trigger
ran and did nothing". Check it before concluding anything.

---

## 1. The standard sweep

Plan §5.3's list. It applies to **every** mechanic, so it is not repeated in the
entries below. Run it once per mechanic; the per-mechanic entry says only what
is specific.

| # | Check |
|---|---|
| S1 | The trigger fires when the card says, and does not fire when it does not |
| S2 | The telegraph is visible: emote, chat line, or an `EVT` countdown on the HUD |
| S3 | The counterplay works — the named button or the named movement, tried deliberately |
| S4 | A death caused by it names it (`KILLBY`, and the leaderboard's `cause`) |
| S5 | Anything it summoned despawns on logout, on your death, on a zone change, and on leash |
| S6 | Nothing fires while mounted, in flight, in a sanctuary, or inside `Gauntlet.Grace.Seconds` of login |
| S6b | **The interval on a card is a base, not a promise.** The scheduler multiplies every *paced* interval by the event budget — `1 + 0.25 × (timed affixes − 1)` — so a card saying "every 20 seconds" acts every 45 on a run carrying six. `.gauntlet status` prints the multiplier and the addon's footer shows it. A *fixed* interval — Death Rattle's two-second fuse, Ambush's and Carrion's telegraph — is exempt and must be exactly what it says at any carried-set size |
| S7 | Every rank differs from the one below it in the way the entry says, and `.gauntlet status` says so too. **There are four ranks**, except where an entry says "three ranks only" — those eight stop at III because rank III already ends their ladder, and `.gauntlet debug offers` must never offer them a IV |
| S8 | The boon applies, and the number in the offer matches the number delivered |
| S9 | The addon shows the state — HUD counter, panel row, or countdown |
| S10 | A bot in the same zone is untouched (`IsEligible` is false for them) |
| S11 | A grouped player without the Gauntlet is unaffected, except where the card says otherwise |

S6 is the one most likely to be quietly broken and the least likely to be
noticed, because a mechanic that fires in a sanctuary looks like a mechanic that
fires.

---

## 2. Priority

Test in this order. The list is what Phase 4's report ranked, updated for what
Phase 5 changed.

1. **`PermanentCooldown` greying a client button.** Five wave-A curses are built
   on it. `.gauntlet debug give c13_cold_trail 3` on a rogue: **Vanish must go
   grey and stay grey through Preparation.** If it does not grey, C10, C13, C18,
   C23 and C24 all need a different shape. This is still the single most
   load-bearing unverified thing in the module.
2. **The two class bargains.** `.gauntlet debug hurt 100`, then Reincarnation or
   a Soulstone. The run must survive, and `.gauntlet status` must show every
   boon at zero (Ankh Pact) or something standing next to you (Stone).
3. **Half-Tamed and Fel Pact turning a pet hostile.** The real pet must be
   callable again afterwards.
4. **Grave Call's Risen** (creature 900006), the one new template.
5. **The new leaderboard tab.** `/gauntlet top` with an empty
   `gauntlet_leaderboard` must say "No completed runs yet" rather than hang on
   "Asking the server…", and a run with conducts must show them on hover.
6. **The seven family switches.** `Gauntlet.Family.Class.Enable = 0`, reload,
   `.gauntlet debug offers 40` — no class curse may appear, and a carried one
   must keep working.
7. **One Totem's cull**, and whether the tick-based timing looks instant.
8. **The spell ids.** Most could not be confirmed off a running client, because
   3.3.5 spells live in DBC rather than SQL. A wrong id is not a crash: that one
   curse silently stops reacting to that one spell, and `Diagnose()` will show a
   counter stuck at zero.

---

## 3. Family S — spawn

**S1 · The Shade** (`shade`, 1) · tiers 20–80
Rises every 15 / 10 / 7 / 5 min and hunts you. Health 1× / 1⅓× / 1⅔× / 2× yours.
The named nemesis starts at rank III and stays there — it is not gated on the
top rank, deliberately.
- It must path to you across a zone, and give up when you outrun it far enough.
- Kill it: the timer restarts, it does not immediately re-spawn.
- Exclusive with Echo (`stalker`); a run may never carry both.
- Boon is bonus experience — check the XP number on a kill.

**S2 · Echo** (`echo`, 2) · tiers 30–80
Every 30th / 25th / 18th / 12th kill returns as an echo of you, at 1× / 1.25× /
1.5× / 1.75× the template health.
- `.gauntlet debug set echo 29` to stand it up on the next kill.
- The echo must use your class's abilities, not a generic melee.
- Reward-shaped: killing it must pay. Check the XP.

**S3 · Carrion** (`carrion`, 3) · tiers 1–50
Every 5th / 4th / 3rd / 2nd corpse looted draws a pack of 2 / 2 / 3 / 4.
- It counts **loots**, not kills. Kill five without looting: nothing.
- Boon is money; check a purse before and after.

**S4 · Reinforcements** (`reinforcements`, 4) · tiers 25–80
A fight past 45 / 30 / 20 / 15 s draws another enemy every 15 / 15 / 12 / 12 s,
up to 2 / 3 / 4 / 6 — bounded by `Gauntlet.Summons.MaxAlive` across every spawn
mechanic together.
- The repeat stops at twelve because `Gauntlet.Events.MinSpacing` is twelve: a
  cadence below the floor cannot be delivered, so ranks III and IV escalate on
  the first arrival and the cap instead. If you lower `MinSpacing`, this ladder
  should move with it.
- **Known live bug once, fixed:** it fired once per session because `Arm()`
  armed a constant event id. Stay in one fight for three minutes and confirm it
  repeats, then start a second fight and confirm it repeats there too.
- Leaving combat must cancel the pending arrival, not bank it.

**S5 · Ambush** (`ambush`, 5) · tiers 4–45
Standing still out of combat for 30 / 20 / 12 / 8 s attracts an ambusher.
- Standing still **in** combat must not trigger it.
- Not a stalker for exclusivity purposes: Ambush + Shade is a legal set, on
  purpose. If a run is ever refused that pair, the `stalker` key has leaked.

---

## 4. Family E — enemies that behave differently

**E1 · Champions** (`champions`, 6) · tiers 1–80
Every 10th / 8th / 6th / 4th fight opens against a Champion with 2× / 2.5× / 3× /
4× health.
- The champion must be a **promoted real mob**, not a summon: full XP, full loot.
- Check the health multiplier against the same mob type nearby.

**E2 · Craven** (`craven`, 7) · tiers 12–60
Enemies flee at 20% / 25% / 35% / 50% health and fetch 1 / 1 / 2 / 3 friends.
- The flee must be a real flee (it runs, it can be chased down), not a despawn.
- **Pair test with Call to Arms — see §9.**

**E3 · Call to Arms** (`call_to_arms`, 8) · tiers 25–65
A kill alerts kin within 20 / 30 / 40 / 50 yd; 1 / 1 / 2 / 3 answer.
- The grid search runs on the 500 ms tick. Watch the server's CPU with several
  players in one zone; the plan's fallback is a 1 s cadence per config.

**E4 · Death Rattle** (`death_rattle`, 9) · tiers 20–60
Corpses burst 2 s after death for 8% / 12% / 18% / 25%, within 5 yd.
- Two seconds is the counterplay: step back and take nothing.
- **It must be two seconds with a full carried set too.** The fuse is
  `Pacing::Fixed`; before Phase 7 the budget stretched it and the spacing could
  walk it out to twelve. Kill three of a pack at once with five other timed
  affixes carried: all three must burst about two seconds after their own kill,
  not twelve seconds apart.
- Exclusive with Grudge (`onkill-positional`).

**E5 · Grudge** (`grudge`, 10) · tiers 8–50
A ghost on each corpse drains 2% / 3% / 5% / 7%.
- The blurb was rewritten once because a player asked what "saps" meant. Confirm
  the current wording reads as damage from the corpse.
- Walking away must stop it.

**E6 · Nimble** (`nimble`, 11) · tiers 30–80
Enemies move 20% / 30% / 40% faster. **Three ranks only** — 40% is exactly
`Gauntlet.Caps.EnemySpeed`, so a fourth could not do anything and the row says
so. Confirm `.gauntlet debug offers` never offers a rank IV of it.
- Check against `Gauntlet.Caps.EnemySpeed` (1.4): stacking with anything else
  that moves enemies must clamp, not multiply past it.

**E7 · Cunning** (`cunning`, 12) · tiers 30–80
Melee enemies kick your cast every 15 / 12 / 8 / 6 s, locking the school for
2 / 3 / 4 / 4 s. The lock deliberately stops at four: four seconds in six is
already two thirds of a caster’s uptime.
- Only relevant to casters — a warrior must never be offered it.
- Exclusive with Falter (`roletax`). **See §9.**

**E8 · Keen-nosed** (`keen_nosed`, 13) · tiers 4–55
Aggro radius +5 / +8 / +12 / +18 yd.
- Test against a known pull distance, at your level and five levels below.

---

## 5. Family T — tempo

**T1 · Falling Sky** (`falling_sky`, 14) · tiers 25–80
Every 25 / 20 / 15 / 12 s in combat the sky marks your spot; three seconds later
it lands for 25% / 35% / 50% / 65%. The three-second warning does not move at
any rank.
- **Known live bug once, fixed:** 23 telegraphs and 0 strikes, because the
  scheduler's tie-break preferred the lowest mechanic id and starved it forever.
  Carry it with two other timed affixes and confirm it actually lands.
- The three-second gap is the counterplay: move and take nothing.
- Its boon is move speed, delivered as an aura, so the tooltip on that aura will
  not match. The `Describe()` says the real number.

**T2 · Frenzy** (`frenzy`, 15) · tiers 8–80
Kills within 8 s stack +4% / +6% / +8% / +10% damage taken.
- Reward-shaped: the stacks must pay as well as cost. **Pair test with
  Champions — see §9.**
- The stack must decay when the chain breaks, and the HUD must show it.

**T3 · Overextended** (`overextended`, 16) · tiers 1–60
Each attacker past the first adds 15% / 20% / 30% / 40% damage taken. At rank IV
three extra attackers is the whole of `Gauntlet.Caps.DamageTaken` on its own.
- Pull one, then three. Check the aggregate product in `.gauntlet debug dump`
  against `Gauntlet.Caps.DamageTaken` (2.0).

**T4 · Falter** (`falter`, 17) · tiers 25–65
Every 60 / 45 / 30 / 22 s in combat your hands fail for 2 / 3 / 4 / 5 s.
- Exclusive with Cunning (`roletax`).

**T5 · Hubris** (`hubris`, 18) · tiers 1–50
Enemies below your level give 50% / 25% / 0% / 0% experience; above give
+20% / +30% / +40% / +50%.
- **Rank IV changes the rule, not the number**: an enemy at *exactly* your level
  counts as below it, so only something higher pays experience at all. The blurb
  says "at or below" at rank IV and "below" at every other. Check both.
- Rank III is the interesting one: grey and green mobs give **nothing**.
- Reward-shaped, and the reward is the whole point — confirm the bonus on a
  higher-level kill.

---

## 6. Family D — attrition

**D1 · Deep Wounds** (`deep_wounds`, 19) · tiers 10–60
30% / 40% / 50% / 60% of damage taken becomes a wound that only rest heals.
- The maximum-health floor is `Gauntlet.Caps.MaxHealth` (0.6). Take enough
  damage to try to break it and confirm it clamps.
- Sitting must heal it; a healing spell must not.
- The addon shows the wound percentage. Confirm it matches `dump`.
- **Pair test with the Shade — see §9.**

**A5 · Killing Floor** (`a05_killing_floor`, 74) · tiers 10–80
No healing reaches you while something you have wounded is still alive; every
kill gives back 10% / 8% / 6% / 5% of maximum health. Ranks III and IV hold the
block for ten and fifteen seconds after the fight.
- **It replaces Unspent (69), which was retired.** A character carrying Unspent
  had the row deleted by `2026_08_29_03_gauntlet_retire_unspent.sql`; confirm
  `.gauntlet status` shows no blank row and one free slot.
- Wound something, then try a potion, a bandage and a self-heal: all three must
  do nothing, and the first one must print the chat line.
- **Food and drink still work** — they restore through a regeneration aura, not
  a heal, so this never sees them. That is stated in the blurb; it is not a bug.
- Kill it: the burst must land even though healing is blocked (it goes through
  `ModifyHealth`, not through the heal path it would otherwise be eaten by).
- Damage from a Shade or any other module summon must **not** arm the block —
  a stalker you cannot outrun would hold it open for as long as it lives.
- Rank III: leave combat and confirm the block holds ten more seconds, and that
  `.gauntlet debug dump` counts the linger down.
- **Pair test with Deep Wounds** — see §9.

**D2 · Blood Magic** (`blood_magic`, 20) · tiers 25–60
Spells cost 2% / 3% / 5% / 7% of maximum health on top of mana.
- The self-damage goes through `RunState::selfDamage`, so it must **not** make a
  Deep Wound and must **not** spend a Last Rites charge. Carry all three and
  check.
- It must not be able to kill you outright. Cast at 1% health.

---

## 7. Family R — rules

**R1 · Self-found** (`self_found`, 23) · tiers 1–20
No trade, no mail, no auction house.
- Try all three. Each must be refused with a line saying why, not silently.
- The refusal must survive a relog.

**R2 · Lone Wolf** (`lone_wolf`, 24) · tiers 1–30
Half health in a group; more experience alone.
- Join a group: maximum health halves **while grouped**, and comes back on
  leaving. Solo: +20% experience.
- Watch the transition mid-combat — health must clamp, not kill you.

**R3 · Iron Purse** (`iron_purse`, 25) · tiers 1–15
Repairs cost double.
- The core computes `costs * discountMod`, so the multiplier is 2.0. Repair the
  same item twice on two characters, one with and one without, and compare.
- Training costs are **not** implemented (no hook) and the blurb does not
  promise them.

---

## 8. Family B — bargains

**B1 · Last Rites** (`last_rites`, 26) · tiers 40–80
A killing blow leaves you at 1 health instead, once every 1 / 2 / 3 / 4 levels.
- `.gauntlet debug hurt 100`. You must survive at 1 health.
- Then immediately again: the second must kill you.
- The charge must come back on the right level, and `.gauntlet status` must show
  the count.
- Blood Magic's self-damage must not spend a charge.

**B2 · Cursed Hoard** (`cursed_hoard`, 27) · tiers 30–80
Chests give twice the loot; opening one makes you take triple damage until
3 / 4 / 5 / 6 kills.
- **Known live bug once, fixed:** the escape opened immediately, because players
  loot after clearing and the out-of-combat timer started at once. The curse now
  only expires after it has been in a fight. Open a chest, stand still: the
  curse must **not** lift. Then fight: it must.
- `Gauntlet.Bargain.CursedHoard.EscapeSeconds` is the knob (default 10).
- The loot doubling must be visible on the chest itself.

---

## 9. Pair tests (plan §5.4)

On a level-40 character, with both affixes given deliberately.

**Call to Arms + Craven.** Kill one enemy of a pack. Craven makes the survivors
flee and fetch; Call to Arms alerts kin on the kill. The question is whether the
two can chain into a fight that never ends. Watch for: a fetched friend whose
arrival alerts more kin, whose kill fetches again. If the chain does not
terminate within a minute, one of the two needs a per-fight cap.

**Champions + Frenzy.** Champions gives a 3× health enemy; Frenzy wants chained
kills within 8 seconds. They pull against each other, which is the design's
intent. Check that the champion fight does not simply reset every Frenzy stack
and make the pair strictly worse than either alone — and that the damage-taken
product stays under `Gauntlet.Caps.DamageTaken`.

**Killing Floor + Deep Wounds.** The pair this phase added and the one most
likely to be too much: Deep Wounds caps the health you can reach and only rest
lifts it, Killing Floor stops anything reaching it while a fight is open. Check
that a run carrying both can still recover between pulls, and that the kill
burst is not simply eaten by the wound ceiling — a burst that can never land is
a reward-shaped affix with no reward.

**Shade + Deep Wounds.** The Shade is unavoidable damage; Deep Wounds turns a
share of damage taken into a health ceiling that only rest lifts. Together they
are the module's clearest death spiral. Check that the maximum-health floor
(`Gauntlet.Caps.MaxHealth`, 0.6) actually holds while a Shade is chasing you,
and that resting is a real out.

**Cunning vs Falter (role-tax exclusivity).** `.gauntlet debug give cunning`,
then `.gauntlet debug offers 40` repeatedly: Falter must never appear. Both
carry the `roletax` exclusive key, and `OfferInvariants` checks this across 1.6
million sets — this is the in-game confirmation that the same rule reaches the
live path.

**Shade vs Echo (stalker exclusivity).** The same, with the `stalker` key. Note
that Ambush is deliberately **not** in it.

---

## 10. Family C — class curses

Forty-four of them and two class bargains. Each needs the class it belongs to,
and most need a specific ability, so the setup line is `.gauntlet debug give
<key> <rank>` on that class with that ability trained.

**Five of these are built on `PermanentCooldown`** — C10, C13, C18, C23, C24 —
and all five fail the same way if it does not grey the client button. Do §2's
first item before any of them.

**Where an entry says the implementation is narrower than the card**, that is
recorded in the source file too, and it is not a bug to file. It is a decision
with a reason, and the reason is in the file.

### Warrior

**C1 · Red Mist** (`c01_red_mist`, 28) · rage 100 / 90 / 80 / 70
You lose control for three seconds and your rage empties.
- Build rage on a training dummy and watch for the trigger at the exact number.
- The rage must actually empty; losing control without the drain is half of it.

**C2 · Berserker's Bargain** (`c02_berserkers_bargain`, 29) · line 30% / 35% / 40% / 50%
Below the line: +25% damage, and Shield Wall, Last Stand and Enraged
Regeneration are refused. Above it they work.
- The +25% is the boon and is delivered by the mechanic, not the aggregate — the
  offer must not also promise a separate one.
- Cross the line in both directions mid-fight and confirm the buttons flip.

**C3 · Iron Discipline** (`c03_iron_discipline`, 30) · 6 / 10 / 20 / 30 s
Stance changes go on cooldown.
- The card says ten seconds flat; the ladder is 6/10/20 and the `Describe()`
  says the real number. Confirm the offer text matches the delay you get.

**C4 · Deafening Roar** (`c04_deafening_roar`, 31) · 20 / 30 / 40 / 50 yd
Shouts wake every enemy in range.
- Battle Shout in a camp and count what comes. The grid search is on the tick.
- Its registry boon was changed from `BonusRegen` to `BonusAbility` because the
  first would have promised a percentage nothing pays. Check the offer wording.

### Paladin

**C5 · Long Forbearance** (`c05_long_forbearance`, 32) · 2 / 3 / 5 / 8 min
And Divine Shield empties your mana.
- The Forbearance tooltip will still say one minute — `SetDuration` moves the
  client's timer, not the DBC. The `Describe()` says the real number.
- Bubble with full mana: it must go to zero.

**C6 · Consecrated Ground** (`c06_consecrated_ground`, 33) · ×1.15 / ×1.25 / ×1.40 / ×1.60
Damage taken while not standing in your own Consecration.
- The mechanic remembers the spell id of the cast that made the circle rather
  than testing eight rank ids. Cast every rank you know and confirm each is
  recognised.
- Step out and back in mid-fight; the multiplier must follow you.

**C7 · No Sanctuary** (`c07_no_sanctuary`, 34) · **three ranks only**
Rank I: Hearthstone refused under Divine Shield or Hand of Protection.
Rank II: also under Forbearance. Rank III: Divine Shield breaks on your first
attack.
- Rank III is watched at the damage site, not on a cast, because auto-attack is
  the likeliest first blow. Confirm a plain swing breaks it.

**C8 · Commitment** (`c08_commitment`, 35) · 3 / 4 / 6 / 8 s
Hammer of Justice roots you for its duration.
- The root must break when the target dies, or you are stuck for the full time
  with nothing to hit.

### Hunter

**C9 · Half-Tamed** (`c09_half_tamed`, 36) · turns at unhappy / unhappy / content / content
Hostile for 15 / 20 / 25 / 40 s — the happiness threshold stops at content and
the duration carries the ladder.
- **Rank I and rank II were identical before Phase 7** — same threshold, same
  duration, and the offer card for II was the same string as I. Check that
  `.gauntlet debug cards c09_half_tamed` prints four different lines.
- **Priority item.** `RemovePet` then a summon of the same entry: the copy must
  attack you, and the real pet must be callable afterwards.
- Feed it back to happy and confirm it stops.

**C10 · Dead Weight** (`c10_dead_weight`, 37) · 3 min / 5 min / never — **three ranks only**
Feign Death's cooldown. Rank III denies it outright.
- `PermanentCooldown` — the button must go grey.

**C11 · Wide Dead Zone** (`c11_wide_dead_zone`, 38) · 8 / 10 / 15 / 20 yd
Ranged attacks refused inside it.
- **The known risk:** Auto Shot goes through `OnPlayerSpellCast`, and
  interrupting it there may desync the client's auto-repeat state. Step inside
  the zone while auto-shooting, step out, and confirm shooting resumes without
  a re-click.

**C12 · Blood Bond** (`c12_blood_bond`, 39) · 20% / 30% / 40% / 50%
A share of your pet's damage taken is dealt to you.
- Uses `OnPetDamaged`, added in wave B. Send the pet in alone and watch your
  own health fall with no enemy near you.
- The share must not be able to kill you from full through a pet tanking.

### Rogue

**C13 · Cold Trail** (`c13_cold_trail`, 40) · 10 min / 30 min / never — **three ranks only**
Vanish's cooldown. **This is the `PermanentCooldown` test in §2.**
- Vanish must go grey and **stay** grey through Preparation, which re-adds the
  cooldown within a tick.

**C14 · Poisoned Blades** (`c14_poisoned_blades`, 41) · 25% / 35% / 50% / 65%
A share of the poison damage you deal ticks on you.
- Uses `OnPeriodicTick`, added in wave B. Apply Deadly Poison and stand still.
- It must not tick while the target is dead.

**C15 · Exposed Back** (`c15_exposed_back`, 42) · ×1.30 / ×1.50 / ×1.75 / ×2.00
Attacks from behind you.
- The arc test is `Position::HasInArc`. Let a mob get behind you deliberately.
- Its boon is `BonusAvoidance`, delivered as a **full avoid** rather than dodge —
  the combat log will read as a zero, not as a dodge. The wording says "avoid"
  for exactly that reason.

**C16 · Slow Hands** (`c16_slow_hands`, 43) · half / none / none — **three ranks only**
Energy regeneration while moving in combat.
- Needs a server-side `spell_dbc` aura, which is invisible to the client, so the
  addon is the only readout. Confirm the addon shows it and `dump` agrees.

### Priest

**C17 · Frail Soul** (`c17_frail_soul`, 44) · 20 / 30 / 45 / 60 s
Weakened Soul's duration.
- The tooltip lies (`SetDuration`); the `Describe()` has the real number.

**C18 · Faithless Form** (`c18_faithless_form`, 45) · 15 / 30 / 60 / 90 s
Leaving Shadowform goes on cooldown.
- `PermanentCooldown`. Shadowform off, then try to re-enter.

**C19 · Penance of Silence** (`c19_penance_of_silence`, 46) · 2 / 3 / 4 / 5 s
**Narrower than the card, on purpose: it applies a stun, not a silence.** There
is no `UNIT_STATE` for silence — it is an aura mechanic, and applying one needs
a spell id whose tooltip would then describe something else. A stun of the same
length is a heavier price than the card asks for.
- Heal yourself and confirm the stun, and that the `Describe()` says stun.

**C20 · Whispers of the Deep** (`c20_whispers_of_the_deep`, 47) · 15% / 20% / 30% / 40%
Below the line you flee for three seconds, once per fight.
- Once per **fight**: cross the line twice in one fight and confirm the second
  does nothing.

### Death Knight

All four open at tier 60, so a level-60 character is the earliest test.

**C21 · Rune-starved** (`c21_rune_starved`, 48) · ×1.20 / ×1.30 / ×1.40 / ×1.55
Damage taken while all six runes are on cooldown.
- **Its boon is not delivered and the blurb does not promise one.** The card
  spends it on runic power decaying more slowly, which is inside the core's own
  rune tick with no hook. Confirm the offer text promises nothing.

**C22 · Grave Call** (`c22_grave_call`, 49) · 8 / 5 / 3 / 2 s to claim
Corpses you do not claim rise against you.
- **Priority item:** the Risen is creature template 900006, the one new row.
  Confirm it spawns, looks like a ghoul (display 570), and is not level 1.
- Claiming (Death Strike, Blood Strike, or a rune consumed near it) must stop it.

**C23 · Cold Presence** (`c23_cold_presence`, 50) · 6 / 10 / 20 / 30 s
Changing presence costs all runic power and goes on cooldown.
- `PermanentCooldown`.
- **Narrower than the card:** the "+25% presence effects" half is not delivered,
  because it lives inside each presence's own aura with no seam. The blurb does
  not promise it.

**C24 · One Ward** (`c24_one_ward`, 51) · shared cooldown 2 / 3 / 5 / 8 min
Anti-Magic Shell and Icebound Fortitude share a cooldown; rank III adds
Lichborne. Before Phase 7 the cooldown was flat, so ranks I and II were the same
behaviour *and* the same sentence.
- `PermanentCooldown`. Use one and confirm the other greys.

### Shaman

**C25 · One Totem** (`c25_one_totem`, 52)
Rank I: one totem at a time. Rank II: the standing totem dies to one hit.
Rank III: totems cost twice as much. Rank IV: three times.
- **The cull runs on the tick, not at the cast**, because the new totem is not
  in its slot until the spell effect has run. Drop four totems fast and confirm
  the right three go — and that the cull looks instant rather than laggy.

**C26 · Totemic Anchor** (`c26_totemic_anchor`, 53) · ×1.20 / ×1.30 / ×1.40 / ×1.55
Damage taken beyond fifteen yards from your totems.
- Drop a totem, walk away, walk back. Composes with One Totem by design.

**C27 · Elemental Overload** (`c27_elemental_overload`, 54) · ×1.5 / ×2 / ×3 / ×4
Casting the same spell twice in a row.
- Different ranks of the same spell must count as the same spell
  (`GetFirstSpellInChain`). Cast Lightning Bolt rank 1 then rank 6.

**C28 · Spirit Debt** (`c28_spirit_debt`, 55) · 2% / 3% / 4% / 5%
Every hit consumes a shield charge and each costs health.
- Lightning Shield up, take five hits, count the charges and the health.

### Mage

**C29 · Cold Feet** (`c29_cold_feet`, 56) · 15% / 25% / denied — **three ranks only**
Blink costs maximum health; at rank III it is refused outright.
- Rank III uses `PermanentCooldown` — Blink must grey.
- The self-damage shares `RunState::selfDamage` with Blood Magic, so it must not
  make a Deep Wound nor spend a Last Rites charge.

**C30 · Fickle Sheep** (`c30_fickle_sheep`, 57) · 5 / 4 / 3 / 2 s
Polymorph breaks early and the sheep comes back angry.
- "Angry" must mean it comes straight at you, not that it resets.

**C31 · Mana Burn** (`c31_mana_burn`, 58) · 30% / 50% / 100% — **three ranks only**
A share of damage taken also burns mana.
- At rank III every point of damage is a point of mana. Confirm at full mana and
  at zero — at zero it must simply do nothing, not go negative.

**C32 · Arcane Frailty** (`c32_arcane_frailty`, 59) · health ×0.80 / ×0.70 / ×0.60 / ×0.50, damage +20% / +30% / +40% / +50%
- The health cut goes through the aggregate, so check it against
  `Gauntlet.Caps.MaxHealth` (0.6) with Deep Wounds also carried — at rank III
  alone it is exactly at the floor.

### Warlock

**C33 · Fel Pact** (`c33_fel_pact`, 60) · 20 / 15 / 10 / 7 kills
The demon turns on you.
- **Priority item**, same shape as Half-Tamed. The real demon must be
  re-summonable afterwards.
- The counter must reset when it turns, not keep counting.

**C34 · Affliction of the Self** (`c34_affliction_of_the_self`, 61) · 20% / 30% / 40% / 50%
Your curses and corruption tick on you too.
- Uses `OnPeriodicTick`. Apply Corruption and stand still.

**C35 · Shard Economy** (`c35_shard_economy`, 62) · shards drop only from level −2 / +0 / +1 / +2 and up
- **Only the second half of the card is implemented.** The summon-and-Healthstone
  cost is not, because it would double-charge a warlock also carrying Fel Pact.
  Confirm the blurb describes only what happens.
- Kill something grey and confirm no shard; kill something even and confirm one.

**C36 · Shared Blood** (`c36_shared_blood`, 63) · ×1.15 / ×1.25 / ×1.40 / ×1.55 taken
And the demon deals 40% more.
- Dismiss the demon: the penalty must lift. Re-summon: it must come back.

### Druid

**C37 · Bound Skin** (`c37_bound_skin`, 64) · 4 / 6 / 10 / 15 s
Shapeshifting goes on cooldown.
- Uses `OnShapeshift`, a Phase 0 seam that nothing dispatched until Phase 4.
- Its boon is applied in `OnMaxHealth` because it is gated on being shifted.
  Shift and confirm the health changes; shift back and confirm it goes.
- Exclusive with Nature's Toll (`shortcut:shapeshift`).

**C38 · Nature's Toll** (`c38_natures_toll`, 65) · 2% / 3% / 4% / 5%
Kills made as a beast leave you bleeding until you leave forms.
- Exclusive with Bound Skin, deliberately: the card says pairing them would make
  it a tax rather than a rhythm. Confirm the generator never offers both.

**C39 · Commitment of Roots** (`c39_commitment_of_roots`, 66) · one rank only, 8 s
Entangling Roots holds you as long as it holds them.
- Breaking the target's root early must free you too.

**C40 · Two Faces** (`c40_two_faces`, 67) · 20% / 30% / 40% / 50%
Spells weaker by day, claws weaker by night.
- Needs a full in-game day. `.gauntlet debug dump` reports which half is active,
  so check both without waiting for one.

### Everyone

**C41 · Faint** (`c41_faint`, 68) · 2 / 3 / 4 / 5 s
Hitting zero mana in combat blacks you out.
- Mana users only — a warrior or rogue must never be offered it.
- **Its boon is approximated:** the card says "+15% mana regeneration while
  casting", there is no hook on the five-second rule, and it is paid as a small
  per-second top-up in combat. The blurb says "while fighting" for that reason.

**C42 · Unspent** (`c42_unspent`, 69) · a point every 1½ / 2 / 3 levels
And each unspent point makes you weaker.
- Uses `OnTalentPoints`, declared on `IMechanic` since Phase 0 and dispatched
  from nowhere until wave B. Level up and confirm the grant rate.
- Spend the points: the penalty must lift.

### The two class bargains

**C43 · Ankh Pact** (`c43_ankh_pact`, 70) · shaman, tiers 40–80, **one rank**
Reincarnation works once in this run, and every boon you carry burns out.
- `.gauntlet debug hurt 100`, then Reincarnate. The run must survive and
  `.gauntlet status` must show **every** boon at zero.
- Die again: it must be gone. It is once per run, not once per level.
- Asking and paying are separate: `WillBuyDeath` runs from the resurrection veto
  before the core has committed, so a charge must not be spent on a
  resurrection that never happened. Cancel the resurrect and confirm the charge
  survives.

**C44 · Stone of the Damned** (`c44_stone_of_the_damned`, 71) · warlock, tiers 40–80, **one rank**
A Soulstone brings you back once, and whoever killed you is waiting.
- **The killer is recorded on every blow, not on the killing one**, because by
  then the attacker may have wandered off or despawned.
- It is not persisted: relog while dead and the second life is free, with
  nothing waiting. That is by design, and worth seeing once so it is not
  mistaken for a bug later.
