/*
 * mod-gauntlet - the death knight's two: Rune-starved, Grave Call
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletSummons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Player.h"
#include "Position.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <string>
#include <vector>

// Design section 3, family C, death knight. Engine: runes and runic power,
// diseases, Death Strike, the ghoul. The classic death: every rune spent with
// nothing left when the second elite arrives -- which is exactly what C21
// prices, and C22 is the class fantasy inverted: the dead are yours only if you
// take them.

namespace Gauntlet
{
    namespace
    {
        constexpr uint32 SPELL_RAISE_DEAD       = 46584;   // the registry's requiresSpell for C22
        constexpr uint32 SPELL_CORPSE_EXPLOSION = 49158;
        constexpr uint32 SPELL_DEATH_PACT       = 48743;
        constexpr uint32 SPELL_ARMY_OF_THE_DEAD = 42650;

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
        // C21 - Rune-starved (48)
        //
        // "While all six runes are on cooldown you take 30% more damage."
        //
        // The threshold verb, on the one resource the class watches every
        // second. What it punishes is the "press everything on cooldown"
        // rotation; what it rewards is keeping one rune, which turns Blood Tap
        // and Empower Rune Weapon into defensive tools.
        // ==================================================================
        constexpr uint16 MECHANIC_RUNE_STARVED = 48;

        constexpr float STARVED_MULT[MAX_RANK] = { 1.20f, 1.30f, 1.40f };

        bool AllRunesSpent(Player* player)
        {
            if (!player || player->getClass() != CLASS_DEATH_KNIGHT)
                return false;

            for (uint8 i = 0; i < MAX_RUNES; ++i)                 // Player.h:385
                if (player->GetRuneCooldown(i) == 0)              // Player.h:2561
                    return false;

            return true;
        }

        class RuneStarved final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, KeyOf(MECHANIC_RUNE_STARVED, "c21_rune_starved"), 0);
            }

            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override
            {
                Player* player = ctx.player;
                if (!player)
                    return;

                bool const starved = AllRunesSpent(player);
                if (starved == _starved && _published)
                    return;

                _starved   = starved;
                _published = true;

                // The card asks for this by name -- "the addon shows the state"
                // -- and it is the whole of the counterplay: a death knight who
                // can see the light is on will keep a rune back.
                AddonFor(ctx)->QueueStat(player, KeyOf(MECHANIC_RUNE_STARVED, "c21_rune_starved"),
                                         starved ? 1 : 0);
            }

            float DamageTakenMult(Ctx& ctx, Unit*, SpellInfo const*) override
            {
                return AllRunesSpent(ctx.player) ? STARVED_MULT[RankIndexOf(ctx.self)] : 1.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const extra = uint32((STARVED_MULT[RankIndexOf(&self)] - 1.0f) * 100.0f + 0.5f);

                // The boon is Boon::BonusRegen and the card spends it on runic
                // power decaying more slowly. That decay is entirely inside the
                // core's own rune tick with no hook on it, so the sentence
                // below deliberately does not promise it -- see the report.
                return "While all six of your runes are on cooldown you take " + std::to_string(extra)
                     + "% more damage. Keep one back.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                return std::string("rune-starved: ") + (AllRunesSpent(ctx.player) ? "ALL SPENT" : "at least one up");
            }

        private:
            bool _starved   = false;
            bool _published = false;
        };

        // ==================================================================
        // C22 - Grave Call (49)
        //
        // "The dead you do not claim rise against you."
        //
        // The companion verb, inverted. A kill is not finished until the corpse
        // is used, so every corpse-consuming ability becomes a rotation piece
        // -- or you walk away, because the risen leashes like any mob.
        // ==================================================================
        constexpr uint16 MECHANIC_GRAVE_CALL = 49;

        // The card's ladder, and it runs the interesting way: the window to
        // claim a corpse shrinks as the affix worsens.
        constexpr uint32 CLAIM_MS[MAX_RANK] = { 8000, 5000, 3000 };

        // Rank III's risen are not weak. The first two ranks are, and the
        // mechanic does that at runtime rather than with a second template.
        constexpr float WEAK_HEALTH_PCT[MAX_RANK] = { 0.5f, 0.5f, 1.0f };

        constexpr uint32 RISEN_LIFETIME_MS = 120000;   // TODO(design)

        // Two at a time. A death knight clearing a camp leaves corpses faster
        // than anyone can claim them, and the affix is meant to make the
        // economy tight rather than to bury the player.
        constexpr size_t MAX_PENDING = 2;              // TODO(design)

        bool ClaimsCorpses(uint32 spellId)
        {
            uint32 const base = sSpellMgr->GetFirstSpellInChain(spellId);
            return base == SPELL_RAISE_DEAD
                || base == SPELL_CORPSE_EXPLOSION
                || base == SPELL_DEATH_PACT
                || base == SPELL_ARMY_OF_THE_DEAD;
        }

        class GraveCall final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                _pending.clear();
                if (ctx.player)
                    sGauntletSummons->DespawnFor(ctx.player, MECHANIC_GRAVE_CALL);
            }

            void OnKill(Ctx& ctx, Creature* killed) override    { Note(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Note(ctx, killed); }

            void OnTick(Ctx& ctx, uint32 diffMs) override;

            void OnSpellCast(Ctx& ctx, Spell* spell) override;

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx&) const override
            {
                return "grave call: " + std::to_string(_pending.size()) + " corpse(s) counting down, "
                     + std::to_string(_claimed) + " claimed, " + std::to_string(_risen) + " risen";
            }

        private:
            struct Corpse
            {
                Position at;
                uint32   leftMs = 0;
            };

            void Note(Ctx& ctx, Creature* killed);
            void Rise(Ctx& ctx, Corpse const& corpse);

            std::vector<Corpse> _pending;
            uint32              _claimed = 0;
            uint32              _risen   = 0;
        };

        void GraveCall::Note(Ctx& ctx, Creature* killed)
        {
            if (!ctx.player || !killed)
                return;
            if (ctx.run && ctx.run->dead)
                return;

            // Nothing this module raised may raise again, and nothing that was
            // never a fight counts as a corpse.
            if (sGauntletSummons->IsGauntletSummon(killed) || !IsOrdinaryFoe(killed))
                return;

            if (_pending.size() >= MAX_PENDING)
                return;

            Corpse corpse;
            corpse.at     = killed->GetPosition();
            corpse.leftMs = CLAIM_MS[RankIndexOf(ctx.self)];
            _pending.push_back(corpse);

            AddonFor(ctx)->SendEvent(ctx.player, KeyOf(MECHANIC_GRAVE_CALL, "c22_grave_call"),
                                     corpse.leftMs / 1000u, "Claim the dead");
        }

        void GraveCall::OnTick(Ctx& ctx, uint32 diffMs)
        {
            if (_pending.empty())
                return;

            for (size_t i = 0; i < _pending.size(); )
            {
                if (_pending[i].leftMs > diffMs)
                {
                    _pending[i].leftMs -= diffMs;
                    ++i;
                    continue;
                }

                Corpse const corpse = _pending[i];
                _pending.erase(_pending.begin() + static_cast<ptrdiff_t>(i));
                Rise(ctx, corpse);
            }
        }

        void GraveCall::Rise(Ctx& ctx, Corpse const& corpse)
        {
            Player* player = ctx.player;
            if (!player || !player->IsInWorld() || !player->IsAlive())
                return;
            if (ctx.run && ctx.run->dead)
                return;

            Creature* risen = sGauntletSummons->Summon(player, ENTRY_RISEN, corpse.at,
                                                        RISEN_LIFETIME_MS,
                                                        /*countsAsStalker*/ false,
                                                        MECHANIC_GRAVE_CALL);
            if (!risen)
                return;   // the cap refused; a corpse that does not rise is not a fault

            float const share = WEAK_HEALTH_PCT[RankIndexOf(ctx.self)];
            if (share < 1.0f)
            {
                uint32 const pool = uint32(float(risen->GetMaxHealth()) * share);
                risen->SetMaxHealth(pool != 0 ? pool : 1);
                risen->SetHealth(risen->GetMaxHealth());
            }

            if (CreatureAI* ai = risen->AI())
                ai->AttackStart(player);

            risen->AddThreat(player, 1.0f);
            ++_risen;

            AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_GRAVE_CALL, "c22_grave_call"), 0,
                                     "Claim the dead");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r One you did not claim has risen.");
        }

        void GraveCall::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            Player* player = ctx.player;
            if (!player || !spell)
                return;

            SpellInfo const* info = spell->GetSpellInfo();
            if (!info)
                return;

            uint32 const base = sSpellMgr->GetFirstSpellInChain(info->Id);

            // The boon: Raise Dead comes back sooner. It is also the readiest
            // way to claim a corpse, so the boon is again an argument for the
            // counterplay rather than a consolation for the curse.
            if (base == SPELL_RAISE_DEAD && ctx.self && ctx.self->boonMag != 0)
            {
                uint32 const now = player->GetSpellCooldownDelay(info->Id);
                if (now != 0)
                    player->ModifySpellCooldown(info->Id,
                                                -int32(uint64(now) * ctx.self->boonMag / 100u));
            }

            if (!ClaimsCorpses(info->Id) || _pending.empty())
                return;

            // One cast claims one corpse: the oldest, which is the one about to
            // rise. Army of the Dead claiming the whole field would make the
            // affix free on the one cooldown a death knight already presses.
            _pending.erase(_pending.begin());
            ++_claimed;

            if (_pending.empty())
                AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_GRAVE_CALL, "c22_grave_call"), 0,
                                         "Claim the dead");
        }

        std::string GraveCall::Describe(AffixInstance const& self) const
        {
            uint8 const  i    = RankIndexOf(&self);
            uint32 const secs = CLAIM_MS[i] / 1000u;
            bool const   weak = WEAK_HEALTH_PCT[i] < 1.0f;
            uint32 const pct  = self.boonMag;

            std::string out = "Anything you kill rises against you after " + std::to_string(secs)
                            + " seconds unless you claim the corpse first -- Raise Dead, Corpse"
                              " Explosion, Death Pact or Army of the Dead. What rises is "
                            + (weak ? "weak." : "as strong as what died.");

            if (pct != 0)
                out += " In exchange, Raise Dead comes back " + std::to_string(pct) + "% sooner.";

            return out;
        }
    }

    GAUNTLET_MECHANIC(48, RuneStarved);
    GAUNTLET_MECHANIC(49, GraveCall);
}
