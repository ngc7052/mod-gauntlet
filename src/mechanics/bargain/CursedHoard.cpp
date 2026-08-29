/*
 * mod-gauntlet - B2 Cursed Hoard: the chest is worth twice as much, and so are you
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletSummons.h"
#include "../Nearby.h"

#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "ObjectGuid.h"
#include "Player.h"

#include <string>
#include <iterator>

// Registry id 27. Design section 3, card B2: "Chests hold twice the loot, but
// opening one curses you: until you kill three enemies, any hit deals triple
// damage."
//
// The card names Dead Cells' cursed chest as its source and calls it "the best
// risk/reward loop in the genre", and the reason it works there is that the
// decision is made before anything happens: you can see the chest, you know
// what it costs, and walking past is a real option. Everything below exists to
// keep that property.
//
// Two deviations, both taken as decisions before the phase started; see
// docs/phase-3-prompt.md.
//
// Decision 2: the curse also lifts after a spell out of combat, on
// Gauntlet.Bargain.CursedHoard.EscapeSeconds (default 10). Said plainly,
// because it is the most consequential number in this file: at ten seconds the
// bargain is close to free -- open the chest, walk away, wait -- and what buys
// that is a hardcore realm where a genuine triple with no exit is a run ended
// by one unlucky pull. Setting the key to 0 restores the card exactly: three
// kills or nothing. The dial is there to be turned once it has been played.
//
// Decision 6: the tier window moved from the card's 4-14 to 6-14, because the
// design's own family-B header says bargains open at tier 6 and the generator
// has enforced that with BARGAIN_MIN_TIER since Phase 0. The row and the
// constant now agree, and a registry test keeps them agreeing.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_CURSED_HOARD = 27;

        // The card's ladder: 3 -> 4 -> 5 kills to lift it, and 6 at rank IV.
        // Gauntlet.Bargain.CursedHoard.EscapeSeconds is the other way out at
        // every rank, so the ladder lengthens the curse rather than sealing
        // it -- a bargain whose price cannot be paid stops being a bargain.
        constexpr uint32 KILLS_TO_LIFT[] = { 3, 4, 5, 6 };
        static_assert(std::size(KILLS_TO_LIFT) >= MAX_RANK, "KILLS_TO_LIFT is short a rank");

        // The card's one damage number, and it is real: see RelaxCaps.
        constexpr float CURSE_DAMAGE_MULT = 3.0f;

        constexpr uint32 DEFAULT_ESCAPE_SECONDS = 10;   // TODO(design)

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_CURSED_HOARD);
            return def ? def->key : "cursed_hoard";
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        std::string DebtKey() { return std::string(MechanicKey()) + ".debt"; }

        uint32 EscapeMs()
        {
            return sConfigMgr->GetOption<uint32>("Gauntlet.Bargain.CursedHoard.EscapeSeconds",
                                                 DEFAULT_ESCAPE_SECONDS) * 1000u;
        }

        class CursedHoard final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override;
            void OnDetach(Ctx& ctx) override;
            void OnTick(Ctx& ctx, uint32 diffMs) override;

            void OnEnterCombat(Ctx&, Unit*, bool) override
            {
                _calmMs = 0;
                if (_debt != 0)
                    _engaged = true;
            }

            void OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot) override;
            void OnLootGroupAmount(Ctx& ctx, uint32& groupAmount) override;

            void OnKill(Ctx& ctx, Creature* killed) override    { Paid(ctx, killed); }
            void OnPetKill(Ctx& ctx, Creature* killed) override { Paid(ctx, killed); }

            float DamageTakenMult(Ctx&, Unit* /*attacker*/, SpellInfo const*) override
            {
                return _debt != 0 ? CURSE_DAMAGE_MULT : 1.0f;
            }

            // Without this the card's triple is a double and the blurb lies.
            // Gauntlet.Caps.DamageTaken is 2.0, and the clamp is applied to the
            // product, so a x3 arriving alone still comes out at x2. The
            // ceiling is raised to exactly this affix's own number, only while
            // the curse is up, and the product is still clamped once -- so the
            // curse plus five Frenzy stacks plus a Champion reach 3.0 together
            // rather than each being paid out on top of it.
            void RelaxCaps(AffixInstance const& /*self*/, AggregateKind kind,
                           AggregateCaps& caps) const override
            {
                if (kind != AggregateKind::DamageTaken || _debt == 0)
                    return;

                if (caps.damageTakenMax < CURSE_DAMAGE_MULT)
                    caps.damageTakenMax = CURSE_DAMAGE_MULT;
            }

            std::string Describe(AffixInstance const& self) const override;
            std::string Diagnose(Ctx& ctx) const override;

        private:
            void Paid(Ctx& ctx, Creature* killed);
            void Lift(Ctx& ctx, char const* how);
            void Save(Ctx& ctx) const;
            void Publish(Ctx& ctx);

            uint32 _debt    = 0;   // kills still owed; 0 = no curse
            uint32 _calmMs  = 0;   // how long out of combat, while cursed
            uint32 _opened  = 0;
            bool   _engaged = false;   // has this curse been in a fight yet

            // Diagnostics only; see OnLoot.
            uint32 _lootWindows = 0;
            uint32 _goWindows   = 0;
        };

        void CursedHoard::OnAttach(Ctx& ctx)
        {
            if (ctx.state)
                _debt = uint32(std::max(0, ctx.state->Get(DebtKey())));

            _calmMs  = 0;
            _engaged = false;
            Publish(ctx);
        }

        void CursedHoard::OnDetach(Ctx& ctx)
        {
            // The debt stays written: swapping the affix out does not pay off
            // a curse the run already took on. What is cleared is the display.
            Save(ctx);

            if (ctx.addon && ctx.player)
            {
                ctx.addon->QueueCounter(ctx.player, MechanicKey(), 0, 0);
                AddonFor(ctx)->SendEvent(ctx.player, MechanicKey(), 0, "Cursed");
            }
        }

        void CursedHoard::OnTick(Ctx& ctx, uint32 diffMs)
        {
            if (_debt == 0)
                return;

            uint32 const escape = EscapeMs();
            if (escape == 0)
            {
                // The strict card: three kills or nothing.
                _calmMs = 0;
                return;
            }

            // Breaking away requires something to break away from.
            //
            // Without this the affix did not exist. A player clears a camp,
            // loots the chest standing safely out of combat -- which is the
            // normal way anyone opens one -- and the escape timer starts
            // immediately, so the curse lifted ten seconds later having never
            // multiplied a single blow. The user's report was "I just looted a
            // chest with Cursed Hoard and got no curse at all", and that is
            // exactly what it looked like from the outside.
            //
            // So the out-of-combat exit only opens once the curse has actually
            // been in a fight. That is what decision 2's escape was for: a
            // pull that goes wrong under a triple is a run ended by one
            // unlucky moment, and disengaging has to be an answer. Never
            // fighting at all is not disengaging.
            if (!_engaged)
            {
                _calmMs = 0;
                return;
            }

            Player* player = ctx.player;
            if (!player || player->IsInCombat() || !player->IsAlive())
            {
                _calmMs = 0;
                return;
            }

            _calmMs += diffMs;
            if (_calmMs < escape)
            {
                // The countdown is republished as it runs, because this is the
                // player's other way out and they cannot choose it without
                // knowing how much of it is left.
                AddonFor(ctx)->SendEvent(player, MechanicKey(),
                                         (escape - _calmMs + 999u) / 1000u, "Curse lifting");
                return;
            }

            Lift(ctx, "You broke away. The curse lifts.");
        }

        void CursedHoard::OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* /*loot*/)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            // Counted before any filtering, and reported by Diagnose(), because
            // "I looted a chest and got no curse" has two very different
            // causes -- the hook never reached this mechanic, or it reached it
            // and the guid was not a game object -- and no amount of reading
            // the code tells them apart. `.gauntlet debug dump` now does.
            ++_lootWindows;
            if (lootGuid.IsGameObject())
                ++_goWindows;

            // A game object and nothing else. OnLoot fires for every loot
            // window the player opens, corpses very much included, and a curse
            // that landed on every skinned wolf would be the affix nobody could
            // play around. ObjectGuid::IsGameObject is the whole test
            // ($CORE/src/server/game/Entities/Object/ObjectGuid.h:175).
            if (!lootGuid.IsGameObject())
                return;

            if (ctx.run && ctx.run->dead)
                return;

            ++_opened;

            // Opening a second chest while already cursed does not stack it;
            // it refreshes the debt to full. Stacking would make the affix a
            // death sentence for the one mistake it is meant to make survivable.
            _debt    = KILLS_TO_LIFT[RankIndex(ctx.self)];
            _calmMs  = 0;
            _engaged = false;
            Save(ctx);
            Publish(ctx);

            if (player->GetSession())
            {
                ChatHandler handler(player->GetSession());
                handler.PSendSysMessage(
                    "|cffff2020[Gauntlet]|r The hoard was cursed. Everything hits you three times"
                    " as hard until you have killed {} more.", _debt);
            }
        }

        // The other half of the card, and the reason anyone opens the chest at
        // all. GlobalScript::OnAfterCalculateLootGroupAmount is consulted once
        // per loot group per fill ($CORE/.../GlobalScript.h:66), so doubling
        // the group amount doubles the number of items rolled out of that
        // group -- which is the card's "chests hold twice the loot" expressed
        // where the core actually decides it.
        void CursedHoard::OnLootGroupAmount(Ctx& /*ctx*/, uint32& groupAmount)
        {
            if (groupAmount != 0)
                groupAmount *= 2;
        }

        void CursedHoard::Paid(Ctx& ctx, Creature* killed)
        {
            if (_debt == 0 || !killed)
                return;

            // Nothing this module summoned pays a debt this module set. Echo,
            // Reinforcements and Carrion's scavengers arrive in numbers and on
            // their own schedule, and letting them count would mean the curse
            // could be paid off by standing still.
            if (sGauntletSummons->IsGauntletSummon(killed))
                return;

            // An ordinary enemy, on the same test every other on-kill mechanic
            // in the module uses: no critters, no totems, no other scripts'
            // summons, nothing that was never a fight.
            if (!IsOrdinaryFoe(killed))
                return;

            --_debt;
            _calmMs = 0;
            Save(ctx);

            if (_debt == 0)
            {
                Lift(ctx, "The curse is paid off.");
                return;
            }

            Publish(ctx);
        }

        void CursedHoard::Lift(Ctx& ctx, char const* how)
        {
            _debt    = 0;
            _calmMs  = 0;
            _engaged = false;
            Save(ctx);
            Publish(ctx);

            AddonFor(ctx)->SendEvent(ctx.player, MechanicKey(), 0, "Cursed");

            if (ctx.player && ctx.player->GetSession())
                ChatHandler(ctx.player->GetSession()).PSendSysMessage(
                    "|cff20ff20[Gauntlet]|r {}", how);
        }

        void CursedHoard::Save(Ctx& ctx) const
        {
            if (ctx.state)
                ctx.state->Set(DebtKey(), int32(_debt));
        }

        void CursedHoard::Publish(Ctx& ctx)
        {
            if (!ctx.player)
                return;

            AddonFor(ctx)->QueueCounter(ctx.player, MechanicKey(), _debt,
                                        KILLS_TO_LIFT[RankIndex(ctx.self)]);
        }

        std::string CursedHoard::Describe(AffixInstance const& self) const
        {
            uint8 const  i     = RankIndex(&self);
            uint32 const kills = KILLS_TO_LIFT[i];
            uint32 const secs  = EscapeMs() / 1000u;

            std::string out = "Chests give twice as much loot. Opening one curses you: you take"
                              " triple damage until you kill " + std::to_string(kills)
                            + " enemies.";

            if (secs != 0)
                out += " Once the curse has been in a fight, staying out of combat for "
                     + std::to_string(secs) + " seconds also ends it.";

            out += " Open a chest at full health with easy kills nearby, or leave it.";

            // No BoonClause: the boon is the doubled hoard, which the first
            // clause already promises, and Boon::BonusMoney here is the row's
            // way of saying "this pays out" rather than a second upside.
            return out;
        }

        std::string CursedHoard::Diagnose(Ctx& ctx) const
        {
            std::string out = "cursed hoard: ";
            out += _debt != 0 ? ("CURSED, " + std::to_string(_debt) + " kill(s) owed, "
                                 + (_engaged ? "engaged, calm " + std::to_string(_calmMs / 1000u) + "s"
                                             : std::string("not yet in combat, no escape open"))
                                )
                              : std::string("clean");
            out += ", " + std::to_string(_opened) + " chest(s) opened this session ("
                 + std::to_string(_lootWindows) + " loot window(s) seen, "
                 + std::to_string(_goWindows) + " of them game objects)";
            (void)ctx;
            return out;
        }
    }

    GAUNTLET_MECHANIC(27, CursedHoard);
}
