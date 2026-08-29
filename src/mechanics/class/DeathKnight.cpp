/*
 * mod-gauntlet - the death knight's two: Rune-starved, Grave Call
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletSummons.h"
#include "../Nearby.h"
#include "AuraDurationEdit.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Player.h"
#include "Position.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellAuras.h"
#include "SpellMgr.h"

#include <array>
#include <string>
#include <vector>
#include <iterator>

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

        constexpr float STARVED_MULT[] = { 1.20f, 1.30f, 1.40f, 1.55f };

        static_assert(std::size(STARVED_MULT) >= MAX_RANK, "STARVED_MULT is short a rank");

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
        constexpr uint32 CLAIM_MS[] = { 8000, 5000, 3000, 2000 };
        static_assert(std::size(CLAIM_MS) >= MAX_RANK, "CLAIM_MS is short a rank");

        // Rank III's risen are not weak. The first two ranks are, and the
        // mechanic does that at runtime rather than with a second template.
        // Full health from rank III on. Rank IV escalates on the claim window
        // instead -- two seconds to touch a corpse -- because a risen stronger
        // than the thing that died would stop being a corpse standing up.
        constexpr float WEAK_HEALTH_PCT[] = { 0.5f, 0.5f, 1.0f, 1.0f };
        static_assert(std::size(WEAK_HEALTH_PCT) >= MAX_RANK, "WEAK_HEALTH_PCT is short a rank");

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

        // ==================================================================
        // C23 - Cold Presence (50)
        //
        // "Changing presence costs all your runic power and has a ten-second
        // cooldown."
        //
        // The identity verb. Blood for the elite, Frost to hold, Unholy to
        // travel -- chosen before the fight rather than during it, and the
        // Rune Strike and Death Coil dumps happen *before* the switch.
        // ==================================================================
        constexpr uint16 MECHANIC_COLD_PRESENCE = 50;

        constexpr uint32 SPELL_BLOOD_PRESENCE  = 48266;
        constexpr uint32 SPELL_FROST_PRESENCE  = 48263;
        constexpr uint32 SPELL_UNHOLY_PRESENCE = 48265;

        constexpr std::array<uint32, 3> PRESENCES = { {
            SPELL_BLOOD_PRESENCE, SPELL_FROST_PRESENCE, SPELL_UNHOLY_PRESENCE
        } };

        constexpr uint32 PRESENCE_LOCK_MS[] = { 6000, 10000, 20000, 30000 };

        static_assert(std::size(PRESENCE_LOCK_MS) >= MAX_RANK, "PRESENCE_LOCK_MS is short a rank");

        bool IsPresence(uint32 spellId)
        {
            for (uint32 id : PRESENCES)
                if (id == spellId)
                    return true;
            return false;
        }

        class ColdPresence final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                if (Player* player = ctx.player)
                    for (uint32 id : PRESENCES)
                        player->RemoveSpellCooldown(id, /*update*/ true);
            }

            void OnSpellCast(Ctx& ctx, Spell* spell) override
            {
                Player* player = ctx.player;
                if (!player || !spell)
                    return;

                SpellInfo const* info = spell->GetSpellInfo();
                if (!info || !IsPresence(info->Id))
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                player->SetPower(POWER_RUNIC_POWER, 0);

                uint32 const ms = PRESENCE_LOCK_MS[RankIndexOf(ctx.self)];
                for (uint32 id : PRESENCES)
                    if (id != info->Id)
                        player->AddSpellCooldown(id, 0, ms, /*needSendToClient*/ true);

                AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_COLD_PRESENCE, "c23_cold_presence"),
                                         ms / 1000u, "Presence locked");

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r The change costs everything you had stored.");
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const secs = PRESENCE_LOCK_MS[RankIndexOf(&self)] / 1000u;

                // The card's boon is "+25% presence effects", which lives
                // inside each presence's own aura and has no seam this module
                // can reach; it is not promised here. See the report.
                return "Changing presence empties your runic power and locks the other two for "
                     + std::to_string(secs) + " seconds. Dump before you switch.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                return "cold presence: " + std::to_string(PRESENCE_LOCK_MS[RankIndexOf(ctx.self)] / 1000u)
                     + "s lock";
            }
        };

        // ==================================================================
        // C24 - One Ward (51)
        //
        // "Anti-Magic Shell and Icebound Fortitude share a cooldown."
        //
        // Read the fight: a caster pack is a Shell fight, a melee elite is a
        // Fortitude fight, and you no longer get to be wrong about which.
        // ==================================================================
        constexpr uint16 MECHANIC_ONE_WARD = 51;

        constexpr uint32 SPELL_ANTI_MAGIC_SHELL    = 48707;
        constexpr uint32 SPELL_ICEBOUND_FORTITUDE  = 48792;
        constexpr uint32 SPELL_LICHBORNE           = 49039;

        // The card's shared cooldown: the longer of the two.
        constexpr uint32 WARD_SHARED_MS = 120000;

        class OneWard final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                if (Player* player = ctx.player)
                    for (uint32 id : { SPELL_ANTI_MAGIC_SHELL, SPELL_ICEBOUND_FORTITUDE, SPELL_LICHBORNE })
                        player->RemoveSpellCooldown(id, /*update*/ true);
            }

            void OnSpellCast(Ctx& ctx, Spell* spell) override
            {
                Player* player = ctx.player;
                if (!player || !spell)
                    return;

                SpellInfo const* info = spell->GetSpellInfo();
                if (!info)
                    return;

                bool const rankThree = RankIndexOf(ctx.self) >= 2;
                bool const isWard = info->Id == SPELL_ANTI_MAGIC_SHELL
                                 || info->Id == SPELL_ICEBOUND_FORTITUDE
                                 || (rankThree && info->Id == SPELL_LICHBORNE);
                if (!isWard)
                    return;
                if (ctx.run && ctx.run->dead)
                    return;

                for (uint32 id : { SPELL_ANTI_MAGIC_SHELL, SPELL_ICEBOUND_FORTITUDE, SPELL_LICHBORNE })
                {
                    if (id == info->Id)
                        continue;
                    if (id == SPELL_LICHBORNE && !rankThree)
                        continue;

                    player->AddSpellCooldown(id, 0, WARD_SHARED_MS, /*needSendToClient*/ true);
                }

                AddonFor(ctx)->SendEvent(player, KeyOf(MECHANIC_ONE_WARD, "c24_one_ward"),
                                         WARD_SHARED_MS / 1000u, "One Ward");
            }

            // The boon: whichever ward was used lasts longer.
            void OnAuraApplied(Ctx& ctx, Unit* target, Aura* aura) override
            {
                Player* player = ctx.player;
                if (!player || !aura || target != player || !ctx.self || ctx.self->boonMag == 0)
                    return;

                SpellInfo const* info = aura->GetSpellInfo();
                if (!info)
                    return;
                if (info->Id != SPELL_ANTI_MAGIC_SHELL && info->Id != SPELL_ICEBOUND_FORTITUDE
                    && info->Id != SPELL_LICHBORNE)
                    return;

                AuraDurationEdit::Scale(aura, 1.0f + float(ctx.self->boonMag) / 100.0f);
            }

            std::string Describe(AffixInstance const& self) const override
            {
                bool const three = RankIndexOf(&self) >= 2;
                uint32 const pct = self.boonMag;

                std::string out = "Anti-Magic Shell and Icebound Fortitude share a two-minute"
                                  " cooldown";
                out += three ? ", and so does Lichborne." : ".";
                out += " Read the fight before you spend one.";

                if (pct != 0)
                    out += " In exchange whichever you use lasts " + std::to_string(pct)
                         + "% longer, whatever its tooltip says.";

                return out;
            }

            std::string Diagnose(Ctx&) const override
            {
                return "one ward: two-minute shared cooldown";
            }
        };
    }

    GAUNTLET_MECHANIC(50, ColdPresence);
    GAUNTLET_MECHANIC(51, OneWard);
    GAUNTLET_MECHANIC(48, RuneStarved);
    GAUNTLET_MECHANIC(49, GraveCall);
}
