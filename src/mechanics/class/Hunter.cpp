/*
 * mod-gauntlet - the hunter's three: Half-Tamed, Dead Weight, Wide Dead Zone
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletSummons.h"
#include "../Boons.h"
#include "PermanentCooldown.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Pet.h"
#include "PetDefines.h"
#include "Player.h"
#include "Position.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <cmath>
#include <string>
#include <iterator>

// Design section 3, family C, hunter. Engine: ranged auto and Steady Shot, the
// pet, Aspects, traps. The design names it the most-died class in the official
// Classic Hardcore count, with two signature deaths: a resisted Feign Death, and
// Cheetah's daze with three mobs behind you. All three curses below are about
// the second half of that -- what a hunter does when the reset button is gone.

namespace Gauntlet
{
    namespace
    {
        constexpr uint32 SPELL_FEIGN_DEATH = 5384;   // the registry's own requiresSpell for C10
        constexpr uint32 SPELL_DISENGAGE   = 781;

        uint8 RankIndexOf(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* KeyOf(uint16 id, char const* fallback)
        {
            MechanicDef const* def = FindMechanic(id);
            return def ? def->key : fallback;
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        // ==================================================================
        // C9 - Half-Tamed (36)
        //
        // "An unhappy pet turns on you."
        //
        // The companion verb, and the card is unusually direct about the point:
        // "a hunter who manages loyalty never sees this affix act; that is the
        // point." Feeding the pet is the button nobody presses after level 20,
        // and this is what makes it a button again.
        // ==================================================================
        constexpr uint16 MECHANIC_HALF_TAMED = 36;

        // The card's ladder is what counts as unhappy enough to break the leash.
        // Rank III also breaks at Content, which is most of the time for a
        // hunter who is not feeding.
        constexpr uint32 TURNS_AT[] = { UNHAPPY, UNHAPPY, CONTENT };
        static_assert(std::size(TURNS_AT) >= MAX_RANK, "TURNS_AT is short a rank");

        // The card's two numbers: fifteen seconds, twenty-five at rank III.
        constexpr uint32 HOSTILE_MS[] = { 15000, 15000, 25000 };
        static_assert(std::size(HOSTILE_MS) >= MAX_RANK, "HOSTILE_MS is short a rank");

        // Not on the card. A short grace after a break so a hunter who calls the
        // pet straight back into the same unhappiness is not attacked twice in
        // the same breath.
        constexpr uint32 REGRIP_MS = 30000;   // TODO(design)

        class HalfTamed final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                if (ctx.player)
                    sGauntletSummons->DespawnFor(ctx.player, MECHANIC_HALF_TAMED);
            }

            void OnTick(Ctx& ctx, uint32 diffMs) override;

            // Boon::BonusPetDamage, which has existed since Phase 0 with nothing
            // able to pay it until Phase 4 added the dispatch point. The card:
            // "Happy pets deal +10% damage" -- so it is paid only while the pet
            // is actually happy, which is the same behaviour the curse is
            // trying to teach seen from the other side.
            void OnPetDamage(Ctx& ctx, Unit* /*victim*/, uint32& damage) override
            {
                if (!ctx.player || !ctx.self || ctx.self->boonMag == 0)
                    return;

                Pet* pet = ctx.player->GetPet();
                if (!pet || pet->GetHappinessState() != HAPPY)      // Pet.h:84
                    return;

                damage += uint32(uint64(damage) * ctx.self->boonMag / 100u);
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                std::string out = "half-tamed: ";
                if (Pet* pet = ctx.player ? ctx.player->GetPet() : nullptr)
                {
                    uint32 const state = uint32(pet->GetHappinessState());
                    out += state == HAPPY ? "pet happy" : (state == CONTENT ? "pet content" : "pet UNHAPPY");
                }
                else
                {
                    out += "no pet";
                }
                out += ", " + std::to_string(_broke) + " break(s), grace "
                     + std::to_string(_graceMs / 1000u) + "s";
                return out;
            }

        private:
            void Break(Ctx& ctx, Pet* pet);

            uint32 _graceMs = 0;
            uint32 _broke   = 0;
        };

        void HalfTamed::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            if (_graceMs != 0)
            {
                _graceMs = _graceMs > diffMs ? _graceMs - diffMs : 0;
                return;
            }

            if (ctx.run && ctx.run->dead)
                return;
            if (!player->IsInWorld() || !player->IsAlive())
                return;

            Pet* pet = player->GetPet();
            if (!pet || !pet->IsAlive())
                return;

            if (uint32(pet->GetHappinessState()) > TURNS_AT[RankIndexOf(ctx.self)])
                return;

            Break(ctx, pet);
        }

        void HalfTamed::Break(Ctx& ctx, Pet* pet)
        {
            Player* player = ctx.player;

            uint32 const   entry = pet->GetEntry();
            Position const at    = pet->GetPosition();

            // The real pet goes away first, so the copy is not standing on top
            // of it and the hunter is not fighting two of the same creature
            // with one name. PET_SAVE_NOT_IN_SLOT keeps it callable: the card
            // says "the real pet can be called back afterwards".
            player->RemovePet(pet, PET_SAVE_NOT_IN_SLOT);        // Player.h:1232

            _graceMs = REGRIP_MS;
            ++_broke;

            Creature* feral = sGauntletSummons->Summon(player, entry, at,
                                                       HOSTILE_MS[RankIndexOf(ctx.self)],
                                                       /*countsAsStalker*/ false,
                                                       MECHANIC_HALF_TAMED);

            if (feral)
            {
                if (CreatureAI* ai = feral->AI())
                    ai->AttackStart(player);

                feral->AddThreat(player, 1.0f);
            }

            AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_HALF_TAMED, "c09_half_tamed"),
                                     HOSTILE_MS[RankIndexOf(ctx.self)] / 1000u, "Half-Tamed");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Your pet has had enough of you. Feed it next time.");

        }

        std::string HalfTamed::Describe(AffixInstance const& self) const
        {
            uint8 const  i    = RankIndexOf(&self);
            bool const   soon = TURNS_AT[i] >= CONTENT;
            uint32 const secs = HOSTILE_MS[i] / 1000u;

            std::string out = std::string("A pet that is ")
                            + (soon ? "no better than content" : "unhappy")
                            + " breaks its leash: it is dismissed and a wild copy of it attacks you"
                              " for " + std::to_string(secs) + " seconds. You can call it back"
                              " afterwards. Feed it and this never happens.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }

        // ==================================================================
        // C10 - Dead Weight (37)
        //
        // "Feign Death has a three-minute cooldown."
        //
        // The shortcut verb, and the design's own note on the class is why it
        // is here: a resisted Feign Death is one of the two signature hunter
        // deaths, and this affix removes the assumption that it will be there
        // at all. Disengage, Frost Trap, Concussive Shot and a pet holding
        // aggro are the kite the class was built for; what goes is the reset.
        // ==================================================================
        // 3 min, 5 min, then gone. Rank III is the card's "removes it", which is
        // the family ladder's price -> higher price -> removal in one row.
        constexpr uint32 FEIGN_COOLDOWN_MS[] = { 180000, 300000, 0 };
        static_assert(std::size(FEIGN_COOLDOWN_MS) >= MAX_RANK, "FEIGN_COOLDOWN_MS is short a rank");

        class DeadWeight final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override { Sync(ctx); }

            void OnDetach(Ctx& ctx) override
            {
                PermanentCooldown::Allow(ctx.player, SPELL_FEIGN_DEATH);
            }

            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override { Sync(ctx); }

            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                uint32 const ms = FEIGN_COOLDOWN_MS[RankIndexOf(ctx.self)];
                return std::string("dead weight: ")
                     + (ms == 0 ? "Feign Death denied outright"
                                : "Feign Death on " + std::to_string(ms / 60000) + " min");
            }

        private:
            void Sync(Ctx& ctx)
            {
                // Rank III only. The lower ranks put their cooldown on at the
                // moment of the cast, which is where a cooldown belongs; rank
                // III is a standing denial and has to be re-asserted, because
                // the login path and anything that clears cooldowns would
                // otherwise hand the button back.
                if (FEIGN_COOLDOWN_MS[RankIndexOf(ctx.self)] == 0)
                    PermanentCooldown::Hold(ctx.player, SPELL_FEIGN_DEATH);
            }
        };

        void DeadWeight::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            Player* player = ctx.player;
            if (!player || !spell)
                return;

            SpellInfo const* info = spell->GetSpellInfo();
            if (!info)
                return;

            uint32 const base = sSpellMgr->GetFirstSpellInChain(info->Id);

            // The boon: Disengage comes back sooner. Applied by shortening the
            // cooldown the core has just set, which is the one moment it is
            // there to shorten.
            if (base == SPELL_DISENGAGE && ctx.self && ctx.self->boonMag != 0)
            {
                uint32 const now = player->GetSpellCooldownDelay(info->Id);
                if (now != 0)
                {
                    uint32 const cut = uint32(uint64(now) * ctx.self->boonMag / 100u);
                    player->ModifySpellCooldown(info->Id, -int32(cut));
                }
            }

            if (base != SPELL_FEIGN_DEATH)
                return;

            uint32 const ms = FEIGN_COOLDOWN_MS[RankIndexOf(ctx.self)];
            if (ms == 0)
                return;   // rank III: Sync holds it denied and the cast never lands

            player->AddSpellCooldown(info->Id, 0, ms, /*needSendToClient*/ true);

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r That was your last one for {} minutes.", ms / 60000);
        }

        std::string DeadWeight::Describe(AffixInstance const& self) const
        {
            uint32 const ms = FEIGN_COOLDOWN_MS[RankIndexOf(&self)];

            std::string out = ms == 0
                ? std::string("Feign Death does not answer at all. Disengage, traps and a pet that"
                              " holds aggro are the kite you have left.")
                : "Feign Death has a " + std::to_string(ms / 60000)
                  + " minute cooldown. The over-pull has to be fought now.";

            uint32 const pct = self.boonMag;
            if (pct != 0)
                out += " In exchange, Disengage comes back " + std::to_string(pct) + "% sooner.";

            return out;
        }

        // ==================================================================
        // C11 - Wide Dead Zone (38)
        //
        // "Ranged attacks cannot be used within ten yards."
        //
        // The anchor verb: TBC's dead zone, chosen rather than inflicted. The
        // card's own note is the best argument for it -- "melee hunters exist
        // and this affix creates them" -- and what it really restores is that
        // Wing Clip, Raptor Strike and Concussive Shot are buttons with a
        // purpose.
        // ==================================================================
        // The card's ladder, in yards.
        constexpr float DEAD_ZONE_YARDS[] = { 8.0f, 10.0f, 15.0f };
        static_assert(std::size(DEAD_ZONE_YARDS) >= MAX_RANK, "DEAD_ZONE_YARDS is short a rank");

        // The boon's threshold: beyond this, shots hit harder.
        constexpr float LONG_SHOT_YARDS = 20.0f;

        class WideDeadZone final : public IMechanic
        {
        public:
            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            // The boon, at the damage site so the distance is the one the shot
            // actually travelled.
            float DamageDoneMult(Ctx& ctx, Unit* victim, SpellInfo const* info) override
            {
                if (!ctx.player || !victim || !ctx.self || ctx.self->boonMag == 0)
                    return 1.0f;
                if (!info || !info->IsRangedWeaponSpell())          // SpellInfo.h:492
                    return 1.0f;
                if (ctx.player->GetExactDist2d(victim) < LONG_SHOT_YARDS)
                    return 1.0f;

                return 1.0f + float(ctx.self->boonMag) / 100.0f;
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                return "wide dead zone: " + std::to_string(uint32(DEAD_ZONE_YARDS[RankIndexOf(ctx.self)]))
                     + " yd, " + std::to_string(_refused) + " shot(s) refused";
            }

        private:
            uint32 _refused = 0;
        };

        void WideDeadZone::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            Player* player = ctx.player;
            if (!player || !spell)
                return;

            SpellInfo const* info = spell->GetSpellInfo();
            if (!info || !info->IsRangedWeaponSpell())
                return;

            Unit* victim = player->GetVictim();
            if (!victim)
                return;

            if (player->GetExactDist2d(victim) >= DEAD_ZONE_YARDS[RankIndexOf(ctx.self)])
                return;

            // InterruptNonMeleeSpells with the spell id named, so a melee swing
            // in the same instant is untouched (Unit.h:1597).
            player->InterruptNonMeleeSpells(false, info->Id);
            ++_refused;

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Too close to shoot. Back up, or draw your blade.");
        }

        std::string WideDeadZone::Describe(AffixInstance const& self) const
        {
            uint32 const yards = uint32(DEAD_ZONE_YARDS[RankIndexOf(&self)]);
            uint32 const pct   = self.boonMag;

            std::string out = "Ranged attacks are refused within " + std::to_string(yards)
                            + " yards. Wing Clip, Raptor Strike and your pet are what you have"
                              " inside that.";

            if (pct != 0)
                out += " In exchange, shots beyond " + std::to_string(uint32(LONG_SHOT_YARDS))
                     + " yards hit " + std::to_string(pct) + "% harder.";

            return out;
        }

        // ==================================================================
        // C12 - Blood Bond (39)
        //
        // "A fifth of the damage your pet takes is dealt to you."
        //
        // Wave B's hunter, and the counterpart to Half-Tamed: that one prices
        // neglecting the pet, this one prices hiding behind it. The card's
        // reading is that a voidwalker-style "let it tank everything" loop
        // becomes a shared health pool the hunter has to watch.
        // ==================================================================
        constexpr uint16 MECHANIC_BLOOD_BOND = 39;

        constexpr uint32 BOND_PCT[] = { 20, 30, 40 };

        static_assert(std::size(BOND_PCT) >= MAX_RANK, "BOND_PCT is short a rank");

        constexpr uint32 SPELL_MEND_PET = 136;

        class BloodBond final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override
            {
                if (ctx.player)
                    AddonFor(ctx)->QueueStat(ctx.player, KeyOf(MECHANIC_BLOOD_BOND, "c12_blood_bond"),
                                             int32(BOND_PCT[RankIndexOf(ctx.self)]));
            }

            void OnDetach(Ctx& ctx) override
            {
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, KeyOf(MECHANIC_BLOOD_BOND, "c12_blood_bond"), 0);
            }

            void OnPetDamaged(Ctx& ctx, Unit* /*attacker*/, uint32& damage) override
            {
                Player* player = ctx.player;
                if (!player || damage == 0)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;
                if (!player->IsAlive())
                    return;

                uint32 const share = uint32(uint64(damage) * BOND_PCT[RankIndexOf(ctx.self)] / 100u);
                if (share == 0)
                    return;

                // Floored at one health, like every other self-damage in this
                // module: an affix about watching a health bar must not be the
                // thing that empties it, and a pet dying to a pull the hunter
                // is running from should not take them with it.
                uint32 const health = uint32(player->GetHealth());
                uint32 const cost   = health > 1 ? std::min(share, health - 1) : 0;
                if (cost == 0)
                    return;

                bool* flag = ctx.run ? &ctx.run->selfDamage : nullptr;
                if (flag)
                    *flag = true;

                Unit::DealDamage(player, player, cost, nullptr, SELF_DAMAGE,
                                 SPELL_SCHOOL_MASK_NORMAL, nullptr, /*durabilityLoss*/ false);

                if (flag)
                    *flag = false;
            }

            // The boon: Mend Pet heals the hunter too. It is the button the
            // curse is trying to teach, which is the pattern wave A used for
            // Fear Ward and Raise Dead.
            //
            // It arrives through OnPeriodicTick rather than any heal hook,
            // because Mend Pet is a periodic heal *on the pet* -- nothing about
            // it ever touches the hunter, so there is no ModifyHealReceived on
            // them to intercept.
            void OnPeriodicTick(Ctx& ctx, Unit* victim, uint32& healing, SpellInfo const* info) override
            {
                // Mend Pet is a periodic heal on the pet, so its ticks arrive
                // here rather than through any heal hook on the hunter.
                Player* player = ctx.player;
                if (!player || !info || !ctx.self || ctx.self->boonMag == 0)
                    return;
                if (sSpellMgr->GetFirstSpellInChain(info->Id) != SPELL_MEND_PET)
                    return;
                if (!victim || victim == player)
                    return;

                uint32 const share = uint32(uint64(healing) * ctx.self->boonMag / 100u);
                if (share != 0)
                    player->ModifyHealth(int32(share));
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const pct  = BOND_PCT[RankIndexOf(&self)];
                uint32 const boon = self.boonMag;

                std::string out = std::to_string(pct) + "% of the damage your pet takes is dealt"
                                  " to you as well. It cannot kill you.";

                if (boon != 0)
                    out += " In exchange, Mend Pet heals you for " + std::to_string(boon)
                         + "% of what it heals your pet.";

                return out;
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                return "blood bond: " + std::to_string(BOND_PCT[RankIndexOf(ctx.self)])
                     + "% of pet damage shared";
            }
        };
    }

    GAUNTLET_MECHANIC(39, BloodBond);
    GAUNTLET_MECHANIC(36, HalfTamed);
    GAUNTLET_MECHANIC(37, DeadWeight);
    GAUNTLET_MECHANIC(38, WideDeadZone);
}
