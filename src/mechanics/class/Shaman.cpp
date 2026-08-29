/*
 * mod-gauntlet - the shaman's two: One Totem, Totemic Anchor
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletAddon.h"
#include "GauntletRegistry.h"

#include "Chat.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"

#include <algorithm>
#include <string>

// Design section 3, family C, shaman. The card for C26 notes that the two
// compose -- one totem, one anchor -- and that is the shape of the pair: the
// first makes the choice of totem matter, the second makes where it stands
// matter.

namespace Gauntlet
{
    namespace
    {
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

        // The four totem slots, walked the way the core walks them
        // ($CORE/src/server/game/Battlegrounds/Zones/BattlegroundRV.cpp:99).
        // m_SummonSlot is public and read directly there, so it is read
        // directly here; SUMMON_SLOT_TOTEM_FIRE and MAX_TOTEM_SLOT are in
        // SharedDefines.h:3544.
        template <typename Fn>
        void ForEachTotem(Player* player, Fn&& fn)
        {
            if (!player || !player->IsInWorld())
                return;

            Map* map = player->GetMap();
            if (!map)
                return;

            for (uint8 slot = SUMMON_SLOT_TOTEM_FIRE; slot < MAX_TOTEM_SLOT; ++slot)
                if (player->m_SummonSlot[slot])
                    if (Creature* totem = map->GetCreature(player->m_SummonSlot[slot]))
                        fn(slot, totem);
        }

        // ==================================================================
        // C25 - One Totem (52)
        //
        // "Only one totem may stand at a time."
        //
        // The identity verb. Four buttons the class presses as a set become a
        // real allocation: Stoneclaw to tank, Searing to kill, Healing Stream
        // to last, Earthbind to run -- one of them, this fight.
        // ==================================================================
        constexpr uint16 MECHANIC_ONE_TOTEM = 52;

        // Rank II makes the standing totem die to one hit; rank III doubles
        // what totems cost. Rank I is the rule alone.
        constexpr bool  FRAGILE[MAX_RANK]     = { false, true,  true  };
        constexpr uint32 COST_MULT[MAX_RANK]  = { 1,     1,     2     };

        class OneTotem final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player, KeyOf(MECHANIC_ONE_TOTEM, "c25_one_totem"), 0);
            }

            void OnSpellCast(Ctx& ctx, Spell* spell) override;
            void OnTick(Ctx& ctx, uint32 diffMs) override;

            std::string Describe(AffixInstance const& self) const override;

            std::string Diagnose(Ctx& ctx) const override
            {
                uint32 standing = 0;
                ForEachTotem(ctx.player, [&standing](uint8, Creature*) { ++standing; });
                return "one totem: " + std::to_string(standing) + " standing, "
                     + std::to_string(_culled) + " culled";
            }

        private:
            uint32 _culled     = 0;
            uint32 _sinceMs    = 0;
            uint8  _keepSlot   = 0;
        };

        void OneTotem::OnSpellCast(Ctx& ctx, Spell* spell)
        {
            Player* player = ctx.player;
            if (!player || !spell)
                return;

            SpellInfo const* info = spell->GetSpellInfo();
            if (!info)
                return;

            // The cost half of rank III, refunded in reverse: the cast has
            // already paid once, so a second payment is taken here.
            if (COST_MULT[RankIndexOf(ctx.self)] > 1 && info->ManaCost != 0)
            {
                bool summonsTotem = false;
                ForEachTotem(player, [&summonsTotem](uint8, Creature*) { summonsTotem = true; });
                if (summonsTotem)
                {
                    int32 const extra = int32(info->ManaCost);
                    player->SetPower(POWER_MANA, std::max<int32>(0, player->GetPower(POWER_MANA) - extra));
                }
            }
        }

        void OneTotem::OnTick(Ctx& ctx, uint32 diffMs)
        {
            Player* player = ctx.player;
            if (!player)
                return;

            _sinceMs += diffMs;

            // Culling on the tick rather than at the cast, and the reason is
            // ordering: the new totem is not in its slot until the spell
            // effect has run, so a cull at OnPlayerSpellCast would remove the
            // three that were already there and then let the fourth land --
            // which is the right answer only by accident, and wrong whenever
            // the cast fails. Half a second later the world is settled and the
            // newest totem is unambiguous.
            uint8     newestSlot = 0;
            Creature* newest     = nullptr;
            uint32    standing   = 0;

            ForEachTotem(player, [&](uint8 slot, Creature* totem)
            {
                ++standing;
                // The longest remaining lifetime is the one most recently
                // planted, because totems of the same rank share a duration.
                if (!newest || totem->GetRespawnTime() >= newest->GetRespawnTime())
                {
                    newest     = totem;
                    newestSlot = slot;
                }
            });

            if (standing > 1)
            {
                ForEachTotem(player, [&](uint8 slot, Creature* totem)
                {
                    if (slot == newestSlot)
                        return;

                    totem->DespawnOrUnsummon();
                    ++_culled;
                });

                _keepSlot = newestSlot;

                if (player->GetSession())
                    ChatHandler(player->GetSession()).PSendSysMessage(
                        "|cffff2020[Gauntlet]|r The spirits will answer one call at a time.");
            }

            // Rank II: whatever stands is fragile. Applied every tick because a
            // totem's health is restored by nothing else, and one hit is one
            // hit whether it lands now or in a minute.
            if (FRAGILE[RankIndexOf(ctx.self)])
                ForEachTotem(player, [](uint8, Creature* totem)
                {
                    if (totem->GetMaxHealth() > 1)
                    {
                        totem->SetMaxHealth(1);
                        totem->SetHealth(1);
                    }
                });

            AddonFor(ctx)->QueueStat(player, KeyOf(MECHANIC_ONE_TOTEM, "c25_one_totem"),
                                     int32(standing));
        }

        std::string OneTotem::Describe(AffixInstance const& self) const
        {
            uint8 const i = RankIndexOf(&self);

            std::string out = "Only one of your totems may stand at a time; planting another"
                              " takes the first away.";

            if (FRAGILE[i])
                out += " The one that stands dies to a single hit.";
            if (COST_MULT[i] > 1)
                out += " Totems cost double.";

            out += " In exchange the standing totem lasts twice as long.";
            return out;
        }

        // ==================================================================
        // C26 - Totemic Anchor (53)
        //
        // "You take 30% more damage when more than fifteen yards from your
        // totems."
        //
        // The anchor verb. The spirits protect their circle: drop totems where
        // the fight will be, pull to them, re-drop when you move. Kiting out
        // of the circle costs; kiting around it is the skill.
        // ==================================================================
        constexpr uint16 MECHANIC_TOTEMIC_ANCHOR = 53;

        constexpr float ADRIFT_MULT[MAX_RANK] = { 1.20f, 1.30f, 1.40f };
        constexpr float ANCHOR_YARDS = 15.0f;

        class TotemicAnchor final : public IMechanic
        {
        public:
            void OnDetach(Ctx& ctx) override
            {
                if (ctx.addon && ctx.player)
                    ctx.addon->QueueStat(ctx.player,
                                         KeyOf(MECHANIC_TOTEMIC_ANCHOR, "c26_totemic_anchor"), 0);
            }

            void OnTick(Ctx& ctx, uint32 /*diffMs*/) override
            {
                Player* player = ctx.player;
                if (!player)
                    return;

                bool const anchored = Anchored(player);
                if (anchored == _anchored && _published)
                    return;

                _anchored  = anchored;
                _published = true;

                AddonFor(ctx)->QueueStat(player, KeyOf(MECHANIC_TOTEMIC_ANCHOR, "c26_totemic_anchor"),
                                         anchored ? 0 : 1);
            }

            float DamageTakenMult(Ctx& ctx, Unit*, SpellInfo const*) override
            {
                return Anchored(ctx.player) ? 1.0f : ADRIFT_MULT[RankIndexOf(ctx.self)];
            }

            std::string Describe(AffixInstance const& self) const override
            {
                uint32 const extra = uint32((ADRIFT_MULT[RankIndexOf(&self)] - 1.0f) * 100.0f + 0.5f);

                return "You take " + std::to_string(extra) + "% more damage whenever no totem of"
                       " yours is within " + std::to_string(uint32(ANCHOR_YARDS))
                     + " yards. Drop them where the fight will be, and pull to them.";
            }

            std::string Diagnose(Ctx& ctx) const override
            {
                return std::string("totemic anchor: ")
                     + (Anchored(ctx.player) ? "inside the circle" : "ADRIFT");
            }

        private:
            static bool Anchored(Player* player)
            {
                bool near = false;
                ForEachTotem(player, [&near, player](uint8, Creature* totem)
                {
                    if (player->GetExactDist2d(totem) <= ANCHOR_YARDS)
                        near = true;
                });
                return near;
            }

            bool _anchored  = false;
            bool _published = false;
        };
    }

    GAUNTLET_MECHANIC(52, OneTotem);
    GAUNTLET_MECHANIC(53, TotemicAnchor);
}
