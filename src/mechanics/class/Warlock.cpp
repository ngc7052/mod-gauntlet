/*
 * mod-gauntlet - the warlock's one: Fel Pact
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletSummons.h"
#include "../Boons.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Pet.h"
#include "Player.h"
#include "Position.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
#include "Unit.h"

#include <algorithm>
#include <string>
#include <iterator>

// Design section 3, family C, warlock. The engine is the demon and the shard
// economy; Fel Pact makes the second pay for the first, which is the resource
// decision the class is built around made visible.

namespace Gauntlet
{
    namespace
    {
        // The summon spells that re-bind a demon. Casting any of them costs a
        // shard and resets the count, which is the card's counterplay.
        constexpr uint32 SPELL_SUMMON_IMP        = 688;
        constexpr uint32 SPELL_SUMMON_VOIDWALKER = 697;
        constexpr uint32 SPELL_SUMMON_SUCCUBUS   = 712;
        constexpr uint32 SPELL_SUMMON_FELHUNTER  = 691;
        constexpr uint32 SPELL_SUMMON_FELGUARD   = 30146;


        char const* KeyOf(uint16 id, char const* fallback)
        {
            MechanicDef const* def = FindMechanic(id);
            return def ? def->key : fallback;
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        // ==================================================================
        // C33 - Fel Pact (60)
        //
        // "Your demon's binding frays with every kill it makes. After twenty
        // it breaks free and turns on you, unless you re-bind it first."
        //
        // The companion verb, and the card's own reading of it: a shard versus
        // a fight, every twenty kills. The counter is the affix -- it is
        // visible, it only moves when the demon kills, and the player decides
        // when to pay.
        // ==================================================================
        constexpr uint16 MECHANIC_FEL_PACT = 60;

        // The card's ladder: 20 -> 15 -> 10 kills before the binding goes.
        constexpr uint32 PACT_KILLS = 20;

        constexpr uint32 HOSTILE_MS = 15000;

        // Not on the card, and the same reason Half-Tamed has one: a warlock
        // who re-summons straight into a broken pact should not be attacked
        // twice in the same breath.
        constexpr uint32 REGRIP_MS = 30000;   // TODO(design)

        bool IsSummonDemon(uint32 spellId)
        {
            uint32 const base = sSpellMgr->GetFirstSpellInChain(spellId);
            return base == SPELL_SUMMON_IMP
                || base == SPELL_SUMMON_VOIDWALKER
                || base == SPELL_SUMMON_SUCCUBUS
                || base == SPELL_SUMMON_FELHUNTER
                || base == SPELL_SUMMON_FELGUARD;
        }

        class FelPact final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override
            {
                // The count is the run's, not the session's: a warlock who logs
                // out at nineteen comes back at nineteen.
                if (ctx.state)
                    _kills = uint32(std::max(0, ctx.state->Get(StateKey())));
                Publish(ctx);
            }

            void OnDetach(Ctx& ctx) override
            {
                Save(ctx);
                if (ctx.player)
                    sGauntletSummons->DespawnFor(ctx.player, MECHANIC_FEL_PACT);
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueCounter(ctx.player, KeyOf(MECHANIC_FEL_PACT, "c33_fel_pact"), 0, 0);
            }

            void OnPetKill(Ctx& ctx, Creature* killed) override;
            void OnTick(Ctx& ctx, uint32 diffMs) override;
            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            // Boon::BonusPetDamage, paid unconditionally: the card's boon is
            // "the demon deals +10% damage" with no condition on it, unlike
            // Half-Tamed's, which is gated on the pet being happy.
            void OnPetDamage(Ctx& ctx, Unit* /*victim*/, uint32& damage) override
            {
                if (!ctx.self || ctx.self->boonMag == 0)
                    return;

                damage += uint32(uint64(damage) * ctx.self->boonMag / 100u);
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& /*ctx*/) const override
            {
                return "fel pact: " + std::to_string(_kills) + "/"
                     + std::to_string(PACT_KILLS) + " kills, grace "
                     + std::to_string(_graceMs / 1000u) + "s";
            }

        private:
            static std::string StateKey() { return "fel_pact.kills"; }

            void Save(Ctx& ctx) const
            {
                if (ctx.state)
                    ctx.state->Set(StateKey(), int32(_kills));
            }

            void Publish(Ctx& ctx)
            {
                if (ctx.player)
                    AddonFor(ctx)->QueueCounter(ctx.player, KeyOf(MECHANIC_FEL_PACT, "c33_fel_pact"),
                                                _kills, PACT_KILLS);
            }

            void Break(Ctx& ctx, Pet* pet);

            uint32 _kills   = 0;
            uint32 _graceMs = 0;
            uint32 _broke   = 0;
        };

        void FelPact::OnPetKill(Ctx& ctx, Creature* killed)
        {
            if (!ctx.player || !killed)
                return;
            if (ctx.run && ctx.run->dead)
                return;

            // Nothing this module summoned frays a binding this module set.
            if (sGauntletSummons->IsGauntletSummon(killed))
                return;

            ++_kills;
            Save(ctx);
            Publish(ctx);
        }

        void FelPact::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            if (_graceMs != 0)
            {
                _graceMs = _graceMs > diffMs ? _graceMs - diffMs : 0;
                return;
            }

            if (_kills < PACT_KILLS)
                return;
            if (ctx.run && ctx.run->dead)
                return;
            if (!player->IsInWorld() || !player->IsAlive())
                return;

            Pet* pet = player->GetPet();
            if (!pet || !pet->IsAlive())
                return;   // nothing bound; the count waits

            Break(ctx, pet);
        }

        void FelPact::Break(Ctx& ctx, Pet* pet)
        {
            Player* player = ctx.player;

            uint32 const   entry = pet->GetEntry();
            Position const at    = pet->GetPosition();

            player->RemovePet(pet, PET_SAVE_NOT_IN_SLOT);

            _kills   = 0;
            _graceMs = REGRIP_MS;
            ++_broke;
            Save(ctx);
            Publish(ctx);

            Creature* freed = sGauntletSummons->Summon(player, entry, at, HOSTILE_MS,
                                                        /*countsAsStalker*/ false, MECHANIC_FEL_PACT);
            if (freed)
            {
                if (CreatureAI* ai = freed->AI())
                    ai->AttackStart(player);
                freed->AddThreat(player, 1.0f);
            }

            AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_FEL_PACT, "c33_fel_pact"),
                                     HOSTILE_MS / 1000u, "Fel Pact");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r The binding breaks. It was never loyal.");
        }

        void FelPact::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            if (!spell || _kills == 0)
                return;

            SpellInfo const* info = spell->GetSpellInfo();
            if (!info || !IsSummonDemon(info->Id))
                return;

            // Re-binding resets the count. The shard is the price and the core
            // has already taken it; this affix does not take a second one.
            _kills = 0;
            Save(ctx);
            Publish(ctx);

            if (ctx.player && ctx.player->GetSession())
                ChatHandler(ctx.player->GetSession()).PSendSysMessage(
                    "|cff20ff20[Gauntlet]|r The binding is fresh again.");
        }

        std::string FelPact::Describe(AffixInstance const& self) const
        {
            uint32 const kills = PACT_KILLS;

            std::string out = "Every kill your demon makes frays its binding. After "
                            + std::to_string(kills) + " it breaks free and attacks you for 15"
                              " seconds. Re-summoning your demon costs a shard and resets the"
                              " count.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }

        // ==================================================================
        // C34 - Affliction of the Self (61)
        //
        // "Your curses and corruption afflict you too, at a fifth of their
        // strength."
        //
        // Multi-DoTting a camp is the greedy play and bleeds accordingly. The
        // antidote is already in the kit -- Drain Life -- and Shadow Bolt and
        // the demon stay free, so the number of targets you dot becomes a
        // health decision rather than a reflex.
        // ==================================================================
        constexpr uint32 AFFLICTION_PCT = 20;

        class AfflictionOfTheSelf final : public IMechanic
        {
        public:
            void OnPeriodicTick(Ctx& ctx, Unit* victim, uint32& damage,
                                SpellInfo const* info) override
            {
                Player* player = ctx.player;
                if (!player || !info || damage == 0)
                    return;
                if (!victim || victim == player)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                // The warlock's own afflictions, and nothing else on the
                // target. SpellFamilyName separates them from a bleed, a trap,
                // or another caster's DoT on the same mob.
                if (info->SpellFamilyName != SPELLFAMILY_WARLOCK)
                    return;

                uint32 const share = uint32(uint64(damage)
                                          * AFFLICTION_PCT / 100u);
                if (share == 0)
                    return;

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

                _bled += cost;
            }

            float DamageDoneMult(Ctx& ctx, Unit*, SpellInfo const* info) override
            {
                if (!info || !ctx.self || ctx.self->boonMag == 0)
                    return 1.0f;
                if (info->SpellFamilyName != SPELLFAMILY_WARLOCK)
                    return 1.0f;

                return 1.0f + float(ctx.self->boonMag) / 100.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const pct  = AFFLICTION_PCT;
                uint32 const boon = self.boonMag;

                std::string out = std::to_string(pct) + "% of what your curses and corruption"
                                  " deal is dealt to you as well. It cannot kill you.";

                if (boon != 0)
                    out += " In exchange they deal " + std::to_string(boon) + "% more.";

                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return "affliction of the self: " + std::to_string(_bled) + " health bled";
            }

        private:
            uint32 _bled = 0;
        };

        // ==================================================================
        // C35 - Shard Economy (62)
        //
        // "Every summon and every Healthstone costs a Soul Shard, and shards
        // drop only from enemies of your level or higher."
        //
        // Shards become lives. Hunting higher-level mobs for them is a risk the
        // class chooses rather than one imposed on it, which is the shape the
        // family is after.
        //
        // Only the second half is implemented, and this file says so rather
        // than pretending. The first -- a shard consumed by summons and
        // Healthstones -- would need Player::DestroyItemCount on item 6265 at
        // two hooks, and the summon half is already Fel Pact's territory in a
        // way that would double-charge a warlock carrying both. TODO(design)
        // ==================================================================
        // Rank IV wants an enemy two levels above you before a shard drops, which
        // is the same instruction Hubris gives from the other side: fight up.
        constexpr int32 SHARD_LEVEL_DELTA = 0;

        constexpr uint32 ITEM_SOUL_SHARD  = 6265;

        class ShardEconomy final : public IMechanic
        {
        public:
            void OnKill(Ctx& ctx, Creature* killed) override
            {
                Player* player = ctx.player;
                if (!player || !killed)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;
                if (sGauntletSummons->IsGauntletSummon(killed))
                    return;

                int32 const wanted = int32(player->GetLevel()) + SHARD_LEVEL_DELTA;
                if (int32(killed->GetLevel()) >= wanted)
                {
                    // At or above the line: the boon doubles what Drain Soul
                    // gave, which the core has already handed over.
                    if (ctx.self && ctx.self->boonMag != 0)
                    {
                        player->AddItem(ITEM_SOUL_SHARD, 1);
                        ++_doubled;
                    }
                    return;
                }

                // Below the line the shard is taken back. Removing one is the
                // only way to express "does not drop" from here: the core has
                // already created it by the time any kill hook runs.
                if (player->HasItemCount(ITEM_SOUL_SHARD, 1))
                {
                    player->DestroyItemCount(ITEM_SOUL_SHARD, 1, true);
                    ++_refused;
                }
            }

            std::string Describe(AffixInstance const& self) const override
            {
                int32 const  delta = SHARD_LEVEL_DELTA;
                std::string  line  = delta < 0
                    ? "no more than " + std::to_string(-delta) + " levels below you"
                    : (delta == 0 ? std::string("at your level or above")
                                  : "at least " + std::to_string(delta) + " level above you");

                std::string out = "Soul Shards come only from enemies " + line + ".";

                if (self.boonMag != 0)
                    out += " In exchange those enemies give two.";

                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return "shard economy: " + std::to_string(_refused) + " refused, "
                     + std::to_string(_doubled) + " doubled";
            }

        private:
            uint32 _refused = 0;
            uint32 _doubled = 0;
        };

        // ==================================================================
        // C36 - Shared Blood (63)
        //
        // "While your demon lives you take 25% more damage, and it deals 40%
        // more."
        //
        // The demon stops being free. Dismissing it is a real option and the
        // card means it to be: a warlock who cannot afford the damage fights
        // without one, which is a style rather than a failure.
        // ==================================================================
        constexpr float SHARED_TAKEN = 1.25f;

        class SharedBlood final : public IMechanic
        {
        public:
            float DamageTakenMult(Ctx& ctx, Unit*, SpellInfo const*) override
            {
                Player* player = ctx.player;
                if (!player || !player->GetPet())
                    return 1.0f;

                return SHARED_TAKEN;
            }

            void OnPetDamage(Ctx& ctx, Unit* /*victim*/, uint32& damage) override
            {
                if (!ctx.self || ctx.self->boonMag == 0)
                    return;

                damage += uint32(uint64(damage) * ctx.self->boonMag / 100u);
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const extra = uint32((SHARED_TAKEN - 1.0f) * 100.0f + 0.5f);
                uint32 const boon  = self.boonMag;

                std::string out = "While your demon is out you take " + std::to_string(extra)
                                + "% more damage.";

                if (boon != 0)
                    out += " In exchange it deals " + std::to_string(boon) + "% more.";

                out += " Dismissing it is always an option.";
                return out;
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                return std::string("shared blood: ")
                     + (ctx.player && ctx.player->GetPet() ? "demon out, paying" : "no demon");
            }
        };
    }

    GAUNTLET_MECHANIC(61, AfflictionOfTheSelf);
    GAUNTLET_MECHANIC(62, ShardEconomy);
    GAUNTLET_MECHANIC(63, SharedBlood);
    GAUNTLET_MECHANIC(60, FelPact);
}
