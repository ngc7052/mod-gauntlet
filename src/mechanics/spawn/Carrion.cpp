/*
 * mod-gauntlet - S3 Carrion: looting draws scavengers, and corpses are richer
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"
#include "GauntletScheduler.h"
#include "GauntletState.h"
#include "GauntletSummons.h"
#include "../Boons.h"

#include "Chat.h"
#include "Creature.h"
#include "LootMgr.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "Position.h"
#include "SharedDefines.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
#include <iterator>

// Registry id 3. Design section 3, card S3: "Every 4th corpse you loot draws
// scavengers. Corpses are richer."
//
// One of the three affixes design section 4.6 puts at tier 1, because it is one
// of the three that teach what an affix is: the counter is on screen, the
// trigger is the player's own hand on a corpse, and the reward is paid into
// every purse whether or not the scavengers ever arrive. "Skip looting trash
// when hurt; loot in cleared areas with your back to a wall; loot at full
// health, not at 40% mid-eat" is a decision the player makes several times a
// minute.
//
// Both halves of the card's boon are here: the money is paid in OnLootMoney and
// the item chance in OnItemRoll, which is the one hook in the core that can
// raise a drop rate for one player.

namespace Gauntlet
{
    namespace
    {
        constexpr uint16 MECHANIC_CARRION = 3;

        constexpr uint32 EVENT_SCAVENGERS = 1;

        // The card's ladder: every 5 -> 4 -> 3 loots, 2 -> 2 -> 3 scavengers.
        constexpr int32 LOOTS_PER_PACK[] = { 5, 4, 3 };
        static_assert(std::size(LOOTS_PER_PACK) >= MAX_RANK, "LOOTS_PER_PACK is short a rank");
        constexpr uint32 PACK_SIZE[]      = { 2, 2, 3 };
        static_assert(std::size(PACK_SIZE) >= MAX_RANK, "PACK_SIZE is short a rank");

        // The card's other fixed number: they arrive 25 yards away and charge.
        constexpr float SPAWN_YARDS = 25.0f;

        // "+25% item drop chance on creature loot", the half of the boon that
        // is not money. Fixed rather than laddered, because the card states one
        // number and BoonTable has no second magnitude to give this row.
        constexpr float ITEM_CHANCE_MULT = 1.25f;   // TODO(design)

        // Not on the card. Scavengers are a fight the player brought on
        // themselves by looting, so they get the same two minutes to be fought
        // or walked away from that every other summon has, and four seconds of
        // warning -- which is the same lead Ambush's card names for the same
        // situation, a fight that starts while you are standing still.
        constexpr uint32 LIFETIME_MS = 120000;   // TODO(design)
        constexpr uint32 WARN_MS     = 4000;     // TODO(design)
        constexpr uint32 DEFER_MS    = 10000;    // TODO(design)

        // Persistent: plan section 3.3 names "carrion.loots" itself. A counter
        // that resets at the login screen cannot be planned around, and
        // planning is the whole decision this affix offers.
        constexpr char const* KEY_LOOTS = "carrion.loots";

        // How many recently-counted corpses are remembered, so that reopening a
        // loot window -- which a player does constantly, because the window
        // closes on every partial take -- cannot advance the counter twice for
        // one corpse. A handful is enough: the guids being compared are the
        // corpses in front of the player right now.
        constexpr std::size_t RECENT_CORPSES = 8;

        uint8 RankIndex(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return static_cast<uint8>((rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank)) - 1);
        }

        char const* MechanicKey()
        {
            MechanicDef const* def = FindMechanic(MECHANIC_CARRION);
            return def ? def->key : "carrion";
        }

        Addon* AddonFor(Ctx& ctx) { return ctx.addon ? ctx.addon : sGauntletAddon; }

        class Carrion final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override;
            void OnDetach(Ctx& ctx) override;

            // The loot window opening, which is the card's trigger: "count
            // distinct creature corpses whose loot window the owner opens".
            void OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* loot) override;

            // The boon's money half.
            void OnLootMoney(Ctx& ctx, Loot* loot) override;

            // And its item half.
            void OnItemRoll(Ctx& ctx, float& chance) override;

            void OnWarn(Ctx& ctx, uint32 eventId) override;
            void OnEvent(Ctx& ctx, uint32 eventId) override;

            std::string Describe(AffixInstance const& self) const override;

        private:
            void  Arrive(Ctx& ctx);
            void  ShowCounter(Ctx& ctx) const;
            int32 Threshold(Ctx const& ctx) const { return LOOTS_PER_PACK[RankIndex(ctx.self)]; }
            int32 Loots(Ctx& ctx) const;
            void  SetLoots(Ctx& ctx, int32 value);
            bool  AlreadyCounted(ObjectGuid const& guid);

            std::vector<ObjectGuid> _recent;
            int32  _transient = 0;   // only read when ctx.state is null
            bool   _pending   = false;
        };

        int32 Carrion::Loots(Ctx& ctx) const
        {
            int32 const stored = ctx.state ? ctx.state->Get(KEY_LOOTS, 0) : _transient;
            return std::max(0, stored);
        }

        void Carrion::SetLoots(Ctx& ctx, int32 value)
        {
            _transient = value;
            if (ctx.state)
                ctx.state->Set(KEY_LOOTS, value);
        }

        void Carrion::ShowCounter(Ctx& ctx) const
        {
            if (ctx.player)
                AddonFor(ctx)->QueueCounter(ctx.player, MechanicKey(),
                                            uint32(Loots(ctx)), uint32(Threshold(ctx)));
        }

        void Carrion::OnAttach(Ctx& ctx)
        {
            _recent.clear();
            _pending   = false;
            _transient = ctx.state ? std::max(0, ctx.state->Get(KEY_LOOTS, 0)) : 0;

            // A counter that is already full when the session starts is armed
            // straight away rather than waiting for one more corpse. The
            // counter is persisted and `_pending` is not, so a player who
            // logged out on the last loot before a pack came back to a counter
            // reading 5/5 that would sit there until they looted a sixth --
            // and looting a sixth is exactly what a player who thinks the affix
            // is broken stops doing.
            //
            // Echo has done this since it was written; Carrion did not, and the
            // two are the same shape. The scheduler's own grace window is what
            // keeps the pack off the player until they have taken control.
            if (ctx.clock && Loots(ctx) >= Threshold(ctx))
            {
                _pending = true;
                ctx.clock->Arm(MECHANIC_CARRION, EVENT_SCAVENGERS, WARN_MS, WARN_MS);
            }

            ShowCounter(ctx);
        }

        void Carrion::OnDetach(Ctx& ctx)
        {
            if (ctx.clock)
                ctx.clock->Cancel(MECHANIC_CARRION);

            if (ctx.player)
                sGauntletSummons->DespawnFor(ctx.player, MECHANIC_CARRION);

            _recent.clear();
            _pending = false;
        }

        bool Carrion::AlreadyCounted(ObjectGuid const& guid)
        {
            if (std::find(_recent.begin(), _recent.end(), guid) != _recent.end())
                return true;

            if (_recent.size() >= RECENT_CORPSES)
                _recent.erase(_recent.begin());
            _recent.push_back(guid);
            return false;
        }

        void Carrion::OnLoot(Ctx& ctx, ObjectGuid const& lootGuid, Loot* /*loot*/)
        {
            Player* player = ctx.player;
            if (!player || !ctx.clock)
                return;
            if (ctx.run && (ctx.run->dead || !ctx.run->pending.empty()))
                return;

            // "Corpses", so a chest, a herb node or a fishing bobber is not
            // one. ObjectGuid::IsCreature is the branch design section 6 names
            // for exactly this hook.
            if (!lootGuid.IsCreature())
                return;

            if (AlreadyCounted(lootGuid))
                return;

            int32 const max  = Threshold(ctx);
            int32 const next = std::min(Loots(ctx) + 1, max);
            SetLoots(ctx, next);
            ShowCounter(ctx);

            if (next < max || _pending)
                return;

            _pending = true;
            ctx.clock->Arm(MECHANIC_CARRION, EVENT_SCAVENGERS, WARN_MS, WARN_MS);
        }

        void Carrion::OnWarn(Ctx& ctx, uint32 eventId)
        {
            Player* player = ctx.player;
            if (eventId != EVENT_SCAVENGERS || !player)
                return;
            if (ctx.run && ctx.run->dead)
                return;

            AddonFor(ctx)->SendEvent(player, MechanicKey(), WARN_MS / 1000u, "Carrion");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Something has smelled the blood.");
        }

        void Carrion::OnEvent(Ctx& ctx, uint32 eventId)
        {
            Player* player = ctx.player;
            if (eventId != EVENT_SCAVENGERS || !player || !ctx.clock)
                return;
            if (ctx.run && ctx.run->dead)
                return;

            if (!player->IsInWorld() || !player->IsAlive())
            {
                ctx.clock->Arm(MECHANIC_CARRION, EVENT_SCAVENGERS, DEFER_MS, 0);
                return;
            }

            Arrive(ctx);
        }

        void Carrion::Arrive(Ctx& ctx)
        {
            Player* player = ctx.player;

            uint32 const wanted = PACK_SIZE[RankIndex(ctx.self)];
            uint32       spawned = 0;

            for (uint32 n = 0; n < wanted; ++n)
            {
                // Spread around the player rather than stacked on one spot, so
                // a pack reads as a pack and "back to a wall" is a real answer:
                // the angles are evenly spaced across the half-circle behind
                // and beside them.
                float const angle = float(M_PI) * (0.5f + float(n) / float(wanted));

                Position const at = player->GetFirstCollisionPosition(SPAWN_YARDS, angle);

                Creature* scavenger = sGauntletSummons->Summon(player, ENTRY_SCAVENGER, at, LIFETIME_MS,
                                                               /*countsAsStalker*/ false, MECHANIC_CARRION);
                if (!scavenger)
                    break;   // the caps refused; whatever arrived is the pack

                scavenger->HandleEmoteCommand(EMOTE_ONESHOT_BATTLE_ROAR);   // Unit.h:1943
                ++spawned;
            }

            _pending = false;

            if (spawned == 0)
            {
                // Nothing could be put into the world, so the counter is not
                // spent: the player is still owed the pack their looting paid
                // for.
                ctx.clock->Arm(MECHANIC_CARRION, EVENT_SCAVENGERS, DEFER_MS, 0);
                return;
            }

            SetLoots(ctx, 0);
            ShowCounter(ctx);

            Addon* addon = AddonFor(ctx);
            addon->SendEvent(player, MechanicKey(), 0, "Carrion");

            if (player->GetSession())
                ChatHandler(player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r Scavengers come for the bodies.");
        }

        void Carrion::OnLootMoney(Ctx& ctx, Loot* loot)
        {
            if (!loot || !loot->gold || !ctx.self)
                return;

            float const mult = BoonMoneyMult(*ctx.self);
            if (mult <= 1.0f)
                return;

            uint64 const raised = static_cast<uint64>(static_cast<double>(loot->gold) * mult);
            loot->gold = static_cast<uint32>(std::min<uint64>(raised, std::numeric_limits<uint32>::max()));
        }

        void Carrion::OnItemRoll(Ctx& ctx, float& chance)
        {
            // The other half of the card's built-in boon, and the only one of
            // the two that needs a hook nothing else in the module uses. It is
            // gated on the instance actually carrying the money boon, so an
            // instance whose boon was overridden does not quietly keep the
            // half that has no magnitude of its own.
            if (!ctx.self || ctx.self->boon != Boon::BonusMoney || chance <= 0.0f)
                return;

            chance *= ITEM_CHANCE_MULT;
        }

        std::string Carrion::Describe(AffixInstance const& self) const
        {
            uint8 const i = RankIndex(&self);

            std::string out = "Every " + std::to_string(LOOTS_PER_PACK[i])
                            + "th corpse you loot draws " + std::to_string(PACK_SIZE[i])
                            + " scavengers, four seconds after the last one you opened."
                              " Loot in cleared ground, and at full health.";

            out += BoonClause(self.boon, self.boonMag);
            if (self.boon == Boon::BonusMoney)
                out += " Creature loot also drops items a quarter more often.";

            return out;
        }
    }

    GAUNTLET_MECHANIC(3, Carrion);
}
