# Implementation plan — the affix redesign

Plan for implementing `docs/affix-design.md` in mod-gauntlet. No code in this
document; it names files, interfaces, tables, messages, tests and decisions.
Written against module commit `c334a9f` and the core at
`/mnt/c/Users/3302/azerothcore-wotlk` (`9fb906bb`). Every hook, API, creature
flag and spell id referenced here was checked in that tree; the spell ids were
matched by name against its `env/dist/data/dbc/Spell.dbc` (Appendix A).

---

## 0. Shape of the work

Six phases, each shippable on the live server behind config switches:

| Phase | Delivers | Depends on | Size |
|---|---|---|---|
| 0 | Generator rewrite, data model, addon protocol, debug commands | — | 3 sessions |
| 1 | Runtime framework (scheduler, summons, caps, state) + vertical slice: Champions, Falling Sky, The Shade, Deep Wounds | 0 | 4 sessions |
| 2 | Remaining spawn/enemy/tempo mechanics and the reworked scalars (14) | 1 | 5 sessions |
| 3 | Attrition, rules, bargains (6) | 1 | 2 sessions |
| 4 | Class family: 21 priority-A curses, then 23 priority-B | 1 (needs 2 for E1 reuse in C44) | 5 + 4 sessions |
| 5 | Pacing tools: event budget tuning, pair tests, leaderboard conducts, README | 2–4 | 2 sessions |

"Session" means one focused Claude Code session with a build-and-deploy at
the end. Roughly 25 sessions total; the vertical slice (phases 0–1, ~7
sessions) is the point at which the thesis is proven or not.

The organising decision: **store what was picked, regenerate only offers.**
Today an affix is regenerated from `(seed, tier, roll)` on every login, which
means any change to the generator silently rewrites every live run. The new
model regenerates *offers* from the seed (so a seed still reproduces a run's
choices at a given generator version) but writes the chosen mechanic, rank,
condition and boon to columns. Carried affixes never move again.

---

## 1. Phases

### Phase 0 — generator, data model, protocol

Files touched: `src/Gauntlet.h`, `src/GauntletAffix.cpp` (→ deleted),
`src/GauntletMgr.{h,cpp}`, `src/GauntletScripts.cpp`, `data/sql/**`,
`conf/mod_gauntlet.conf.dist`, `mod-gauntlet.cmake`, `addon/GauntletUI/**`.
New: `src/GauntletRegistry.{h,cpp}`, `src/GauntletGenerator.{h,cpp}`,
`src/GauntletAddon.{h,cpp}`, `src/GauntletCommands.cpp`, `tests/*.cpp`.

Order of work:

1. `GauntletRegistry`: the `MechanicDef` table (§2.1) with all 71 entries as
   data — id, key, family, class mask, tier window, max rank, exclusivity keys,
   flags, boon type, and the `Describe` template — but no behaviour yet. Every
   mechanic without an implementation is flagged `NotImplemented` and excluded
   from rolls, exactly as `IsImplemented` does today. Ids are stable integers
   and are never reused.
2. `GauntletGenerator`: the offer builder (§2.4) on the existing splitmix
   stream, with a `GeneratorVersion` constant folded into the stream seed. The
   old `Roll` survives as `LegacyRoll` for migration only.
3. Data model and SQL (§3): new `gauntlet_affix` columns, `gauntlet_state`,
   `gauntlet_affix_log`, leaderboard columns; the migration that converts
   existing `(tier, roll)` rows through `LegacyRoll` once.
4. `GauntletAddon`: the server→addon channel (§4) and the addon-side parser,
   with the chat fallback kept byte-for-byte so the old addon keeps working
   until replaced.
5. `GauntletCommands`: `.gauntlet` moves out of `GauntletScripts.cpp`; add the
   `debug` subtree (§5.2), GM-only.
6. `tests/`: generator determinism and offer-construction invariants (§5.1),
   registered through `ACORE_MODULE_TEST_SOURCES` in `mod-gauntlet.cmake`.

Done when: a fresh character is offered three affixes built by the new
generator from the current four scalar effects (now expressed as registry
entries `Exposed`, `Feeble`, and the temporary `Withering`, `Forgetful`), picks
persist in the new columns, an existing pre-migration character logs in with
the same affixes it had, the addon shows offers and carried affixes from the
protocol rather than chat parsing, and the unit tests pass 10,000 seeds × 16
tiers × 10 classes without an invariant failure.

### Phase 1 — runtime framework and the vertical slice

New: `src/GauntletMechanic.h` (interface, §2.2), `src/GauntletScheduler.{h,cpp}`,
`src/GauntletSummons.{h,cpp}`, `src/GauntletSummonAI.cpp`,
`src/mechanics/enemy/Champions.cpp`, `src/mechanics/tempo/FallingSky.cpp`,
`src/mechanics/spawn/Shade.cpp`, `src/mechanics/attrition/DeepWounds.cpp`,
`data/sql/db-world/base/gauntlet_creatures.sql`.

Order of work:

1. `IMechanic` and the dispatch adapters in `GauntletScripts.cpp`: every core
   hook the design uses becomes one adapter that iterates the player's active
   instances (≤ 16, so no indexing needed). `Mgr::Multiplier` becomes
   `Mgr::Aggregate` (§2.5) with clamps.
2. `Scheduler` (§2.3): per-player queue, 500 ms accumulator, minimum spacing,
   event budget, suppression flags, and the warning→fire two-step every timed
   event uses.
3. `Summons`: owner-bound `SummonCreature` wrapper that records the GUID in the
   run state, the shared `GauntletSummonAI`, level-to-owner, despawn on
   logout/death/zone change, reduced XP via `OnPlayerGiveXP` (victim is a
   `TempSummon` whose summoner is the player → ×0.5). Template SQL for the
   Shade, Scavenger, Ambusher, Restless Spirit and Doppelgänger entries with
   `flags_extra` `NO_TAUNT` (0x100) where the design says taunt-immune.
4. The four mechanics, in this order: Champions (counters, addon counter
   message, reward), Falling Sky (scheduler warning/fire, World Trigger visual,
   damage), Deep Wounds (`OnPlayerAfterUpdateMaxHealth` observer, decay,
   persistence), The Shade (summons, AI, nemesis rank persistence, leash).
5. Per-player state persistence (§3.3) for the counters those four need.

Done when: a level-20 test character with all four at rank I plays for an hour
with every event telegraphed, attributed in chat and on the addon, no summon
left behind after logout, and `.gauntlet debug dump` shows the scheduler
spacing holding when all four are armed.

### Phase 2 — the rest of the world-side mechanics

`src/mechanics/spawn/{Echo,Carrion,Reinforcements,Ambush}.cpp`,
`enemy/{Craven,CallToArms,DeathRattle,Grudge,Nimble,Cunning,KeenNosed}.cpp`,
`tempo/{Frenzy,Overextended,Falter,Hubris}.cpp`, `attrition/{Exposed,Feeble}.cpp`
(the reworked scalars, condition-gated, `VersusElites` at the damage site).

Order: by shared infrastructure, not by family — Echo and Reinforcements
(summons of copied entries, clone spells) first; then Call to Arms, Keen-nosed,
Death Rattle, Grudge (grid search and corpse positions); then Craven, Nimble,
Cunning (creature-side state keyed by GUID with evade/exit-combat cleanup);
then Frenzy, Overextended, Hubris, Falter, the scalars (pure hook math).

Done when: every mechanic passes its in-game checklist (§5.3) and the
`Withering`/`Forgetful` registry entries are retired from rolls.

### Phase 3 — attrition, rules, bargains

`attrition/BloodMagic.cpp`, `rules/{SelfFound,LoneWolf,IronPurse}.cpp`,
`bargain/{LastRites,CursedHoard}.cpp`. Last Rites is the `UnitScript::
DealDamage` cheat-death path and must land before Ankh Pact / Stone of the
Damned reuse it. Cursed Hoard needs `OnPlayerBeforeSendLoot` with a game-object
guid and `GlobalScript::OnAfterCalculateLootGroupAmount`.

### Phase 4 — the class family

`src/mechanics/class/{Warrior,Paladin,Hunter,Rogue,Priest,DeathKnight,Shaman,
Mage,Warlock,Druid}.cpp` (four each), `class/Common.cpp` (Faint, Unspent),
`class/Bargains.cpp` (Ankh Pact, Stone of the Damned).

Wave A first (design's build priority A): C1 Red Mist, C2 Berserker's Bargain,
C4 Deafening Roar, C5 Long Forbearance, C6 Consecrated Ground, C9 Half-Tamed,
C10 Dead Weight, C11 Wide Dead Zone, C13 Cold Trail, C15 Exposed Back, C17 Frail
Soul, C20 Whispers of the Deep, C21 Rune-starved, C22 Grave Call, C25 One Totem,
C26 Totemic Anchor, C29 Cold Feet, C31 Mana Burn, C33 Fel Pact, C37 Bound Skin,
C41 Faint. Then wave B. Three shared helpers come first: `PermanentCooldown`
(the "cannot use X" primitive: `AddSpellCooldown` with a 7-day end time, sent
to the client, re-asserted every tick — verified to grey the button without a
patch), `SelfControl` (confuse/root/stun/flee wrappers over `SetControlled`
with a timer), and `AuraDurationEdit` (`OnAuraApply` + `SetDuration`/
`SetMaxDuration` keyed on spell id and the player).

Done when: each curse's checklist passes on its class at the tier it unlocks,
and the leaderboard line shows conducts.

### Phase 5 — pacing tools

Event-budget and cap tuning through config; the §4.7 pair tests run
deliberately (Call to Arms + Craven, Champions + Frenzy, Shade + Deep Wounds);
`.gauntlet top` prints conducts; README rewritten for the family model;
determinism note ("offers reproduce, events do not").

---

## 2. Module layout

### 2.1 Registry

```
src/GauntletRegistry.h

enum class Family : uint8 { Spawn, Enemy, Tempo, Attrition, Rules, Bargain, Class };

enum MechanicFlags : uint32 {
    Timed        = 1 << 0,   // uses the scheduler clock; counts toward the event budget
    OnKill       = 1 << 1,   // family cap "on-kill"
    Stalker      = 1 << 2,   // family cap "stalker": one per run
    RoleTax      = 1 << 3,   // cap: one per run (Cunning, Falter)
    Scalar       = 1 << 4,   // takes a Condition from the condition axis
    RewardShaped = 1 << 5,   // satisfies "one reward-shaped offer per tier"
    NotImplemented = 1 << 31,
};

struct MechanicDef {
    uint16      id;             // stable, never reused; 1..71 in the design's order
    char const* key;            // "shade", "champions", "c01_red_mist"
    char const* name;
    Family      family;
    uint32      classMask;      // 0 = all; CLASSMASK_* otherwise; Faint = mana users
    uint8       minTier, maxTier;
    uint8       maxRank;        // 3 for ladders; 1 for rules
    uint32      flags;
    char const* exclusiveKeys;  // "onkill-positional|shortcut:divine-shield" — no two active
                                // mechanics may share a key ("never pay twice")
    Boon        boon;           // fixed boon type per mechanic; magnitude by rank
    uint32      requiresSpell;  // relevance: e.g. Consecration rank 1 (26573) or 0
    IMechanic*  (*factory)();
};
```

Each mechanic file ends with `GAUNTLET_REGISTER(def)` which appends to a
static vector at load time. The generator, the addon (icons, names), the
commands and the tests all read the same table, so an affix cannot exist that
the addon cannot name.

### 2.2 The mechanic interface

```
src/GauntletMechanic.h

struct AffixInstance {           // one carried affix
    uint16    mechanic;  uint8 rank;  Condition condition;  Boon boon;  uint8 boonMag;
    uint8     slot;              // tier it was taken at
    IMechanic* impl;             // owned by RunState, created from the registry
};

struct Ctx {                     // passed to every callback
    Player*        player;
    RunState&      run;
    AffixInstance& self;
    Scheduler&     clock;
    Addon&         addon;        // Send(type, payload); Counter(key, value, max)
    State&         state;        // persisted key/value (§3.3): Get/Set/Add
};

class IMechanic {
public:
    virtual ~IMechanic() = default;
    virtual void  OnAttach(Ctx&) {}                    // pick or login
    virtual void  OnDetach(Ctx&) {}                    // swap, logout, death
    virtual void  OnTick(Ctx&, uint32 diffMs) {}       // 500 ms, only if Timed or it asks
    virtual void  OnEvent(Ctx&, uint32 eventId) {}     // scheduler callback
    virtual void  OnKill(Ctx&, Creature*) {}
    virtual void  OnPetKill(Ctx&, Creature*) {}
    virtual void  OnEnterCombat(Ctx&, Unit* enemy, bool wasOutOfCombat) {}
    virtual void  OnLeaveCombat(Ctx&) {}
    virtual float DamageTakenMult(Ctx&, Unit* attacker, SpellInfo const*) { return 1.f; }
    virtual float DamageDoneMult (Ctx&, Unit* victim,   SpellInfo const*) { return 1.f; }
    virtual float HealTakenMult  (Ctx&, Unit* healer,   SpellInfo const*) { return 1.f; }
    virtual void  OnDamageTaken(Ctx&, Unit* attacker, uint32 amount) {}      // observer, post-mult
    virtual uint32 OnLethal(Ctx&, uint32 damage) { return damage; }          // UnitScript::DealDamage
    virtual void  OnSpellCast(Ctx&, Spell*) {}
    virtual void  OnAuraApplied(Ctx&, Unit* target, Aura*) {}
    virtual void  OnAuraRemoved(Ctx&, Unit* target, AuraApplication*) {}
    virtual void  OnShapeshift(Ctx&, uint8 form) {}
    virtual void  OnLoot(Ctx&, ObjectGuid lootGuid, Loot*) {}
    virtual void  OnMaxHealth(Ctx&, float& value) {}
    virtual void  OnXP(Ctx&, uint32& amount, Unit* victim) {}
    virtual void  OnTalentPoints(Ctx&, uint32& points) {}
    virtual bool  IsRelevant(Player*) const { return true; }   // beyond the classMask
    virtual std::string Describe(AffixInstance const&) const = 0;
};
```

Dispatch: `GauntletScripts.cpp` keeps one override per core hook and does
nothing but `for (auto& a : run.affixes) a.impl->X(ctx, …)`. Conditions are
evaluated by the framework: for `Scalar` mechanics the multiplier callbacks
are skipped when `ConditionActive` is false; for event mechanics the condition
gates *arming* (a `Nocturnal` Shade only runs its clock at night). `VersusElites`
becomes a real condition because the attacker is now in the callback.

Creature-side state (Craven's flee-once, Champions' promotion, Nimble's speed,
Cunning's kick cooldown) lives in a `std::unordered_map<ObjectGuid, CreatureNote>`
on the RunState, pruned on `OnUnitExitCombat`, `OnUnitEnterEvadeMode`,
`OnUnitDeath`, `OnCreatureRemoveWorld`.

### 2.3 Scheduler

```
src/GauntletScheduler.h

struct Event { uint32 dueMs; uint16 mechanic; uint32 id; uint8 kind; /* Warn, Fire */ };

class Scheduler {
    std::vector<Event> queue;             // small; sorted on insert
    uint32 accumulator;                   // 500 ms tick
    uint32 lastFireMs;                    // for minimum spacing (config, default 12 s)
    uint32 suppressedUntil;               // login / zone-in grace (60 s)
public:
    void Arm(uint16 mechanic, uint32 id, uint32 inMs, uint32 warnMs);   // warn then fire
    void Cancel(uint16 mechanic);
    void Tick(Player*, RunState&, uint32 diff);   // honours suppression and spacing
    float Budget(RunState const&) const;          // 1 + 0.25 * (timedAffixes - 1)
};
```

Rules implemented here, not in mechanics: no events while mounted, in flight,
in a sanctuary, dead, during the grace window or with an offer pending;
minimum spacing between `Fire` events (a due event waits, it is not dropped);
intervals a mechanic asks for are multiplied by `Budget()`; at most one
stalker/ambush summon alive and four affix summons total per player (the
summon wrapper refuses and the mechanic re-arms).

### 2.4 Offer builder (deterministic)

```
stream = Mix((seed << 32) ^ (tier << 8) ^ GeneratorVersion)
slotKinds = { A: (carried.size() >= 3 || tier >= 9) ? RankUp : New,
              B: New,
              C: tier ∈ {4, 8, 12} ? Swap : (Roll(stream, 0..5) == 0 ? Bargain : New) }
for each slot:
    families = allowed at tier ∩ not saturated (caps) ∩ not used by another slot
    family   = weighted roll from stream
    pool     = registry[family] where minTier ≤ tier ≤ maxTier
             ∧ classMask matches ∧ impl->IsRelevant(player)
             ∧ !NotImplemented
             ∧ (RankUp ? carried has it with rank < maxRank : carried lacks it)
             ∧ no exclusiveKey collision with carried (except the one being ranked up)
    if pool empty: try next family; if all empty: fall back to a Scalar
    mechanic = roll from pool; rank = RankUp ? carried.rank + 1 : RankFloor(tier)
    condition = Scalar ? roll from state conditions (never Always/InCombat) : Always
    boon magnitude = BoonTable[mechanic][rank] × (relevance discount)
if no slot is RewardShaped: replace slot B's pick with a RewardShaped roll from the same stream
```

Every roll consumes the same stream in the same order, so a seed reproduces
the offers *given the same carried set*, which is itself a function of earlier
picks. `GeneratorVersion` is bumped whenever the registry table, weights or
this algorithm change; old runs keep their columns and are unaffected.

Swap offers carry `swapSlot` (which carried affix they replace) chosen by the
stream from the carried set; on pick, the replaced row moves to
`gauntlet_affix_log`.

### 2.5 Aggregation and caps

`Mgr::Aggregate(player, kind, ctx…)` multiplies every active instance's
factor, then clamps: damage taken ∈ [1.0, 2.0] × base, damage done ≥ 0.6,
healing received ≥ 0.5, max health ≥ 0.6 × base after wounds, creature run
speed ≤ 1.4. The clamp is applied to the *product*, so Champions' +25%,
Frenzy's stacks and Overextended's attackers cannot combine past the ceiling.
`.gauntlet status` prints the current products.

### 2.6 File tree

```
src/
  Gauntlet.h                 enums (Family, Condition, Boon), AffixInstance, RunState
  GauntletRegistry.{h,cpp}   MechanicDef table, lookup by id/key/family
  GauntletGenerator.{h,cpp}  offer builder, LegacyRoll, GeneratorVersion
  GauntletMechanic.h         IMechanic, Ctx
  GauntletMgr.{h,cpp}        run state, load/save, offer/pick/swap, EndRun, Aggregate
  GauntletScheduler.{h,cpp}
  GauntletSummons.{h,cpp}    owner-bound summon wrapper, despawn rules
  GauntletSummonAI.cpp       shared CreatureScript AI for gauntlet_* entries
  GauntletAddon.{h,cpp}      LANG_ADDON channel, coalescing, rate limit
  GauntletCommands.cpp       .gauntlet pick/status/top/debug
  GauntletScripts.cpp        hook adapters only (~150 lines)
  mechanics/
    spawn/ enemy/ tempo/ attrition/ rules/ bargain/ class/
tests/
  GeneratorTest.cpp  OfferInvariantsTest.cpp  AggregateTest.cpp  SchedulerTest.cpp
data/sql/
  db-characters/base/gauntlet.sql            (new install: final schema)
  db-characters/updates/2026_xx_xx_00.sql    (migration for existing installs)
  db-world/base/gauntlet_creatures.sql       (creature_template rows, ScriptName)
addon/GauntletUI/
  GauntletUI.toc  Protocol.lua  Panel.lua  Hud.lua  Data.lua (icons per family/mechanic)
```

---

## 3. Data model and migrations

### 3.1 `gauntlet_affix` (rewritten)

```
guid        INT UNSIGNED NOT NULL
slot        TINYINT UNSIGNED NOT NULL          -- tier at which it was taken
mechanic    SMALLINT UNSIGNED NOT NULL         -- registry id
rank        TINYINT UNSIGNED NOT NULL DEFAULT 1
cond        TINYINT UNSIGNED NOT NULL DEFAULT 0
boon        TINYINT UNSIGNED NOT NULL DEFAULT 0
boon_mag    TINYINT UNSIGNED NOT NULL DEFAULT 0
gen_version SMALLINT UNSIGNED NOT NULL
picked_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
PRIMARY KEY (guid, slot)
```

A rank-up updates `rank` in place (same slot); a swap deletes the replaced
row after copying it to the log and inserts the new one at the current tier.

### 3.2 `gauntlet_affix_log`

Append-only: `guid, tier, action ENUM('pick','rankup','swap_out','swap_in',
'bargain'), mechanic, rank, gen_version, at`. Source for the leaderboard's
affix list and for reproducing a run's story.

### 3.3 `gauntlet_state`

Per-player mechanic state that must survive logout:

```
guid INT UNSIGNED, k VARCHAR(32), v INT NOT NULL, PRIMARY KEY (guid, k)
```

Keys are `<mechanic key>.<field>`: `champions.count`, `echo.kills`,
`carrion.loots`, `shade.rank`, `shade.deadUntilTier`, `deepwounds.wound`,
`felpact.kills`, `lastrites.chargeLevel`, `unspent.bank`, `ankh.used`.
Written on logout, on death, on pick, and every 60 s while dirty. Transient
state (Frenzy stacks, Falling Sky clock, Falter clock, Grudge spirits) is not
persisted; it resets on login by design.

### 3.4 `gauntlet_run` and `gauntlet_leaderboard`

`gauntlet_run` gains `gen_version` (the version the run was created under —
informational) and `class TINYINT`. `gauntlet_leaderboard` gains `conducts
VARCHAR(255)` ("Cold Trail III, Faint I") and `affixes TEXT` (all carried
names with ranks) written by `EndRun`; `.gauntlet top` prints conducts.

### 3.5 World DB

`creature_template` rows (entries in a reserved range, e.g. 900000+; existing
display ids; `ScriptName = 'gauntlet_summon'`; `flags_extra` `NO_TAUNT`
where taunt-immune): Shade, Scavenger, Ambusher, Restless Spirit (non-
attackable, `unit_flags` NOT_SELECTABLE|NON_ATTACKABLE), Doppelgänger (cloned
at runtime with 45204 + 41055), Risen Ghoul (or reuse the DK ghoul entry).
Reinforcements, Half-Tamed, Fel Pact and Stone of the Damned summon *existing*
entries (the victim's, the pet's, the killer's) and therefore cannot carry a
template flag; their ownership comes from the summon wrapper, and their XP
reduction from `OnPlayerGiveXP`.

### 3.6 Migration

Update script: add columns with defaults; for each `(guid, tier, roll)` row
compute `LegacyRoll(seed, tier, roll)` in a one-shot startup routine on the
worldserver (SQL cannot run splitmix), map the legacy effect+condition+boon to
the registry (`Exposed`, `Feeble`, `Withering`, `Forgetful` entries kept for
this purpose), write the columns with `gen_version = 1`, then drop `roll`.
`Withering` and `Forgetful` remain valid carried affixes for old runs but are
`NotImplemented` for new rolls from Phase 2 on.

---

## 4. Addon protocol

Transport: a `LANG_ADDON` whisper from the player to themself, built with the
same `ChatHandler::BuildChatPacket` overload the core's own
`AddonChannelCommandHandler::Send` uses (`Chat.cpp:1104–1109`), message
`GNT\t<type>\t<fields…>` (tab-separated; total ≤ 255 bytes, so long text is
split). Client→server: the addon sends `SendAddonMessage("GNT", …, "WHISPER",
playerName)`; the server intercepts it in `OnPlayerCanUseChat(player, type,
LANG_ADDON, msg, receiver)` and returns false so it never echoes. Protocol
version in `HELLO`; the addon disables itself on a mismatch and falls back to
chat. Chat lines stay as they are, so the module is playable without the
addon.

| Type | Direction | Fields | When |
|---|---|---|---|
| `HELLO` | S→C | `ver` | login |
| `RUN` | S→C | `seed tier state class` | login, pick |
| `AFFIX` | S→C | `slot id rank cond boon boonMag` | login snapshot (one per carried), pick, swap |
| `AFFIX_END` | S→C | — | end of snapshot |
| `OFFER` | S→C | `i id rank cond boon boonMag kind(new/rankup/swap/bargain) swapSlot` | tier reached, login with pending |
| `OFFER_END` | S→C | — | |
| `EVT` | S→C | `key secs label` | scheduler warning (countdown), `secs=0` = fired |
| `CTR` | S→C | `key value max` | counter changed (Champions, Echo, Carrion, Fel Pact, Grave Call) |
| `STAT` | S→C | `key value` | wound %, Frenzy stacks, Unspent bank, Deep Wounds |
| `COND` | S→C | `slot 0/1` | a Scalar affix's condition became active/inactive |
| `SUMMON` | S→C | `key 0/1` | a stalker/ambusher is alive for you |
| `KILLBY` | S→C | `id name` | on death: which affix's event or multiplier was last to act |
| `TOP` | S→C | `rank name tier level cause conducts` | `.gauntlet top` |
| `PICK` | C→S | `i` | addon button; server validates as `.gauntlet pick` |
| `SYNC` | C→S | — | addon reload; server resends snapshot |

Names, descriptions and icons are **not** sent: the addon ships `Data.lua`
generated from the registry (a `.gauntlet debug export-addon` command writes
it) keyed by mechanic id, so payloads stay small and the two tables cannot
drift without a version bump.

Coalescing: `STAT`/`COND`/`CTR` are queued and flushed once per 500 ms tick
with only the latest value per key; hard cap 8 messages per second per
player. `EVT` and `KILLBY` bypass the queue.

Addon changes: `Protocol.lua` (parser, version gate), `Panel.lua` (offers with
kind badges, carried list with rank pips and condition lights), `Hud.lua`
(countdown bar for `EVT`, counters, wound/stacks readout, summon-alive
indicator), `Data.lua` (icons per family and per mechanic, Blizzard icon
paths only), leaderboard tab with conducts. The current `IconFor(desc)`
string-matching goes away.

---

## 5. Testing and tuning

### 5.1 Unit tests (gtest, built with `BUILD_TESTING=ON`)

Registered from `mod-gauntlet.cmake`:
`set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES <tests/*.cpp>)`
and `ACORE_MODULE_TEST_INCLUDES` for `src/`. The core links the `modules`
library into `unit_tests`, so generator, registry, aggregate and scheduler code
must not touch `Player*` directly in those paths — they take an `IPlayerView`
(class, level, `HasSpell`, spec) that tests stub. No module in this tree uses
the mechanism yet; the first build will need a check that static-linked
module symbols resolve.

- Determinism: same `(seed, tier, carried, class, version)` → identical offers;
  a version bump changes them; the legacy roll reproduces today's affixes for
  a fixed seed table captured before the rewrite.
- Offer invariants over 10,000 seeds × 16 tiers × 10 classes × a random
  carried set: exactly three offers; distinct families; no duplicate
  mechanic; rank-ups only for carried mechanics below max rank; no
  exclusive-key collision; no `NotImplemented`; no class-irrelevant entry; tier
  windows honoured; swap at 4/8/12; at least one `RewardShaped`; never
  `Always`/`InCombat` on a Scalar.
- Aggregate: products clamp at the caps from every combination of the
  contributing mechanics.
- Scheduler with a fake clock: spacing, budget stretching, suppression, warn→
  fire ordering, cancel.

### 5.2 `.gauntlet debug` (SEC_GAMEMASTER)

`give <key|id> [rank] [cond]`, `remove <slot>`, `rank <slot> <n>`,
`fire <key>` (skip the clock, keep the warning), `set <state-key> <value>`,
`dump` (affixes, aggregate products, scheduler queue, summons, state),
`events on|off`, `offers <tier>` (print what the generator would offer),
`seed <n>`, `export-addon` (writes `Data.lua`). Also `Gauntlet.Debug.Enable`
config so it can be compiled in but switched off on the public realm.

### 5.3 In-game checklist (one per mechanic, kept in `docs/checklists.md`)

Trigger fires when the design says; telegraph visible (emote, chat line,
`EVT`); counterplay actually works (each named button or movement tested);
attribution line on death names the affix; summons despawn on logout, death,
zone change and leash; nothing fires while mounted/in flight/in sanctuary/in
the grace window; rank II and III differ as specified; the boon applies; the
addon shows the state; bots are untouched; a grouped non-Gauntlet player is
unaffected (except by design, e.g. Champions' XP).

### 5.4 Pair tests and tuning knobs

Run the three named pairs (Call to Arms + Craven, Champions + Frenzy, Shade +
Deep Wounds) and the role-tax exclusivity (Cunning vs Falter) on a level-40
character. Config: `Gauntlet.Family.<Spawn|Enemy|Tempo|Attrition|Rules|Bargain|
Class>.Enable`, `Gauntlet.Events.Enable`, `Gauntlet.Events.MinSpacing`,
`Gauntlet.Events.BudgetStep`, `Gauntlet.Caps.DamageTaken`, `.DamageDone`,
`.HealTaken`, `.MaxHealth`, `.EnemySpeed`, `Gauntlet.Summons.MaxAlive`,
`Gauntlet.Summons.XpRate`, `Gauntlet.Grace.Seconds`.

---

## 6. Risks and open questions

Decisions I need from you before coding:

1. **Relevance source for class curses.** Recommend trained spells
   (`HasSpell` on the mechanic's `requiresSpell`) plus
   `GetMostPointsTalentTree()` for spec-gated ones (Faithless Form, Rune-
   starved presence assumptions). Talents alone miss low-level characters.
2. **Swap semantics.** Recommend: the swap target is chosen by the stream (so
   offers stay deterministic) and shown in the offer; the player cannot pick
   which affix to discard. Alternative: player chooses the slot, offers stop
   being fully seed-determined.
3. **Summon XP.** Copies of existing entries cannot carry `NO_XP`; recommend
   `OnPlayerGiveXP` ×0.5 for every gauntlet summon (config), and full XP only
   for Champions (a real mob, promoted).
4. **Shade in group dungeons.** Recommend it spawns there too, never during a
   boss encounter (`InstanceScript::IsEncounterInProgress`), attacks only its
   owner, and can be damaged by anyone.
5. **Ankh Pact / Stone of the Damned ordering.** `EndRun` currently fires in
   `OnPlayerJustDied`. To allow a self-resurrection the run must be ended
   later: recommend `OnPlayerJustDied` marks `pendingDeath` and starts a 60 s
   timer; `OnPlayerResurrect` with the charge cancels it; `OnPlayerReleasedGhost`
   or the timer ends the run. Blizzard's rule (release = death) matches.
6. **Legacy runs.** Migrate in place (recommended) or freeze pre-rewrite
   characters on the old code path? In place is cleaner; it needs the
   startup routine in §3.6.
7. **Leaderboard conducts.** Class curses only (recommended) or every affix?

Technical risks to verify in Phase 1, with fallbacks:

- `OnBeforeCreatureSelectLevel` may run before a `TempSummon` knows its
  summoner. Fallback: `SetLevel` + `UpdateAllStats` right after `SummonCreature`
  returns, then `SetFullHealth`.
- The 7-day `AddSpellCooldown` for "cannot use X": confirm the client honours
  the value and that Cold Snap/Preparation resets are re-asserted within a
  tick (they are — the tick re-adds it).
- Addon whisper: confirm the client raises `CHAT_MSG_ADDON` for a self-whisper
  with a tab-separated payload and that 255 bytes is the limit.
- Consecrated Ground's `GetDynObject` must be checked per Consecration rank id
  (26573, 20116, 20922, 20923, 20924, 27173, 48818, 48819).
- Wide Dead Zone: Auto Shot goes through `OnPlayerSpellCast`; confirm
  interrupting it there does not desync the client's auto-repeat state.
- Slow Hands needs a server-side `spell_dbc` aura (`SPELL_AURA_MOD_POWER_REGEN_
  PERCENT`) because energy regeneration has no hook; invisible to the client,
  so the addon shows it.
- Grid searches (Keen-nosed, Call to Arms, Deafening Roar) at 500 ms for a
  handful of real players are cheap; if the realm grows, move them to a 1 s
  cadence per config.

Unimplementable with the verified hooks — mark for cut, not redesign:
**Iron Purse's training-cost variant** (no trainer price hook; repairs only),
**Death Grip redirect** (already cut in the design). Everything else in the
71 has a path above.

---

## Appendix A — verified ids

Matched by name against `env/dist/data/dbc/Spell.dbc` (187 of 189 checked
exact; the two "misses" are name variants: 16979 is "Feral Charge - Bear",
45204 is "Clone Me!").

| Use | Spell ids |
|---|---|
| Creature visuals / utilities | 8599 Enrage, 8269 Frenzy, 45204 Clone Me!, 41055 Copy Weapon, 676 Disarm, 15487 Silence |
| Warrior | 2457/71/2458 stances, 6673/469/1160/5246 shouts, 871 Shield Wall, 12975 Last Stand, 55694 Enraged Regeneration, 2687 Bloodrage, 5308 Execute, 34428 Victory Rush, 78 Heroic Strike, 845 Cleave, 100 Charge, 20252 Intercept, 1715 Hamstring, 18499 Berserker Rage, 20230 Retaliation |
| Paladin | 642 Divine Shield, 633 Lay on Hands, 1022 Hand of Protection, 25771 Forbearance, 853 Hammer of Justice, 26573 Consecration (r1), 8690 Hearthstone, 1044 Hand of Freedom, 498 Divine Protection, 635 Holy Light, 19750 Flash of Light |
| Hunter | 5384 Feign Death, 781 Disengage, 136 Mend Pet, 6991 Feed Pet, 883 Call Pet, 2641 Dismiss Pet, 982 Revive Pet, 5116 Concussive Shot, 2974 Wing Clip, 2973 Raptor Strike, 5118 Aspect of the Cheetah, 13163 Monkey, 34074 Viper, 19263 Deterrence, 13809 Frost Trap, 1499 Freezing Trap, 19503 Scatter Shot, 75 Auto Shot, 34477 Misdirection |
| Rogue | 1856 Vanish, 2983 Sprint, 5277 Evasion, 14185 Preparation, 1766 Kick, 1776 Gouge, 2094 Blind, 1784 Stealth, 408 Kidney Shot, 1833 Cheap Shot, 31224 Cloak of Shadows, 6770 Sap |
| Priest | 17 Power Word: Shield, 6788 Weakened Soul, 6346 Fear Ward, 8122 Psychic Scream, 15473 Shadowform, 47585 Dispersion, 19236 Desperate Prayer, 586 Fade, 139 Renew, 2061 Flash Heal, 1706 Levitate, 9484 Shackle Undead |
| Death Knight | 48707 Anti-Magic Shell, 48792 Icebound Fortitude, 49576 Death Grip, 46584 Raise Dead, 49158 Corpse Explosion, 48743 Death Pact, 42650 Army of the Dead, 49039 Lichborne, 48266/48263/48265 presences, 47528 Mind Freeze, 45529 Blood Tap, 47568 Empower Rune Weapon, 49998 Death Strike, 56815 Rune Strike, 47541 Death Coil, 43265 Death and Decay, 45524 Chains of Ice, 47476 Strangulate, 3714 Path of Frost, 61999 Raise Ally, 49206 Summon Gargoyle |
| Shaman | 2645 Ghost Wolf, 20608 Reincarnation, 974 Earth Shield, 324 Lightning Shield, 5730 Stoneclaw, 2484 Earthbind, 5394 Healing Stream, 3599 Searing, 8177 Grounding, 8143 Tremor, 57994 Wind Shear, 51514 Hex, 17364 Stormstrike, 51505 Lava Burst, 403 Lightning Bolt, 8042/8056/8050 shocks, 2825 Bloodlust, 32182 Heroism, 556 Astral Recall, 2008 Ancestral Spirit |
| Mage | 1953 Blink, 45438 Ice Block, 122 Frost Nova, 118 Polymorph (12826 r4), 1463 Mana Shield, 11426 Ice Barrier, 12051 Evocation, 66 Invisibility, 2139 Counterspell, 55342 Mirror Image, 12043 Presence of Mind, 11958 Cold Snap, 133 Fireball, 116 Frostbolt |
| Warlock | 755 Health Funnel, 697 Voidwalker, 688 Imp, 712 Succubus, 691 Felhunter, 30146 Felguard, 6201 Create Healthstone, 693 Create Soulstone, 20707 Soulstone Resurrection, 1120 Drain Soul, 689 Drain Life, 172 Corruption, 980 Curse of Agony, 348 Immolate, 30108 Unstable Affliction, 7812 Sacrifice, 29858 Soulshatter, 48020/48018 Demonic Circle, 1454 Life Tap, 5782 Fear, 5484 Howl of Terror, 6789 Death Coil, 6229 Shadow Ward, 47241 Metamorphosis |
| Druid | 5487 Bear, 9634 Dire Bear, 768 Cat, 783 Travel, 1066 Aquatic, 24858 Moonkin, 33891 Tree of Life, 339 Entangling Roots, 16689 Nature's Grasp, 1850 Dash, 22812 Barkskin, 5211 Bash, 16979 Feral Charge - Bear, 2637 Hibernate, 29166 Innervate, 61336 Survival Instincts, 22842 Frenzied Regeneration, 5215 Prowl, 20484 Rebirth, 33786 Cyclone, 5229 Enrage |
| Misc | 15007 Resurrection Sickness (do not use), 5019 Shoot |

Creatures: World Trigger 21252 (used by core scripts as an invisible caster),
entry 12999 present in `creature_template`. Flags: `CREATURE_FLAG_EXTRA_NO_XP`
0x40, `CREATURE_FLAG_EXTRA_NO_TAUNT` 0x100. Item: Soul Shard 6265.

## Appendix B — hook → mechanic matrix

| Hook | Mechanics |
|---|---|
| `OnPlayerUpdate` (500 ms) | scheduler tick; Shade, Ambush, Reinforcements, Falling Sky, Falter clocks; Grudge proximity; Keen-nosed; Cunning; Deep Wounds decay; Red Mist, Faint, Rune-starved state; Half-Tamed; permanent cooldowns |
| `OnPlayerCreatureKill` | Echo, Carrion (loot), Call to Arms, Death Rattle, Grudge, Frenzy, Grave Call, Nature's Toll, Cursed Hoard countdown, Champions reward |
| `OnPlayerCreatureKilledByPet` | Fel Pact, Half-Tamed happiness |
| `OnPlayerEnterCombat` / `OnPlayerLeaveCombat` | Champions (out-of-combat engage), Nimble, Reinforcements clock, Whispers once-per-fight reset |
| `ModifyMeleeDamage` / `ModifySpellDamageTaken` / `ModifyPeriodicDamageAurasTick` | Exposed, Feeble, Overextended, Frenzy, Champions, Nimble (none), Rune-starved, Consecrated Ground, Totemic Anchor, Exposed Back, Shared Blood, Mana Burn, Blood Bond, Spirit Debt, Poisoned Blades, Affliction of the Self, Deep Wounds observer, Last Rites Mark, Cursed Hoard |
| `UnitScript::DealDamage` | Last Rites, Ankh Pact, Stone of the Damned (lethal detection), Whispers threshold |
| `UnitScript::OnDamage` | Craven (threshold crossing on creatures) |
| `ModifyHealReceived` | Withering (legacy), Grudge, Last Rites Mark, Blood Bond, Deep Wounds |
| `OnPlayerAfterUpdateMaxHealth` | Deep Wounds, Arcane Frailty, Bound Skin boon |
| `OnPlayerGiveXP` | Hubris, Champions, Echo, summons ×0.5, Lone Wolf boon, Vindication |
| `OnPlayerSpellCast` | Blood Magic, Deafening Roar, Iron Discipline, Cold Presence, One Ward, Long Forbearance (mana), No Sanctuary, Commitment, Cold Feet, Fickle Sheep (none), One Totem, Elemental Overload, Wide Dead Zone, Penance of Silence, Grave Call claim, Fel Pact resummon, Shard Economy, Commitment of Roots |
| `UnitScript::OnAuraApply` / `OnAuraRemove` | Long Forbearance, Frail Soul, Fickle Sheep, Faithless Form, Commitment of Roots |
| `OnUnitSetShapeshiftForm` | Bound Skin, Nature's Toll |
| `OnPlayerBeforeSendLoot`, `OnPlayerBeforeLootMoney`, `GlobalScript::OnItemRoll`, `OnAfterCalculateLootGroupAmount` | Carrion, Cursed Hoard, Self-Found boon |
| `OnPlayerCanInitTrade` / `CanSendMail` / `CanPlaceAuctionBid`, `GroupScript::OnAddMember` | Self-Found, Lone Wolf |
| `OnPlayerBeforeDurabilityRepair` | Iron Purse |
| `OnPlayerCalculateTalentsPoints` / `OnPlayerLearnTalents` | Unspent |
| `OnPlayerCanCastItemUseSpell` | Shard Economy (Healthstone) |
| `OnPlayerJustDied` / `OnPlayerResurrect` / `OnPlayerReleasedGhost` | EndRun, Ankh Pact, Stone of the Damned |
| `OnPlayerLogin` / `Logout` / `UpdateZone` / `LevelChanged` | snapshot, despawn, grace window, Deep Wounds clear, tier offers |
| `AllCreatureScript::OnBeforeCreatureSelectLevel`, `CreatureScript` AI | all summons |
| `OnPlayerCanUseChat` (LANG_ADDON) | addon `PICK`/`SYNC` |
