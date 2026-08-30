# Redesigning the six taxes, and the reward that was a ledger entry

Six cards were called out from play as bad: Killing Floor, Deep Wounds, Falling
Sky, Overextended, Hubris, Frenzy. They are not six unrelated mistakes. They are
one mistake made six times.

---

## 0. What is wrong with all of them

**They are taxes, not decisions.** Nothing happens that the player reacts to; a
number simply gets worse and stays worse. The design doc already made this exact
criticism of Forgetful in §5 — "no moment, no verb" — and then Hubris was written
to replace it and made the same mistake in a different currency.

Card by card, the specific failure:

| Card | The fault |
|---|---|
| **Killing Floor** | Removes healing outright. Taking away a core verb is not a cost, it is an amputation — and the compensation arrives on a kill you may not live to make. |
| **Deep Wounds** | Invisible accumulation, and the counterplay is *travel to an inn*. A chore is not gameplay. |
| **Falling Sky** | A metronome. Every 20 seconds, regardless of what you are doing, you stop and step sideways. Waiting for a beep is not a decision. |
| **Overextended** | A passive multiplier keyed on how many things are hitting you — which is very often not your choice. Patrols and adds are punished the same as bad pulls. |
| **Hubris** | A routing tax. It never acts; it just makes ordinary play worth less and tells you to go somewhere else. |
| **Frenzy** | The closest to good — it *has* a decision — but it punishes the thing it rewards. Chain-pulling raises damage taken, so the card fights itself. |

## 1. The two rules the replacements follow

1. **A card must create a moment and hand the player a verb.** Something happens;
   you do something about it. "You now take 20% more damage" is not a moment.
2. **Cards should chain.** One card's counterplay should be another card's cost.
   A set that interlocks is worth more than six cards that each work alone,
   because carrying two of them should change how you play, not just add two
   numbers.

## 2. The six

| Card | Was | Becomes | The verb |
|---|---|---|---|
| **Killing Floor** | No healing while a wounded enemy lives | Healing is **banked**, not blocked — it lands when the fight ends, or on a kill | Break off and cash in, or push on |
| **Deep Wounds** | Damage becomes max-health loss until you rest in an inn | Wounds close on **kills**, not on travel | Keep killing |
| **Falling Sky** | Every 20s the sky marks your spot | The sky marks **where you have stood still** | Keep moving |
| **Overextended** | Each extra attacker raises damage taken | Enemies **behind you** hit harder | Face them; use terrain |
| **Hubris** | Enemies below your level give no XP | The enemy you **open on** is your duel — less damage from it, more from everything else | Choose your opening target |
| **Frenzy** | Kills stack damage dealt *and* taken | Kills stack damage only; **any damage taken breaks the chain** | Keep the chain clean |

### How they chain

- **Kills are the currency of three cards at once.** Killing Floor's bank is
  cashed by a kill, Deep Wounds' wounds close on a kill, Frenzy's chain is built
  by a kill. Carry all three and the run has one verb: *keep winning fights*.
- **Falling Sky now threatens Frenzy, not just health.** The mark forces
  movement, and taking a hit from it breaks the chain — so the sky costs you
  something even when you survive it.

  (The design first had Frenzy stacks grant movement speed as well. Dropped:
  player movement speed is not an `AggregateKind`, and shipping a no-op
  `AggregateFactor` to stand in for it would have been a lie in the source. The
  chain pays damage; speed is what `Boon::BonusMoveSpeed` is for.)
- **Overextended and Hubris both make where you are pointed matter.** One is
  facing, one is target selection. Together they turn a pull into a plan.
- **Killing Floor plus Deep Wounds is deliberately brutal**: nothing heals until
  you kill, and nothing you have taken closes until you kill. That is a real
  anti-synergy, and it is the offer builder's job to price it — not a reason to
  soften either card.

## 3. Rewards: gold was a ledger entry

Five cards paid `Boon::BonusMoney`. Gold is not felt while playing. You notice it
at a vendor, an hour later, in a different zone — which makes it the worst
possible answer to "what am I getting for carrying this curse".

Every one of them now pays something that is felt in the moment:

| Card | Was | Becomes | Why |
|---|---|---|---|
| Carrion | +% gold | **+ movement speed** | The corpses are being eaten; get there first |
| Death Rattle | +% gold | **+ damage** | Forced off every corpse; damage pays for the risk |
| Keen-nosed | +% gold | **+ movement speed** | Outrun what you woke |
| Self-found | +% gold | **+ damage** | Gold you cannot spend on gear is a joke, not a boon |
| Cursed Hoard | +% gold | **+ damage** | Thematic, and still a ledger entry |

`Boon::BonusMoney` stays in the enum — live characters have it stored in
`gauntlet_affix` — but nothing declares it any more and `BoonMagnitude` pays it
nothing. The `BoonMoneyMult` plumbing in the addon totals stays: it costs
nothing, and it is the seam to use if a gold boon is ever wanted again.

## 4. State

All six are in, with the rewards, across three commits. None of it has been
played yet — the numbers in every ladder are judgement, not measurement, and the
first thing worth doing is finding out which of them is wrong.

The two most likely to need tuning: Falling Sky's `STILL_MS` at rank IV (three
seconds is very little for a caster) and Killing Floor's `LEAVE_LOSS_PCT` at
rank IV (losing half the bank may make breaking off never worth it, which would
put the card back where it started).
