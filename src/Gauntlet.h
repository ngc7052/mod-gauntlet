/*
 * mod-gauntlet - procedurally generated hardcore affix challenge for AzerothCore
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_H
#define MOD_GAUNTLET_H

#include "Define.h"
#include "GauntletState.h"
#include <string>
#include <vector>

// This header is deliberately free of Player.h and every other core game
// header: the registry, the generator and the aggregate maths are compiled
// into unit_tests as well as into worldserver, and the test build has no
// game objects. Anything that needs a Player belongs in GauntletMgr.h or in
// a mechanic implementation.
//
// GauntletState.h is the one module header this one includes, and it is
// included for the same reason it is safe: it is free of Player.h and of
// DatabaseEnv.h, and it does not include this header back. GauntletScheduler.h
// does, which is why the Scheduler that belongs beside RunState::state is not
// a member of RunState -- see the note there.

namespace Gauntlet
{
    // The legacy vocabulary that used to open this header -- Effect, Severity
    // and the free-form Affix generator 1 rolled -- was deleted in Phase 2
    // along with the migration that was its only remaining caller. Condition
    // and Boon stayed behind; see below.

    // ---------------------------------------------------------------------
    // Conditions: WHEN an affix applies. Shared by the legacy scalars and by
    // the redesign's Scalar mechanics.
    // ---------------------------------------------------------------------
    enum class Condition : uint8
    {
        Always,
        InCombat,
        OutOfCombat,
        BelowHalfHealth,
        AboveHalfHealth,
        WhileSolo,
        WhileGrouped,
        InDungeon,
        InOpenWorld,
        VersusElites,
        VersusPlayers,
        AtNight,
        AtDay,
        WhileMoving,
        WhileStationary,
        WhileMounted,
        MAX
    };

    // ---------------------------------------------------------------------
    // Boons: the upside paired with a curse. One fixed type per mechanic in
    // the redesign; the magnitude is the card's, one number per row.
    //
    // The enum is append-only and its values are stored in
    // gauntlet_affix.boon, so nothing here may ever be renumbered, reordered
    // or removed. Appending is now unconditionally safe: nothing rolls a boon
    // at all since Phase 2 deleted the Scalars, so no stored affix can be
    // rewritten by a value added at the end.
    //
    // It has two halves, and the split used to be load-bearing.
    //
    // The first eight are *generic*: they name a stat any character has, they
    // have a magnitude row in the generator's BoonTable and a sentence in
    // mechanics/Boons.cpp's BoonClause. Until Phase 2 a Scalar mechanic carried
    // no fixed boon and the generator rolled one out of this half; with the
    // Scalars deleted, nothing is rolled at all -- every boon is named by
    // MechanicDef::boon and delivered by the mechanic that names it.
    //
    // Everything after BonusRegen is *fixed by the registry* and always was:
    // each one names an upside that only makes sense attached to the mechanic
    // that grants it -- which ability's cooldown, whose pet, what the bespoke
    // buff does -- and the blurb is what tells the player.
    // ---------------------------------------------------------------------
    enum class Boon : uint8
    {
        // Generic; the Scalar boon roll draws from these.
        None,
        BonusDamage,
        BonusHealing,
        BonusMoveSpeed,
        BonusExperience,
        BonusMoney,
        BonusMaxHealth,
        BonusRegen,

        // Fixed by the registry; never rolled. See the note above.
        BonusAvoidance,   // you are harder to hit (C15's +5% dodge)
        BonusCooldown,    // one named ability comes back sooner
        BonusAbility,     // a bespoke buff to one ability; the blurb says which
        BonusPetDamage,   // your pet or demon hits harder, not you
        SecondLife,       // a death you walk away from (B1, C43, C44)
        BonusLoot,        // things drop more often; paid once for every card that
                          // names it, in Mgr::OnItemRoll (docs/greed-redesign.md 7.2)
        MAX
    };

    // The last of the generic half. Nothing rolls a boon any more, so this is
    // no longer a bound on a roll; it is what tells the tests, the addon
    // exporter and a future generator where the generic categories stop and
    // the mechanic-specific ones begin. Kept beside the enum rather than inside
    // it so that iterating 0..Boon::MAX still walks real values only.
    constexpr Boon LastGenericBoon = Boon::BonusRegen;

    // The player-facing adjective for a condition ("Desperate") and for a boon
    // ("Wrathful"), shared by both models and defined in GauntletNames.cpp.
    // Both are load-bearing strings: the offer and pick chat lines are built
    // from them and tests/fixtures/legacy_rolls.json records them.
    std::string ConditionName(Condition c);
    std::string BoonName(Boon b);

    // =====================================================================
    // Redesign vocabulary (generator version 2).
    // =====================================================================

    // Families group mechanics for the offer builder's caps and for the
    // "three distinct families per offer" rule.
    enum class Family : uint8 { Spawn, Enemy, Tempo, Attrition, Rules, Bargain, Class, MAX };

    // How much of the run a card changes -- docs/rarity-plan.md section 2. Not
    // "the same card, bigger": that is the rank ladder, which rarity replaces.
    // A common is one small trade, an uncommon a trade with a condition, a rare
    // a verb the player reacts to, an epic changes how a whole system plays,
    // and a legendary defines the run.
    //
    // It is a property of the registry row, and the offer builder rolls
    // *which rarity to draw from* per slot, weighted by tier
    // (Rules::RarityWeight) -- so the escalation of a run lives here now rather
    // than in a rank numeral. Offer carries a copy of the drawn card's rarity
    // so the chat line and the wire do not have to look it up; nothing stores
    // it yet. When the ranks go (plan section 5b) AffixInstance::rank becomes
    // this and takes its column.
    //
    // Append-only, for the reason Boon is: the values go into Data.lua and over
    // the OFFER frame today, and into gauntlet_affix later.
    enum class Rarity : uint8 { Common, Uncommon, Rare, Epic, Legendary, MAX };

    enum MechanicFlags : uint32
    {
        MF_None         = 0,
        MF_Timed        = 1u << 0,   // uses the scheduler clock; counts toward the event budget
        MF_OnKill       = 1u << 1,   // family cap "on-kill"
        // Something that hunts the player, which the addon draws a SUMMON
        // indicator for. NOT the one-per-run cap: that is the "stalker"
        // exclusive key, which S1 and S2 carry and Ambush deliberately does
        // not -- see the note on the Ambush row in GauntletRegistry.cpp. The
        // comment here said "family cap: one per run" for five phases and was
        // wrong about Ambush the whole time.
        MF_Stalker      = 1u << 2,
        MF_RoleTax      = 1u << 3,   // cap: one per run (Cunning, Falter)
        // 1 << 4 was MF_Scalar, "takes a Condition from the condition axis".
        // Phase 2 deleted the last four Scalars and the flag with them; the bit
        // is left unused rather than reassigned, because a stored row from any
        // past run must never be reinterpreted by a flag that changed meaning.
        MF_RewardShaped = 1u << 5,   // satisfies "one reward-shaped offer per tier"
        MF_NotImplemented = 1u << 31
    };

    // What an offer slot is asking the player to do. RankUp was the second of
    // these until the rank system went (docs/rarity-plan.md section 5b); the
    // kind is not stored anywhere -- the log and the wire spell it as text --
    // so the enum simply closes over the gap.
    enum class OfferKind : uint8 { New, Swap, Bargain };

    // Reserved: no mechanic. Registry ids start at 1.
    constexpr uint16 MECHANIC_NONE = 0;

    // Reservations, not mechanics. Withering and Forgetful were the last two
    // legacy scalars and Phase 2 deleted their registry rows along with
    // Exposed (21) and Feeble (22). An id is never reused -- a stored
    // gauntlet_affix row written for one must never resolve to something else
    // -- so these are kept spelled out here, and 21 and 22 with them in
    // the registry's own comment, to say that the numbers are spent.
    constexpr uint16 MECHANIC_WITHERING = 72;
    constexpr uint16 MECHANIC_FORGETFUL = 73;

    // And Unspent, C42, deleted in Phase 6 for the reasons in
    // docs/unspent-replacement-plan.md: it was a character-sheet tax of exactly
    // the kind this redesign exists to delete, half its card described a state
    // the game never reached because nobody banks talent points, and what was
    // left was close to a free damage boon. Killing Floor (74) takes its place
    // in the table but not its number.
    constexpr uint16 MECHANIC_UNSPENT   = 69;

    // The generator version folded into the offer stream. Bump whenever the
    // registry table, the family weights or the offer algorithm change; runs
    // created under an older version keep their stored columns untouched.
    //
    // 4 is Phase 2: four registry rows deleted, fifteen made offerable, four
    // tier windows widened, the condition and boon rolls removed from the
    // stream, and the slot loop taught to prefer a rank-up in an unused family
    // over a new mechanic in one already on the table. Any one of those alone
    // would have required it.
    //
    // Addon::Version is this number and Protocol.lua's PROTOCOL_VERSION must
    // match it, because Data.lua is generated from the registry: the change
    // that moves a mechanic id is exactly the change that must invalidate the
    // addon's table.
    //
    // 16 is step 4 of docs/rarity-plan.md: the ranks are gone. Every ladder
    // collapsed to the one value the card's blurb states, RankUp left the
    // offer kinds and the slot loop with it, and the AFFIX and OFFER frames
    // carry a rarity where they carried a rank. Every offer set moves.
    //
    // 15 is step 3 of docs/rarity-plan.md: reroll and skip. The reroll count
    // is folded into the stream seed -- bits 16-23, between the tier and the
    // seed -- so a rerolled tier is a different, reproducible set; the RUN
    // frame grows the charge count, OFFER_END stays bare, and the inbound
    // verbs REROLL and SKIP join PICK. A count of zero folds to nothing, so
    // every unrerolled offer set is exactly version 14's.
    //
    // 14 is step 2 of docs/rarity-plan.md: the first ten commons, ids 75-84,
    // join the table. Ten new rows move every offer set that could draw them
    // -- which at tier 1 is all of them -- and Data.lua grows the rows.
    //
    // 13 is step 1 of docs/rarity-plan.md: every offer slot rolls a rarity
    // before it rolls a family, which moves every offer set in the game; the
    // OFFER frame grows a rarity field and Data.lua grows a rarity per row and
    // a rarities table, so an addon that predates it would draw every card
    // uncoloured and every offer's last field into the wrong slot.
    // 6 is the tier-axis change: one tier per level instead of one per five,
    // every registry window rescaled x5 so the *level* each affix unlocks at is
    // unchanged, RankFloor and the swap tiers rewritten on the new axis, and a
    // carried-set cap of MAX_CARRIED introduced because eighty offers with
    // nothing refusing them is thirty-odd simultaneous curses and a pegged
    // aggregate.
    //
    // 5 was Phase 3: six registry rows made offerable across two families that
    // had never produced an offer, Cursed Hoard's window moved from tier 4 to
    // 6 so the row and BARGAIN_MIN_TIER agree, and the slot loop taught to
    // prefer an ordinary new mechanic in a clean family over a bargain it
    // could only place by relaxing a rule. The last of those alone changes
    // every offer set that contains a bargain slot.
    // 11 is Phase 10's TOTALS frame. A frame alone would not need a bump --
    // the addon ignores a type it does not know -- but PROTOCOL_VERSION is this
    // number, and an addon that predates the frame would show a rewritten panel
    // with an empty summary in it rather than being told to update.
    //
    // 10 is Phase 7's dead-rank fixes: Ankh Pact and Stone of the Damned drop
    // from four ranks to one, and One Ward's ladder became real. Three rows
    // changing maxRank changes which rank-ups the builder may offer.
    //
    // 9 is Phase 7's PACE frame and Pacing::Fixed. The frame alone would not
    // need a bump -- the addon ignores a type it does not know -- but Fixed
    // changes when a fuse and a telegraph land, and PROTOCOL_VERSION is this
    // number, so an addon that predates the frame should be told to update
    // rather than silently miss the one line that explains the cadences.
    //
    // 8 was Phase 6's second half: MAX_RANK 3 -> 4. Every rank-up that was
    // refused at III is now offered a IV, fifty-seven rows changed maxRank with
    // it, and RankFloor's ceiling moved -- so the rank an offer carries, and
    // therefore every offer set that contains a rank-up, is different.
    //
    // 7 was Phase 6: Unspent (69) deleted from the table and Killing Floor (74)
    // added in its place. A row leaving and a row arriving each move every
    // offer set that could have drawn them, and this one moves the family
    // weights too -- Attrition goes from two rows to three and Class from
    // forty-four to forty-three.
    //
    // 6 was Phase 5: a rank-up stopped being refused by its mechanic's maxTier.
    // The window says when a mechanic may be *introduced*; whether something
    // already carried may deepen is a different question, and answering both
    // with one test froze an affix taken near the end of its window at whatever
    // rank it happened to get. Every offer set that could contain a rank-up of
    // an out-of-window mechanic moves.
    constexpr uint16 GeneratorVersion = 16;

    class IMechanic;   // GauntletMechanic.h

    // One carried affix. `impl` is owned by the RunState that holds this
    // instance and is created from the registry on attach.
    struct AffixInstance
    {
        uint16     mechanic  = MECHANIC_NONE;
        // The card's rarity, copied from the registry row on attach and on
        // load. It is stored in gauntlet_affix's `rank` column -- the column
        // the rank system left behind, the same width -- but never *read* from
        // it: a card's rarity is a fact about the row, and Mgr::Load takes it
        // from the registry so a table re-tuned between two logins cannot
        // leave a stored affix saying something its card no longer says.
        Rarity     rarity    = Rarity::Rare;
        Condition  condition = Condition::Always;
        Boon       boon      = Boon::None;
        uint8      boonMag   = 0;
        uint8      slot      = 0;          // the tier at which it was taken
        uint16     genVersion = 0;         // generator version that produced it

        IMechanic* impl      = nullptr;    // owned by RunState
    };

    // One line of a tier offer.
    struct Offer
    {
        uint16    mechanic  = MECHANIC_NONE;
        Rarity    rarity    = Rarity::Common;   // the card's; see MakeOffer
        Condition condition = Condition::Always;
        Boon      boon      = Boon::None;
        uint8     boonMag   = 0;
        OfferKind kind      = OfferKind::New;
        uint8     swapSlot  = 0;           // meaningful only when kind == Swap
    };

    // Everything one character's run is, and the sole owner of the IMechanic
    // behind each carried affix.
    //
    // AffixInstance is a plain copyable struct with a raw `impl` in it, which
    // is exactly the shape that gets an owning pointer deleted twice. The
    // ownership is therefore pinned here rather than on the instance: the copy
    // constructor and copy assignment are deleted outright, the destructor
    // frees every impl exactly once, and the only ways in and out are Attach
    // and DetachSlot, which create and destroy the implementation with the
    // instance. A moved-from run is left with an empty vector and nothing to
    // free. The one implementation the module builds outside a RunState is the
    // throwaway an offer line is described through, and it is held in a
    // unique_ptr for the length of that line. The members are defined in
    // GauntletMgr.cpp, where IMechanic is complete.
    // How long an offer on the table holds the scheduler back. Long enough to
    // read three cards, short enough that forgetting to pick does not stop the
    // run.
    constexpr uint32 OFFER_QUIET_MS = 20000;   // TODO(design)

    // The offer economy's rows in the run's key/value store (GauntletState.h):
    // the reroll purse, and how many times the pending tier's offers have been
    // rerolled -- which must survive a logout, or a relog would quietly hand
    // back the pre-reroll set and the charge would have bought nothing.
    //
    // The store's convention is "<mechanic key>.<field>" and no registry key
    // is "run", so the prefix cannot collide with a mechanic's counters. The
    // purse deliberately has no initialiser anywhere: a key never written
    // reads as Rules::REROLL_STARTING_CHARGES through Mgr::RerollCharges'
    // fallback, which is also what hands every run started before rerolls
    // existed its starting charges.
    //
    // Rerolls is only meaningful beside RerollTier: the pair says "the offers
    // of tier N have been rebuilt K times". A stale pair from an older tier is
    // ignored by the tier comparison rather than cleaned up.
    namespace RunKeys
    {
        constexpr char const* RerollCharges = "run.reroll_charges";
        constexpr char const* Rerolls       = "run.rerolls";
        constexpr char const* RerollTier    = "run.reroll_tier";
    }

    struct RunState
    {
        uint32 seed        = 0;
        uint32 tier        = 0;
        bool   dead        = false;        // hardcore: the run is over
        uint16 genVersion  = GeneratorVersion;
        uint8  playerClass = 0;            // CLASS_WARRIOR ...; 0 until first login

        std::vector<AffixInstance> affixes;

        // The offers currently on the table and the tier they were built for.
        // They are never stored: BuildOffers rebuilds them from the seed.
        std::vector<Offer> pending;
        uint32 pendingTier = 0;

        // How long the offer has been on the table, in milliseconds.
        //
        // An offer suppresses every scheduled event, which is right for the few
        // seconds someone spends reading three cards and wrong for ever. A
        // player who did not pick -- or did not notice -- had the whole run
        // freeze silently: the queue kept filling with events that came due and
        // were held, and from inside the game it looked exactly like the module
        // being broken. It was reported as exactly that.
        //
        // So the pause is time-boxed. Take as long as you like over the choice;
        // the world starts moving again after OFFER_QUIET_MS either way.
        uint32 offerMs = 0;

        // Death is no longer instant (plan section 6, decision 5): dying arms
        // this timer, releasing or letting it run out ends the run, and a
        // Phase 3 bargain charge is what will cancel it.
        bool   pendingDeath = false;
        uint32 deathTimerMs = 0;

        // Set by anything that changes a column of gauntlet_run, cleared by
        // Save, so a logout does not rewrite a row nothing touched.
        bool   dirty = false;

        // Raised for the duration of a self-damage the module applies to its
        // own player -- Blood Magic's health cost is the only one -- so the
        // rest of the module can tell it apart from a blow the world landed.
        // It cannot be told apart any other way: the attacker is the player in
        // both cases.
        //
        // While it is up: no Deep Wound is made of it, no Last Rites charge is
        // spent on it, and nothing on the kill path counts it. Set and cleared
        // around the one DealDamage call, never held across a tick.
        bool   selfDamage = false;

        // Plan section 3.3's per-mechanic key/value store: Champions' fight
        // counter, the Shade's nemesis rank, Deep Wounds' wound. Loaded once on
        // login -- before OnAttach, because a Shade that reads shade.rank out of
        // an empty map answers "never left behind" for a nemesis that has been
        // left behind four times -- and written on logout, on death, on a pick
        // and every sixty seconds while it is dirty. It lives here because it is
        // run data and its lifetime is exactly the run's.
        //
        // The Scheduler that goes with it does not, and cannot: every mechanic
        // reaches its clock through Ctx, GauntletScheduler.h includes this
        // header to spell MECHANIC_NONE, and a header cannot include something
        // that includes it back. Holding one by pointer would put an owning
        // pointer and an out-of-line constructor into a header three test
        // binaries compile, for no gain. Mgr keeps one Scheduler per loaded run
        // instead, on a map keyed by the same ObjectGuid and erased by the same
        // Forget; Mgr::ClockFor is what fills Ctx::clock.
        State  state;

        // Counts down in milliseconds. No scheduled event fires while it is
        // non-zero, which is design section 4.2's grace: a character is never
        // ambushed before the player has taken control of it. Set on login and
        // on a zone change from Gauntlet.Grace.Seconds.
        uint32 graceMs = 0;

        // Which affix last did something to this character, and how much longer
        // that claim stays true. KILLBY is built out of the pair, and it is a
        // pair rather than a single field because a Falling Sky that struck
        // twenty minutes ago has no business being named for a death now.
        uint16 lastActor   = MECHANIC_NONE;
        uint32 lastActorMs = 0;

        // The entry of the last creature to hit this player, for the one
        // bargain whose price is fighting it again. Not persisted: a warlock
        // who logs out between dying and returning has had their rematch
        // cancelled by something bigger than an affix.
        uint32 lastKillerEntry = 0;

        // How long a claim on "the last affix to act" stays true. Design
        // section 4.8's fourth question is answered from it, and the pair
        // exists rather than a single field because a Falling Sky that struck
        // twenty minutes ago has no business being named for a death now.
        // Fifteen seconds is long enough to cover a fight that a mechanic's
        // blow started and short enough that an unrelated death is not blamed
        // on it. The value moved here from GauntletMgr.cpp unchanged.
        static constexpr uint32 ACTOR_MEMORY_MS = 15000;   // TODO(design)

        // Mgr::NoteActor does this for a scheduler Fire and for a blow from a
        // creature this module summoned, which covers most of the set. A
        // mechanic that hurts its owner on its own tick -- Grudge standing on a
        // corpse, Death Rattle's burst -- has to say so itself, and this is how,
        // without dragging GauntletMgr.h and Player.h into a mechanic.
        void NoteActor(uint16 mechanic)
        {
            if (mechanic == MECHANIC_NONE)
                return;
            lastActor   = mechanic;
            lastActorMs = ACTOR_MEMORY_MS;
        }

        RunState() = default;
        ~RunState();

        RunState(RunState const&)            = delete;
        RunState& operator=(RunState const&) = delete;
        RunState(RunState&& other) noexcept;
        RunState& operator=(RunState&& other) noexcept;

        // Adds a carried affix and gives it its implementation, which may be
        // null for a mechanic this build does not implement. Returns the
        // stored instance so the caller can hand it to IMechanic::OnAttach.
        AffixInstance& Attach(AffixInstance instance);

        // Destroys the affix in `slot`, implementation included. False when
        // the slot holds nothing.
        bool DetachSlot(uint8 slot);

        // Destroys every carried affix. Called by the destructor.
        void Clear();

        AffixInstance* Find(uint16 mechanic);
        AffixInstance* AtSlot(uint8 slot);
    };

    // What Mgr::Aggregate can be asked for. Every kind is a multiplier
    // applied to a base value; the caps in §2.5 of the plan clamp the
    // product, not the individual contributions.
    enum class AggregateKind : uint8
    {
        DamageTaken,
        DamageDone,
        HealTaken,
        MaxHealth,
        EnemySpeed,
        Experience,
        MAX
    };

    // Whether an offer on the table should still be holding a card back.
    //
    // Ten mechanics wrote `!ctx.run->pending.empty()` by hand, and every one of
    // them therefore went silent for as long as an offer sat unpicked -- the
    // whole Enemy family at once, reported from play as "grudge, death rattle
    // is now not visible on mob death". It is the same fault the scheduler had
    // and it is fixed the same way: see RunState::offerMs. The pause is a
    // courtesy while three cards are read, not an indefinite stop.
    //
    // Written once here so the next card cannot get it wrong, and so that
    // changing the rule changes it everywhere.
    inline bool OfferHoldsBack(RunState const& run)
    {
        return !run.pending.empty() && run.offerMs < OFFER_QUIET_MS;
    }

    std::string AggregateKindName(AggregateKind kind);

    // Clamps on the aggregate product. Defaults are the plan's §2.5 values;
    // every field is overridable from mod_gauntlet.conf.
    struct AggregateCaps
    {
        float damageTakenMin = 1.0f;
        float damageTakenMax = 2.0f;
        float damageDoneMin  = 0.6f;
        float healTakenMin   = 0.5f;
        float maxHealthMin   = 0.6f;
        float enemySpeedMax  = 1.4f;
    };

    // The generator, the registry and the aggregate maths never see a
    // Player: they see this. GauntletMgr supplies the live implementation,
    // the unit tests supply a stub.
    class IPlayerView
    {
    public:
        virtual ~IPlayerView() = default;

        virtual uint8  GetClass() const = 0;               // CLASS_WARRIOR ... CLASS_DRUID
        virtual uint8  GetLevel() const = 0;
        virtual bool   HasSpell(uint32 spellId) const = 0; // Player::HasSpell
        // The talent tree the character has actually committed to, encoded as
        // tabpage + 1: 1/2/3 are the three tabs in client order, and 0 means
        // "no spec yet". The +1 exists because Player::GetMostPointsTalentTree
        // returns a raw 0-based tabpage and also returns 0 for a character that
        // has spent no talent points at all, which would make the first tree of
        // every class indistinguishable from "untalented". MechanicDef::
        // requiresTree uses the same encoding, so a spec gate is just
        // `requiresTree == 0 || requiresTree == GetTalentTree()` and an
        // untalented character satisfies no gate.
        virtual uint8  GetTalentTree() const = 0;

        // 1 << (class - 1), the core's getClassMask() convention.
        uint32 GetClassMask() const { return 1u << (GetClass() - 1); }
    };

    std::string FamilyName(Family f);
    std::string OfferKindName(OfferKind k);

    // "Rare", and the six hex digits the client paints a rare item in. Both
    // defined in GauntletNames.cpp; the colour is exported into Data.lua so the
    // addon and the chat line cannot paint the same card two ways.
    std::string RarityName(Rarity r);
    char const* RarityColor(Rarity r);
}

#endif // MOD_GAUNTLET_H
