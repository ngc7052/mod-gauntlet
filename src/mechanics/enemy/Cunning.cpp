/*
 * mod-gauntlet - E7 Cunning: enemies in melee range kick the spell you are casting
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletRules.h"
#include "GauntletSummons.h"
#include "../Boons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Unit.h"

#include <algorithm>
#include <string>
#include <vector>
#include <iterator>

// Registry id 12. Design section 3, card E7: "Enemies in melee range kick the
// spell you are casting, once every 12 seconds each."
//
// The skill this affix asks for is fake-casting, and the 0.5 s arming window is
// what makes it possible: a cast that is cancelled before it arms eats the kick
// and the school stays open. Everything else on the card -- cast at range before
// they close, instants and DoTs, root or stun the kicker -- is a button the
// class already owns, which is design section 2.8's first principle.
//
// It is deterministic, which is deliberate. A random-proc silence is a coin flip
// against a permadeath run (design section 2.9); a per-attacker cooldown the
// player can count is a puzzle.
//
// It shares the single "role tax" slot with Falter through the registry's
// "roletax" key, and it is only offered to classes with cast-time spells --
// otherwise it is Hades' free-heat problem, an offer that costs a warrior
// nothing.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_CUNNING = 12;

        // The card's ladder: cooldown 15 -> 12 -> 8 s, lock 2 -> 3 -> 4 s.
        // Rank IV is 6 s and 4 s, and the lock deliberately does not move
        // with it: four locked seconds out of every six is already two
        // thirds of a caster's uptime denied while something is in melee
        // range, and a fifth would leave a window too short to land a cast
        // in at all. A role tax with no window is not a tax, it is a ban.
        constexpr uint32 KICK_CD_MS = 12000;
        constexpr uint32 LOCK_MS = 3000;

        // The card's two fixed numbers: five yards of reach, and the half
        // second at each end of the cast that decides whether the kick lands.
        constexpr float  MELEE_YARDS = 5.0f;
        constexpr int32  ARM_MS      = 500;

        // How many attackers carry their own cooldown at once. Beyond this the
        // player is dead for other reasons.
        constexpr std::size_t MAX_KICKERS = 8;   // TODO(design)


        char const* MechanicName()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_CUNNING);
            return def ? def->name : "Cunning";
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_CUNNING);
            return def ? def->key : "cunning";
        }

        class Cunning final : public IMechanic
        {
        public:
            void OnAttach(Ctx&) override { _kickers.clear(); }
            void OnDetach(Ctx&) override { _kickers.clear(); }
            void OnLeaveCombat(Ctx&) override { _kickers.clear(); }
            void OnTick(Ctx& ctx, uint32 diffMs) override;

            // The cast that lands. docs/greed-redesign.md section 3: a card
            // that only interrupts is a brake with a special hatred for
            // casters, and the whole puzzle -- fake-cast, root, cast at range
            // -- stays. What is added is the reason to stop solving it: once a
            // kicker has spent its kick, the next cast in its face is the high
            // roll.
            //
            // OnSpellCast fires as the cast completes (Spell.cpp:3776), so a
            // cast that was interrupted never reaches here and never pays.
            void OnSpellCast(Ctx& ctx, Spell* spell) override
            {
                _payoffSpellId = 0;

                Player* player = ctx.player;
                if (!player || !spell || !spell->GetSpellInfo())
                    return;

                for (Kicker const& kicker : _kickers)
                {
                    Creature* c = ObjectAccessor::GetCreature(*player, kicker.guid);
                    if (!c || !c->IsAlive())
                        continue;
                    if (player->GetDistance(c) > MELEE_YARDS)
                        continue;

                    _payoffSpellId = spell->GetSpellInfo()->Id;
                    return;
                }
            }

            float DamageDoneMult(Ctx& /*ctx*/, Unit* /*victim*/, SpellInfo const* info) override
            {
                if (_payoffSpellId == 0 || !info || info->Id != _payoffSpellId)
                    return 1.0f;

                // Spent on the first thing it pays for: a spell that hits five
                // targets is one cast, and one cast is one reward.
                _payoffSpellId = 0;
                ++_paid;
                return Rules::CunningPayoffMult();
            }

            // BonusDamage. The curse takes casts away; the boon makes the ones
            // that land worth more, which is the trade the card describes when
            // it tells a caster to use fewer, bigger casts.
            float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
            {
                return BoonFactor(self, kind);
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx&) const override
            {
                std::string out = "cunning: " + std::to_string(_kickers.size()) + " kicker(s) tracked, "
                                + std::to_string(_paid) + " cast(s) paid";
                out += _payoffSpellId != 0 ? "; a cast is marked and waiting to land"
                                           : "; nothing marked";
                return out;
            }

        private:
            // One attacker's personal cooldown, counted down here rather than
            // stored on the creature: the creature is not ours and may be gone
            // between two ticks, and a guid plus a number cannot dangle.
            struct Kicker
            {
                ObjectGuid guid;
                uint32     readyInMs = 0;
            };

            Kicker* Find(ObjectGuid const& guid);
            void    Kick(Ctx& ctx, Player* player, Creature* kicker, Spell* spell);

            std::vector<Kicker> _kickers;
            uint32              _payoffSpellId = 0;
            uint32              _paid          = 0;
        };

        Cunning::Kicker* Cunning::Find(ObjectGuid const& guid)
        {
            for (Kicker& kicker : _kickers)
                if (kicker.guid == guid)
                    return &kicker;
            return nullptr;
        }

        void Cunning::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            // Every cooldown runs whether or not there is a cast to kick, so a
            // player who stops casting for twelve seconds really does get a
            // clean window afterwards.
            for (std::size_t i = _kickers.size(); i-- > 0; )
            {
                Kicker& kicker = _kickers[i];
                if (kicker.readyInMs > diffMs)
                {
                    kicker.readyInMs -= diffMs;
                    continue;
                }

                // Ready again, and no longer worth a record: the set is rebuilt
                // from the attacker list, so an absent record simply means
                // "ready".
                _kickers.erase(_kickers.begin() + static_cast<std::ptrdiff_t>(i));
            }

            if (!player->IsInWorld() || !player->IsAlive() || !player->IsInCombat())
                return;
            if (ctx.run && (ctx.run->dead || OfferHoldsBack(*ctx.run)))
                return;

            // CURRENT_GENERIC_SPELL is the slot a cast-time spell occupies;
            // channels live in CURRENT_CHANNELED_SPELL and are deliberately
            // left alone, because the card says "the spell you are casting" and
            // a channel's counterplay -- move, and it breaks -- is not the
            // fake-cast this affix is about.
            Spell* spell = player->GetCurrentSpell(CURRENT_GENERIC_SPELL);   // Unit.h:1577
            if (!spell || spell->getState() != SPELL_STATE_PREPARING)        // Spell.h:496
                return;

            int32 const total     = spell->GetCastTime();                    // Spell.h:562
            int32 const remaining = spell->GetCastTimeRemaining();           // Spell.h:566
            if (total <= 0 || remaining <= 0)
                return;

            // "casting for at least 0.5 s and more than 0.5 s remains". The
            // first half is the fake-cast window and the second is the promise
            // that a kick always costs the player something -- interrupting a
            // cast that was about to finish anyway would be free.
            if (total - remaining < ARM_MS || remaining <= ARM_MS)
                return;

            // The nearest ready attacker inside melee range, which is the
            // card's own rule and the reason backing off two steps mid-cast is
            // a real answer.
            Creature* best     = nullptr;
            float     bestDist = MELEE_YARDS + 1.0f;

            for (Unit* attacker : player->getAttackers())                    // Unit.h:901
            {
                Creature* creature = attacker ? attacker->ToCreature() : nullptr;
                if (!creature || !creature->IsAlive() || !creature->IsInWorld())
                    continue;
                if (!IsOrdinaryFoe(creature))
                    continue;

                // A creature this module summoned does not kick. The Shade and
                // the Echo are fights the player was told about; adding a
                // silence to them is a second affix nobody picked.
                if (sGauntletSummons->IsGauntletSummon(creature))
                    continue;

                if (Find(creature->GetGUID()))
                    continue;   // on its own cooldown

                float const dist = player->GetDistance(creature);
                if (dist > MELEE_YARDS || dist >= bestDist)
                    continue;

                bestDist = dist;
                best     = creature;
            }

            if (!best)
                return;

            Kick(ctx, player, best, spell);
        }

        void Cunning::Kick(Ctx& ctx, Player* player, Creature* kicker, Spell* spell)
        {

            SpellInfo const* info = spell->GetSpellInfo();
            SpellSchoolMask const school = info ? info->GetSchoolMask() : SPELL_SCHOOL_MASK_NORMAL;

            // The interrupt itself. `withDelayed` false and `withInstant` false:
            // this kicks the cast in progress and nothing else, so an instant
            // queued behind it and a spell already in flight both survive --
            // which is what makes "use instants and DoTs" a real answer rather
            // than a slogan.
            player->InterruptNonMeleeSpells(false, 0, false);                // Unit.h:1597

            // And the lock. Player overrides ProhibitSpellSchool with the real
            // implementation, which also sends SMSG_SPELL_COOLDOWN so the
            // client greys the school out -- the player can see what was taken
            // and for how long, which design section 4.8 requires.
            player->ProhibitSpellSchool(school, LOCK_MS);                 // Unit.h:1585

            while (_kickers.size() >= MAX_KICKERS)
                _kickers.erase(_kickers.begin());

            Kicker record;
            record.guid      = kicker->GetGUID();
            record.readyInMs = KICK_CD_MS;
            _kickers.push_back(record);

            // This affix can end a run by taking a heal away, so it claims the
            // death the moment it acts.
            if (ctx.run)
                ctx.run->NoteActor(MECHANIC_CUNNING);

            if (ctx.addon)
                ctx.addon->SendEvent(player, MechanicKey(), LOCK_MS / 1000u, MechanicName());

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r {} cuts your cast short.",
                    kicker->GetNameForLocaleIdx(LOCALE_enUS));
        }

        std::string Cunning::Describe(AffixInstance const& self) const
        {

            std::string out = "Every enemy within five yards can interrupt the spell you are"
                              " casting once every " + std::to_string(KICK_CD_MS / 1000u)
                            + " seconds, locking that school for " + std::to_string(LOCK_MS / 1000u)
                            + ". It only fires half a second into a cast with more than half a"
                              " second left, so a cancelled cast eats the kick.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(12, Cunning);
}
