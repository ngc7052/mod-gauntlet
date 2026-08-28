/*
 * mod-gauntlet - R2 Lone Wolf: the road is easier alone, and you are smaller on it
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "../Boons.h"

#include "Chat.h"
#include "Group.h"
#include "Player.h"

#include <string>

// Registry id 24. Design section 3, card R2: "You cannot join a group. You
// gain 20% more experience."
//
// **This mechanic deliberately does not implement its card**, and the
// deviation was taken as a decision before the phase started; see
// docs/phase-3-prompt.md, decision 1.
//
// What it does instead: you may group. While you are in one your maximum
// health is halved. While you are alone you gain the card's 20% experience.
//
// The reason is the row's window. Tiers 1-6 with Gauntlet.TierInterval at 5 is
// levels 5 to 30, which is Ragefire Chasm, Wailing Caverns, Deadmines,
// Shadowfang Keep, the Stockade and Gnomeregan -- so the card as written does
// not make those levels harder, it removes them, for the remaining seventy
// levels of the run. The design's own note says as much ("on a server where
// the levelling dungeons are the point it is a build-brick") and offers a
// server switch as the answer, which trades a bad affix for an absent one.
//
// Design section 2.9's known-bad patterns reject affixes whose only
// instruction is "don't", and a permanent veto is the purest example of one.
// Half a health pool is not a veto: it is a price, it is paid only while the
// player is actually in a group, it ends the moment they leave, and it makes
// every dungeon invitation a real question with a real answer on both sides.
// That is the shape every other affix in this module has.
//
// Both halves are live and reversible, and that is the mechanic. Nothing here
// is persisted, because there is nothing to persist: the state is "is this
// player in a group", and the core already knows.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_LONE_WOLF = 24;

        // The two numbers. Neither ladders -- R2 is maxRank 1, like every
        // Rules row -- so there is no rank table here.
        constexpr float GROUPED_HEALTH_MULT = 0.5f;

        // The experience half is the card's own 20%, and it is the registry
        // row's Boon::BonusExperience, so it comes from the offer's boon
        // magnitude and the player reads the real number in the offer line.
        constexpr uint8 SOLO_XP_PCT_FALLBACK = 20;

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_LONE_WOLF);
            return def ? def->key : "lone_wolf";
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        bool InGroup(Player* player)
        {
            // GetGroup covers parties and raids alike (Player.h's group
            // accessor returns the same object for both), and a group of bots
            // is a group: this affix is about playing alone, and a bot party
            // would otherwise be a free bypass on a realm that runs five
            // hundred of them.
            return player && player->GetGroup() != nullptr;
        }

        class LoneWolf final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override
            {
                _grouped = InGroup(ctx.player);
                Publish(ctx);
            }

            void OnDetach(Ctx& ctx) override
            {
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, MechanicKey(), 0);
            }

            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override;

            // The curse half. Not AggregateFactor, which is Player-free and
            // therefore cannot see a group; this callback has the Ctx.
            //
            // It also sets _grouped, and the placement is load-bearing rather
            // than lazy: Mgr::OnMaxHealth runs the mechanics' OnMaxHealth and
            // then immediately asks EffectiveCaps for the floor, so setting the
            // flag here means RelaxCaps below reads it in the same call it was
            // written in and can never be a refresh behind.
            void OnMaxHealth(Ctx& ctx, float& value) override
            {
                _grouped = InGroup(ctx.player);

                if (_grouped)
                    value *= GROUPED_HEALTH_MULT;
            }

            // Without this the floor eats the affix. Gauntlet.Caps.MaxHealth
            // is 0.6 and this is a halving, so the clamp would hand the player
            // -40% behind a blurb that says half -- the same unfelt, misstated
            // scalar the whole redesign exists to delete. It widens the floor
            // to exactly this affix's own number and no further, and only
            // while the player is actually in a group, so the ordinary bound
            // returns on its own the moment they leave.
            void RelaxCaps(AffixInstance const& /*self*/, AggregateKind kind,
                           AggregateCaps& caps) const override
            {
                if (kind != AggregateKind::MaxHealth || !_grouped)
                    return;

                if (caps.maxHealthMin > GROUPED_HEALTH_MULT)
                    caps.maxHealthMin = GROUPED_HEALTH_MULT;
            }

            // The boon half, and it is conditional by design: the card pays
            // for playing alone, so it pays while the player is alone.
            void OnXP(Ctx& ctx, uint32& amount, Unit* /*victim*/) override
            {
                if (_grouped || amount == 0 || !ctx.self)
                    return;

                uint32 const pct = ctx.self->boonMag != 0 ? uint32(ctx.self->boonMag)
                                                          : SOLO_XP_PCT_FALLBACK;

                amount += uint32(uint64(amount) * pct / 100u);
            }

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                return std::string("lone wolf: ") + (InGroup(ctx.player) ? "grouped, health halved"
                                                                        : "solo, experience raised");
            }

        private:
            void Publish(Ctx& ctx);

            bool _grouped     = false;
            bool _published   = false;
            bool _lastGrouped = false;
        };

        void LoneWolf::OnTick(Ctx& ctx, uint32 /*diffMs*/)
        {
            bool const now = InGroup(ctx.player);
            if (now == _lastGrouped && _published)
                return;

            _grouped = now;

            // The GroupScript in GauntletScripts.cpp makes this instant, and
            // this makes it certain. They are not redundant: the hook fires on
            // the core's own group paths, and a realm running playerbots has
            // paths that add a member without going through all of them. Half
            // a second late is a tolerable worst case; never is not, because
            // the player would carry a halved pool out of a group they had
            // already left.
            if (ctx.player && ctx.player->IsInWorld())
                ctx.player->UpdateMaxHealth();

            Publish(ctx);
        }

        void LoneWolf::Publish(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            bool const wasPublished = _published;
            bool const changed      = _published && _lastGrouped != _grouped;

            _lastGrouped = _grouped;
            _published   = true;

            // A standing readout, because the affix is a standing state and
            // has no event of its own. It is a flag rather than a number: the
            // HUD shows the row only while the value is non-zero, so "Grouped:
            // half health" is on screen exactly while the penalty is being
            // paid and gone the moment it is not.
            //
            // Nothing is sent for the solo half. Being alone is this run's
            // ordinary state and the experience boon is in the affix panel; a
            // permanent row saying "everything is fine" is what turns a HUD
            // into wallpaper. The health pool is what the player is watching,
            // and it is not smaller.
            AddonFor(ctx)->QueueStat(player, MechanicKey(), _grouped ? 1 : 0);

            // And the moment it flips, in chat, for a player with no addon --
            // which is also the moment it matters, because the pool changes
            // under them.
            if (changed && player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    _grouped ? "|cffff2020[Gauntlet]|r Lone Wolf: in a group your health is halved."
                             : "|cffff2020[Gauntlet]|r Lone Wolf: alone again. Your health returns.");

            (void)wasPublished;
        }

        std::string LoneWolf::Describe(AffixInstance const& self) const
        {
            uint32 const pct = self.boonMag != 0 ? uint32(self.boonMag) : SOLO_XP_PCT_FALLBACK;

            // No BoonClause, for Frenzy's reason: the boon is not paid "in
            // exchange" for the curse, it is the other side of the same
            // choice, and a second sentence promising it again would read as a
            // separate bonus that applies all the time.
            return "While you are in a group your maximum health is halved. While you are alone"
                   " you gain " + std::to_string(pct) + "% more experience. Both change the moment"
                   " you join or leave.";
        }
    }

    GAUNTLET_MECHANIC(24, LoneWolf);
}
