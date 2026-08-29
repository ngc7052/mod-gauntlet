# Phase 4 — running plan and progress

Live working document. Updated as each step lands so an interrupted session can
be resumed from it, and so the user can see on return what was done and what was
decided without reading twenty commits.

## The plan

| # | Step | Ids | State |
|---|---|---|---|
| 1 | The three shared primitives | — | **done** `b960fb9` |
| 2a | Warrior: C1, C2, C4 | 28, 29, 31 | **done** `9b6b16c` |
| 2b | Paladin: C5, C6 | 32, 33 | **done** |
| 2c | Hunter: C9, C10, C11 | 36, 37, 38 | **done** |
| 2d | Rogue: C13, C15 | 40, 42 | **done** |
| 2e | Priest: C17, C20 | 44, 47 | **done** |
| 2f | Death Knight: C21, C22 | 48, 49 | **done** |
| 2g | Shaman: C25, C26 | 52, 53 | **done** |
| 2h | Mage: C29, C31 | 56, 58 | **done** |
| 2i | Warlock: C33 | 60 | **done** |
| 2j | Druid: C37 | 64 | **done** |
| 2k | Common: C41 Faint | 68 | **done** |
| 3 | Class bargains: Ankh Pact, Stone of the Damned | 70, 71 | **done** |
| 4 | The tier windows, and the exclusive key behind them | — | **done** |
| 5 | Class curses recorded as conducts | — | **done** (already built) |
| 6 | `docs/phase-4-report.md` | — | **done** |

Wave A is twenty-one curses (the design's build-priority A). Wave B is not this
phase.

## Standing rules for this run

- **One commit per class**, so a bad curse is one revert.
- **`tests/compile-check.sh` on every file before committing.** The anchor audit
  is what catches "the mechanic is offered and does nothing", which is the
  failure mode this codebase produces most easily.
- **Deploy in batches, not per commit.** The realm is live; a rebuild restarts
  the worldserver and kicks the player out. Deploy at checkpoints only.
- **Registry rows go `MF_NotImplemented` → live in the same commit as the
  code**, and `OFFERABLE` in `tests/RegistryTest.cpp` with them.
- **Where a card's boon does not match its registry row, the row changes** and
  the reason goes in a comment. Two have already: Deafening Roar and, before
  it, several in Phase 2. A row that promises a number the mechanic never pays
  is the fault this redesign exists to remove.
- **Where a card states a flat number the boon table would ladder, add a
  `BoonTable` override.** Berserker's Bargain has one.

## Wave B — the remaining twenty-one

| # | Step | Ids | State |
|---|---|---|---|
| B1 | Warrior C3, Paladin C7, C8 | 30, 34, 35 | **done** |
| B2 | Hunter C12, Rogue C14, C16 | 39, 41, 43 | **done** |
| B3 | Priest C18, C19, DK C23, C24 | 45, 46, 50, 51 | **done** |
| B4 | Shaman C27, C28, Mage C30, C32 | 54, 55, 57, 59 | **done** |
| B5 | Warlock C34, C35, C36 | 61, 62, 63 | **done** |
| B6 | Druid C38, C39, C40 | 65, 66, 67 | **done** |
| B7 | Common C42 Unspent | 69 | **done** |
| B8 | Sweep, report addendum | — | **done** |

## Status: phase complete

Both waves are in, all sixty-nine registry rows have an implementation, and
nothing carries MF_NotImplemented. See docs/phase-4-report.md, including the
wave B addendum at section 7 and the three-way choice it leaves for the empty
tiers 78-80.

## Status: wave A complete and deployed

All six steps are done. `docs/phase-4-report.md` has the full write-up; the
short version is that the phase's biggest finding was not a class curse at all
but a Phase 0 exclusive key that limited a run to one class curse ever, which is
why extending thirty-three tier windows measured as no improvement whatsoever.

Wave B — the remaining twenty-three curses — is not this phase.

## Decisions taken while working

Recorded here as they happen, so the report can be written from facts rather
than from memory.

1. **Spell ids are 3.3.5a base ranks normalised through
   `GetFirstSpellInChain`.** 3.3.5 spells live in DBC, not SQL, so most cannot
   be confirmed from this machine. The failure mode is benign and visible — the
   curse stops reacting to that one spell — and the in-game checklist is what
   catches it. Ids that *are* confirmed carry a file:line.
2. **A "cheaper ability" boon is delivered as a refund, not a discount.** The
   cost is taken in `Spell::TakePower`, which runs before `OnPlayerSpellCast`,
   so by the time this module hears about a cast the mana is gone. The bar lands
   where a discount would have left it.
3. **A curse that watches one of its own spells remembers the id it saw.**
   `Unit::GetDynObject` takes a single spell id and Consecration has eight
   ranks; the id of the cast that made the circle is something the module was
   told, so it is used rather than a table of eight numbers guessed from memory.
4. **`IMechanic::OnPetDamage` was added, and it is outside the aggregate's
   clamp.** `Boon::BonusPetDamage` has existed since Phase 0 with nothing able
   to pay it, because `AggregateKind::DamageDone` is the *player's* damage.
   Outside the clamp deliberately: the caps bound what the world does to the
   player and what the player does to the world, and a hunter's pet is neither.
5. **`Boon::BonusAvoidance` is delivered as a full avoid, not as dodge.** There
   is no server-side way to add flat dodge without applying an aura, and an aura
   needs a spell id whose DBC tooltip would then describe something else. So the
   boon does what a dodge does -- the blow deals nothing -- and the wording says
   "avoid" rather than "dodge", because the combat log will read as a zero
   rather than as a dodge. Exposed Back is the first to use it.
6. **One new creature template, entry 900006 'Risen'.** Grave Call needs
   something to stand up, and display 570 is Slavering Ghoul's, in use by
   creature 1791 in the world today -- so it is in `CreatureDisplayInfo.dbc` on
   any client that can see Duskwood. No DBC edit, no client patch. It is in the
   base file and in a dated update, and **`ac-db-import` must be rebuilt before
   it is applied**, because that container runs the SQL baked into its image.
7. **Rune-starved's boon is not delivered and its blurb does not promise it.**
   The card spends it on runic power decaying more slowly, and that decay is
   inside the core's own rune tick with no hook on it. Saying nothing is better
   than promising a number nothing pays.
8. **An extended aura's tooltip still lies.** `SetDuration`/`SetMaxDuration`
   move the client's timer but not the DBC, so every curse that stretches an
   aura says the real number in its own `Describe()`.
