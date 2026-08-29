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
#include "SpellMgr.h"

#include <string>

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
        constexpr uint32 PACT_KILLS[MAX_RANK] = { 20, 15, 10 };

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

            std::string Diagnose(Ctx& ctx) const override
            {
                return "fel pact: " + std::to_string(_kills) + "/"
                     + std::to_string(PACT_KILLS[RankIndexOf(ctx.self)]) + " kills, grace "
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
                                                _kills, PACT_KILLS[RankIndexOf(ctx.self)]);
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

            if (_kills < PACT_KILLS[RankIndexOf(ctx.self)])
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
            uint32 const kills = PACT_KILLS[RankIndexOf(&self)];

            std::string out = "Every kill your demon makes frays its binding. After "
                            + std::to_string(kills) + " it breaks free and attacks you for 15"
                              " seconds. Re-summoning your demon costs a shard and resets the"
                              " count.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(60, FelPact);
}
