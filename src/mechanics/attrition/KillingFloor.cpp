/*
 * mod-gauntlet - A5 Killing Floor: healing comes from killing, not from resting
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAddon.h"
#include "GauntletMechanic.h"
#include "GauntletRules.h"
#include "GauntletState.h"
#include "GauntletSummons.h"
#include "Chat.h"
#include "Creature.h"
#include "Player.h"
#include "../Boons.h"

#include <algorithm>
#include <limits>
#include <string>
#include <iterator>

// Registry id 74. It replaces Unspent (69), which was retired rather than
// rewritten -- an id is never reused -- and the reasoning is in
// docs/unspent-replacement-plan.md. The short version: Unspent was a
// character-sheet tax, half its card described a state the game never reached
// because nobody banks talent points, and what was left was close to a free
// damage boon.
//
// What this is instead, and why it is shaped the way it is:
//
//   - **Classless.** Unspent was the only row in the table available to every
//     class that was not gated on a resource, and losing that would have made
//     the table thinner in the one place it could least afford it.
//
//   - **MF_RewardShaped, and that is the whole reason it exists.** Phase 5
//     measured that the largest single relaxation in the module is the "one
//     reward-shaped offer per tier" guarantee failing, not the table running
//     out: ten of sixty-nine rows carry the flag and six are gated behind a
//     class or the Bargain family, so a character has four generally available
//     and two of those expire at tier 50. One more, classless, in a window that
//     runs the length of the run, was measured as worth more than wave B's
//     twenty-one curses were.
//
//   - **A bargain in structure**, which is the design's own test for the flag:
//     it pays out for engagement. The curse and the payment are two halves of
//     one sentence, the way Frenzy's and Berserker's Bargain's are, so there is
//     no BoonClause and the registry row's boon is None. The kill burst is the
//     upside and it is stated in the blurb.
//
// The loop is the one Hades and Dead Cells run on: healing is not something you
// retreat and do, it is something the fight gives you for finishing it. In a
// hardcore module that is the resource the run is actually managing, and
// nothing in the table touched it before this except Deep Wounds' ceiling and
// Last Rites' mark.
//
// Three things it deliberately is not:
//
//   - It owns no clock. It arms no scheduler event, so it spends none of the
//     event budget and its row must never gain MF_Timed. OnTick is used only to
//     notice that a fight has ended, which is a poll and not an event.
//   - It is not MF_OnKill. That flag means "this fires on a kill and counts
//     against CAP_ON_KILL", and the curse here is the healing block, which is
//     continuous. The kill is the release, not the trigger.
//   - It holds nothing that can outlive it: no Player pointer, no guid, no
//     summon. Every member is a number or a bool.

namespace Gauntlet
{
    namespace
    {
        // The ladders and the arithmetic live in GauntletRules.h so that
        // tests/RulesTest.cpp can reach them: this file includes Player.h and
        // is therefore invisible to the Player-free test build.
        using namespace Gauntlet::Rules;

        // The addon's STAT key, spelled out the way DeepWounds spells its own:
        // the registry key is the contract with Data.lua and a literal here
        // cannot drift from it without RegistryTest noticing the key changed.
        constexpr char const* STAT_KEY = "a05_killing_floor";


        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        // What a kill gives back, as a percentage of maximum health. It falls
        // with rank, because the curse half is fixed -- healing is blocked, at
        // every rank -- and the only axis left to make a rank harder is how
        // much the release is worth.
        // What share of the banked healing a kill hands back.
        //
        // The card used to refuse healing outright and pay a flat percentage of
        // maximum health on a kill. Refusing a core verb is not a cost, it is
        // an amputation: the player has a healthbar and a button and the card
        // says no, and the compensation arrives on a kill they may not live to
        // make. It was reported from play as one of six cards that felt like
        // taxes, and it is the harshest of the six.
        //
        // Healing is now *delayed rather than denied*. Every heal that lands in
        // combat goes into a bank; a kill pays the bank out. Nothing is taken
        // away -- it is held, and the player decides whether to push for the
        // kill that releases it or break off and take what is left. See
        // docs/tempo-redesign.md.
        // What the bank loses if the fight ends without a kill to release it.
        //
        // This is what stops "walk away" being strictly better than "win". At
        // rank I nothing is lost and the card is pure delay; by rank IV half
        // the bank evaporates, and breaking off becomes a real price rather
        // than a free reset.
        // Rank III keeps the block up after the fight, so disengaging is no
        // longer an instant out. Zero at ranks I and II: leaving combat lifts
        // it at once.
        // Rank IV holds it fifteen seconds and gives back 5%: both halves
        // move, so the top rank is a real escalation of the same question --
        // is this fight worth finishing -- rather than only a smaller reward.
        constexpr uint32 LINGER_MS = 0;

        class KillingFloor final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override
            {
                _wounded = false;
                _lingerMs = 0;
                Publish(ctx);
            }

            void OnDetach(Ctx& ctx) override
            {
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, STAT_KEY, 0);
            }

            // Wounding something arms the block. Deliberately on damage dealt
            // to a creature rather than on entering combat: a fight you have
            // not yet touched is not one you are being punished for, and a mob
            // that aggroed you across a room while you were drinking should not
            // cut the drink off before you have swung at it.
            void OnCreatureDamaged(Ctx& ctx, Creature* victim, uint32 damage) override
            {
                if (!victim || damage == 0)
                    return;

                // Not the module's own creatures. A Shade is not something the
                // player chose to start, and a stalker that cannot be outrun
                // would otherwise hold the block open for as long as it lives.
                if (sGauntletSummons->IsGauntletSummon(victim))
                    return;

                if (!_wounded)
                {
                    _wounded = true;
                    Publish(ctx);
                    Announce(ctx);
                }
                _lingerMs = 0;
            }

            // The block. OnHeal and not HealTakenMult, for Last Rites' reason:
            // this is an absolute limit rather than a coefficient, and the
            // aggregate's HealTaken floor (Gauntlet.Caps.HealTaken, 0.5) is a
            // bound on multipliers. A mechanic that means "none" has to say so
            // here or the floor would quietly turn it into "half".
            void OnHeal(Ctx& /*ctx*/, uint32& heal) override
            {
                if (!_wounded || heal == 0)
                    return;

                // Banked, not burned. The number is what comes back.
                _blocked += heal;
                _bank    += heal;
                heal = 0;
            }

            // The release. Straight through ModifyHealth rather than as a heal,
            // and that is not an optimisation: a heal would go back through
            // ModifyHealReceived, reach OnHeal above, and be zeroed by the very
            // block it is meant to be the answer to.
            void OnKill(Ctx& ctx, Creature* killed) override
            {
                Player* player = ctx.player;
                if (!player || !killed)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;
                if (sGauntletSummons->IsGauntletSummon(killed))
                    return;
                if (!_wounded)
                    return;   // nothing was being withheld, so nothing is released

                if (_bank == 0)
                    return;   // nothing was withheld, so there is nothing to release

                int32 const heal = int32(std::min<uint64>(
                    KillingFloorPayout(_bank),
                    std::numeric_limits<int32>::max()));
                _bank = 0;
                if (heal <= 0)
                    return;

                // Clamped so the burst reports what it actually gave. A kill at
                // full health is not a wasted mechanic -- it is the ordinary
                // case -- but counting it as healing would make Diagnose lie.
                uint32 const before = player->GetHealth();
                player->ModifyHealth(heal);
                _healed += player->GetHealth() - before;
                ++_kills;
            }

            void OnTick(Ctx& ctx, uint32 diffMs) override
            {
                Player* player = ctx.player;
                if (!player || !_wounded)
                    return;

                // Still fighting: the block stands and the linger is not
                // running. Polled rather than driven off a leave-combat hook
                // because the module does not dispatch one, and a 500 ms poll
                // on a bool is cheaper than the hook would have been.
                if (player->IsInCombat())
                {
                    _lingerMs = 0;
                    return;
                }

                uint32 const linger = LINGER_MS;
                _lingerMs += diffMs;
                if (_lingerMs < linger)
                    return;

                // The fight ended without a kill to release the bank. What is
                // left is paid out minus the rank's loss, so breaking off is a
                // choice with a price rather than a free reset.
                if (_bank != 0)
                {
                    int32 const back = int32(std::min<uint64>(
                        KillingFloorLeaveBack(_bank),
                        std::numeric_limits<int32>::max()));
                    _bank = 0;
                    if (back > 0)
                    {
                        uint32 const before = player->GetHealth();
                        player->ModifyHealth(back);
                        _healed += player->GetHealth() - before;
                    }
                }

                _wounded  = false;
                _lingerMs = 0;
                Publish(ctx);
            }

            std::string Describe(AffixInstance const& /*self*/) const override
            {

                // No BoonClause: the upside is the other half of the same
                // sentence, not a separate promise. Frenzy and Berserker's
                // Bargain read the same way and for the same reason.
                std::string out = "While you are in a fight with something you have wounded, healing"
                                  " does not reach you -- it is held. A kill hands back "
                                + std::to_string(KILL_PAYOUT_PCT) + "% of what is held.";

                if (LEAVE_LOSS_PCT != 0)
                    out += " Break off instead and " + std::to_string(LEAVE_LOSS_PCT)
                         + "% of it is lost.";

                if (LINGER_MS != 0)
                    out += " The block holds for " + std::to_string(LINGER_MS / 1000)
                         + " seconds after the fight ends, so running is no longer an answer.";
                else
                    out += " Leaving the fight lifts it.";

                // Said out loud because a player will try it and it would
                // otherwise look like the affix failing rather than the affix's
                // stated shape: food and drink restore health through a
                // regeneration aura, not through a heal, so this never sees
                // them.
                out += " Food and drink still work.";

                return out;
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                return std::string("killing floor: ") + (_wounded ? "blocking" : "clear")
                     + ", " + std::to_string(_bank) + " held now, "
                     + std::to_string(_blocked) + " healing banked in all, "
                     + std::to_string(_kills) + " kill(s) released "
                     + std::to_string(_healed) + " health"
                     + (_wounded && ctx.player && !ctx.player->IsInCombat()
                            ? ", linger " + std::to_string(_lingerMs / 1000) + "s"
                            : "");
            }

        private:
            // The HUD's readout is the whole of the decision the affix creates:
            // "can I be healed right now" is a yes or a no, and a player who
            // cannot see it is playing a mechanic they have to infer.
            void Publish(Ctx& ctx)
            {
                if (ctx.player)
                    AddonFor(ctx)->QueueStat(ctx.player, STAT_KEY, _wounded ? 1 : 0);
            }

            // Once per session. The first time healing silently does nothing is
            // the moment a player decides the module is broken, and a line at
            // that moment is the difference between a mechanic and a bug.
            void Announce(Ctx& ctx)
            {
                if (_warned || !ctx.player || !ctx.player->GetSession())
                    return;

                _warned = true;
                ChatHandler(ctx.player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Killing Floor: nothing will heal you until this fight is"
                    " finished.");
            }

            uint64 _blocked  = 0;

            // Healing held back and not yet handed over.
            uint64 _bank     = 0;
            uint64 _healed   = 0;
            uint32 _kills    = 0;
            uint32 _lingerMs = 0;
            bool   _wounded  = false;
            bool   _warned   = false;
        };
    }

    GAUNTLET_MECHANIC(74, KillingFloor);
}
