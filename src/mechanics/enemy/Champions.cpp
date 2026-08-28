/*
 * mod-gauntlet - E1 Champions: every Nth fight you start opens against a Champion
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"
#include "GauntletAddon.h"
#include "GauntletState.h"
#include "../attrition/Scalars.h"

#include "Chat.h"
#include "Creature.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Unit.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

// Registry id 6. Design section 4.6 wants this to be one of the first affixes a
// character ever meets, because it is the one that teaches that an affix can be
// content rather than tax: the counter is on screen, so the player -- not the
// module -- decides which fight opens against the Champion.
//
// The whole affix turns on one word: it counts *fights*, not creatures. That is
// the Prideful pattern with the snowball taken out, and the core makes it
// almost free. PlayerScript::OnPlayerEnterCombat is called from
// CombatManager::UpdateOwnerCombatState, which returns early when the combat
// state has not actually changed ($CORE/src/server/game/Combat/CombatManager.cpp
// :411-413) and only then sets the flag and calls the hook (:417-423). So the
// hook fires exactly once per fight, on the out-of-combat to in-combat edge,
// and a second creature body-pulled into a fight already under way never
// reaches it. The plan's other suggestion -- reading player->IsInCombat()
// before the core sets it -- is not available: SetUnitFlag(UNIT_FLAG_IN_COMBAT)
// happens at :417, four lines before the hook, and IsInCombat() is that flag
// (Unit.h:936). The edge is the test, and it is the core's own.
namespace Gauntlet
{
    namespace
    {
        constexpr char const* CHAMPIONS_KEY = "champions";

        // Persistent, per CONTRACT-P1 section 5.2. A player who logs out at 7
        // of 8 and comes back at 0 cannot plan around the affix at all, and
        // planning is the only decision it offers. 15 characters, inside
        // State::MaxKeyLen.
        constexpr char const* STATE_COUNT = "champions.count";

        // Design section 4.6's severity ladder, rank I to III.
        constexpr uint32 FIGHTS_PER_CHAMPION[MAX_RANK] = { 10, 8, 6 };
        constexpr float  HEALTH_MULT[MAX_RANK]         = { 2.0f, 2.5f, 3.0f };

        // "twice the health" for each rung of the ladder above, for Describe.
        constexpr char const* HEALTH_WORDS[MAX_RANK] = { "twice the", "two and a half times the",
                                                         "three times the" };

        // The card names one number for each of these and does not vary them
        // by rank, so neither do we.
        constexpr float CHAMPION_SCALE     = 1.3f;
        constexpr float OWNER_DAMAGE_MULT  = 1.25f;
        constexpr uint32 XP_MULT           = 2;

        // "Enrage". Verified present in Spell.dbc (record id 8599, SpellName
        // "Enrage") and used as SPELL_ENRAGE by exactly six core scripts:
        // alterac_valley.cpp:27, boss_ossirian.cpp:382,
        // mob_anubisath_sentinel.cpp:50, boss_keristrasza.cpp:31,
        // zone_shadowmoon_valley.cpp:1326 and scourge_invasion.h.
        //
        // It is not only a visual. Its two effects are SPELL_AURA_MOD_DAMAGE_
        // PERCENT_DONE +10 and SPELL_AURA_MOD_MELEE_HASTE +30
        // (SpellAuraDefines.h:142 and :201), for 120 s (DurationIndex 4). Those
        // apply to everything the Champion hits, not only to its owner, which
        // the owner-only +25% below deliberately does not. The design names
        // this spell by id, so it is used as named and the leak is reported
        // rather than hidden. The duration is longer than an open-world fight,
        // and the core strips the aura on evade (Unit::RemoveEvadeAuras,
        // Unit.cpp:5730, reached from CreatureAI::_EnterEvadeMode,
        // CreatureAI.cpp:403), so nothing has to expire it by hand.
        constexpr uint32 SPELL_ENRAGE = 8599;

        // Champions promote one creature every six to ten fights, so this is
        // slack rather than a budget: it bounds a pathological case, not
        // honest play. Entries hold an ObjectGuid and two numbers, never a
        // pointer, so a stale one cannot dangle.
        constexpr std::size_t MAX_TRACKED = 4;

        uint8 RankOf(AffixInstance const* self)
        {
            uint8 const rank = self ? self->rank : 1;
            return rank < 1 ? 1 : (rank > MAX_RANK ? MAX_RANK : rank);
        }

        uint32 Threshold(AffixInstance const* self) { return FIGHTS_PER_CHAMPION[RankOf(self) - 1]; }
        float  HealthMult(AffixInstance const* self) { return HEALTH_MULT[RankOf(self) - 1]; }

        class Champions final : public IMechanic
        {
        public:
            void OnAttach(Ctx& ctx) override;
            void OnDetach(Ctx& ctx) override;
            void OnEnterCombat(Ctx& ctx, Unit* enemy, bool wasOutOfCombat) override;
            void OnLeaveCombat(Ctx& ctx) override;
            void OnKill(Ctx&, Creature* victim) override;
            void OnXP(Ctx&, uint32& amount, Unit* victim) override;
            void OnLoot(Ctx&, ObjectGuid const& lootGuid, Loot* loot) override;

            float DamageTakenMult(Ctx&, Unit* attacker, SpellInfo const*) override;

            std::string Describe(AffixInstance const& self) const override;

        private:
            // What one promotion did, so it can be undone, and what it is still
            // owed. Creature-side state keyed by ObjectGuid is plan section
            // 2.2's shape; the deviation is where it lives -- see the note on
            // Restore().
            struct Champion
            {
                ObjectGuid guid;
                float      scale     = 1.0f;   // the creature's scale before promotion
                uint32     maxHealth = 0;      // and its max health
                bool       promoted  = true;   // the buffs are still on it
                bool       slain     = false;  // the owner killed it; a coin roll is owed
            };

            Champion* Find(ObjectGuid const& guid);

            // The counter, clamped to the current rank's threshold. ctx.state
            // is live in a run and null under `.gauntlet debug`, which is the
            // only reason the transient mirror exists.
            uint32 Count(Ctx& ctx, uint32 max) const;
            uint32 Bump(Ctx& ctx, uint32 max);
            void   ResetCount(Ctx& ctx);

            // Everything that must be true before a fight may open against a
            // Champion, as opposed to merely being counted.
            bool MayPromote(Ctx& ctx) const;
            bool IsPromotable(Creature* creature) const;

            void Promote(Ctx& ctx, Creature* creature);
            void Restore(Player* owner, Champion& record);
            void ShowCounter(Ctx& ctx, uint32 count, uint32 max) const;

            std::vector<Champion> _champions;

            // Only ever read when ctx.state is null.
            uint32 _transientCount = 0;
        };

        Champions::Champion* Champions::Find(ObjectGuid const& guid)
        {
            for (Champion& record : _champions)
                if (record.guid == guid)
                    return &record;
            return nullptr;
        }

        uint32 Champions::Count(Ctx& ctx, uint32 max) const
        {
            int32 const stored = ctx.state ? ctx.state->Get(STATE_COUNT, 0) : int32(_transientCount);
            if (stored <= 0)
                return 0;
            return std::min<uint32>(uint32(stored), max);
        }

        uint32 Champions::Bump(Ctx& ctx, uint32 max)
        {
            // The counter stops at the threshold rather than running past it.
            // It only ever sits there because a promotion was refused -- the
            // opener was an elite, or an offer was on the table -- and the
            // player is owed that Champion on the next fight that can carry
            // one, not a debt of several.
            int32 next;
            if (ctx.state)
                next = ctx.state->Add(STATE_COUNT, 1);
            else
                next = int32(++_transientCount);

            if (next < 0 || uint32(next) > max)
            {
                next = int32(max);
                if (ctx.state)
                    ctx.state->Set(STATE_COUNT, next);
                else
                    _transientCount = max;
            }

            return uint32(next);
        }

        void Champions::ResetCount(Ctx& ctx)
        {
            if (ctx.state)
                ctx.state->Set(STATE_COUNT, 0);
            _transientCount = 0;
        }

        void Champions::ShowCounter(Ctx& ctx, uint32 count, uint32 max) const
        {
            // The counter being on screen is not decoration: design section 4.6
            // makes the choice of which fight to spend it on the whole of the
            // affix's counterplay, and there is no choice if the number is
            // invisible. CTR coalesces to the latest value per key, so sending
            // it on every engage costs one message per fight at most.
            if (ctx.addon && ctx.player)
                ctx.addon->QueueCounter(ctx.player, CHAMPIONS_KEY, count, max);
        }

        void Champions::OnAttach(Ctx& ctx)
        {
            if (!ctx.player)
                return;

            uint32 const max = Threshold(ctx.self);
            ShowCounter(ctx, Count(ctx, max), max);
        }

        void Champions::OnDetach(Ctx& ctx)
        {
            // Logout, death, or the affix being swapped away. A creature this
            // module inflated must not be left inflated behind a player who is
            // no longer there.
            for (Champion& record : _champions)
                Restore(ctx.player, record);

            _champions.clear();
        }

        bool Champions::MayPromote(Ctx& ctx) const
        {
            Player* owner = ctx.player;
            if (!owner || !owner->IsInWorld() || !owner->IsAlive())
                return false;

            // A retired run does nothing, and nothing lands on a player who is
            // reading an offer: design section 4.2's suppression list is
            // written for the scheduler, but "not while the panel is open" is
            // the same courtesy however the event arrived.
            if (ctx.run && (ctx.run->dead || !ctx.run->pending.empty()))
                return false;

            if (owner->IsGameMaster())
                return false;

            // Battleground and arena creatures are furniture -- flag carriers,
            // spirit guides, siege engines -- and promoting one is noise, not
            // content.
            Map* map = owner->GetMap();
            if (map && map->IsBattlegroundOrArena())
                return false;

            return true;
        }

        bool Champions::IsPromotable(Creature* creature) const
        {
            if (!creature || !creature->IsInWorld() || !creature->IsAlive())
                return false;

            // "Never an elite, a boss or a quest creature." isElite() is false
            // for rank RARE, so a rare mob is fair game and a rare elite is
            // not (Creature.h:115-122); isWorldBoss() reads the template's
            // boss type flag (:124) and IsDungeonBoss() the flags_extra (:132).
            if (creature->isElite() || creature->isWorldBoss() || creature->IsDungeonBoss())
                return false;

            // IsQuestGiver() is the only cheap test the core offers for "a
            // creature a quest needs" (Unit.h:806); a kill-objective mob is
            // indistinguishable from any other and is deliberately left
            // promotable, since being harder to kill is the point.
            if (creature->IsQuestGiver())
                return false;

            // Nothing that belongs to somebody, nothing that is scenery, and
            // nothing another script summoned: promoting a warlock's felhunter
            // or a scripted event's add is a bug in every case.
            if (creature->IsPet() || creature->IsGuardian() || creature->IsTotem() ||
                creature->IsSummon() || creature->IsVehicle() || creature->IsCritter() ||
                creature->IsTrigger() || creature->IsCharmedOwnedByPlayerOrPlayer())
                return false;

            return true;
        }

        void Champions::Promote(Ctx& ctx, Creature* creature)
        {
            // A record survives its fight only until the coin roll it is owed;
            // clear the leftovers of previous Champions before adding one more.
            _champions.erase(std::remove_if(_champions.begin(), _champions.end(),
                                            [](Champion const& c) { return !c.promoted && !c.slain; }),
                             _champions.end());

            while (_champions.size() >= MAX_TRACKED)
                _champions.erase(_champions.begin());

            Champion record;
            record.guid      = creature->GetGUID();
            record.scale     = creature->GetObjectScale();
            record.maxHealth = creature->GetMaxHealth();
            _champions.push_back(record);

            creature->SetObjectScale(record.scale * CHAMPION_SCALE);

            // SetMaxHealth clamps current health down to the new maximum
            // (Unit.cpp:12441), so the order below matters only in that
            // SetFullHealth then tops the Champion up to its new pool.
            float const scaled = float(record.maxHealth) * HealthMult(ctx.self);
            creature->SetMaxHealth(uint32(std::min(scaled, float(std::numeric_limits<int32>::max()))));
            creature->SetFullHealth();

            // AddAura tolerates a missing spell and a dead target by returning
            // null (Unit.cpp:15150-15161); the Champion is neither, and there
            // is nothing useful to do if the aura does not take.
            creature->AddAura(SPELL_ENRAGE, creature);

            if (ctx.addon)
                ctx.addon->SendEvent(ctx.player, CHAMPIONS_KEY, 0, "Champion");

            // The chat line is the fallback for a player with no addon, and it
            // is what design section 4.8 asks for: the affix says that it acted.
            ChatHandler(ctx.player->GetSession())
                .PSendSysMessage("|cffff2020[Gauntlet]|r A Champion rises to meet you.");
        }

        void Champions::Restore(Player* owner, Champion& record)
        {
            if (!record.promoted)
                return;

            // Marked undone whether or not the creature can still be reached:
            // a record that keeps claiming a live promotion would keep handing
            // out the damage bonus.
            record.promoted = false;

            if (!owner || !owner->IsInWorld())
                return;

            // ObjectAccessor.h:70. It searches the map `owner` is on, so a
            // Champion left behind by a teleport is simply not found -- see the
            // report; combat ends before a teleport, so the ordinary case
            // reverts first.
            Creature* creature = ObjectAccessor::GetCreature(*owner, record.guid);
            if (!creature || !creature->IsInWorld())
                return;

            // A Champion dying is the common way a fight ends. Unit::Kill stops
            // combat (Unit.cpp:14150) before it sets the death state (:14165),
            // so the creature can still answer IsAlive() here while being on
            // its way out; shrinking a corpse back down is pointless, and
            // restoring the health of something at zero is worse.
            if (!creature->IsAlive() || !creature->GetHealth())
                return;

            creature->RemoveAurasDueToSpell(SPELL_ENRAGE);
            creature->SetObjectScale(record.scale);
            if (record.maxHealth)
                creature->SetMaxHealth(record.maxHealth);
        }

        void Champions::OnEnterCombat(Ctx& ctx, Unit* enemy, bool wasOutOfCombat)
        {
            // wasOutOfCombat is the contract with integration, not a guess: the
            // core only reaches this hook on the out-of-combat edge (see the
            // note at the top of this file), so integration passes true and a
            // false here can only mean a call site that is not that edge.
            if (!ctx.player || !wasOutOfCombat)
                return;

            uint32 const max   = Threshold(ctx.self);
            uint32 const count = Bump(ctx, max);

            if (count < max)
            {
                ShowCounter(ctx, count, max);
                return;
            }

            Creature* opener  = enemy ? enemy->ToCreature() : nullptr;
            Champion* existing = opener ? Find(opener->GetGUID()) : nullptr;

            if (!MayPromote(ctx) || !IsPromotable(opener) || (existing && existing->promoted))
            {
                // The counter stays where it is. The player spent nine fights
                // getting here and picked this one; a boss walking into them,
                // or an offer sitting on the table, must not spend it for them.
                ShowCounter(ctx, count, max);
                return;
            }

            Promote(ctx, opener);
            ResetCount(ctx);
            ShowCounter(ctx, 0, max);
        }

        void Champions::OnLeaveCombat(Ctx& ctx)
        {
            // The fight is over, so the promotion is over. This is the prune
            // the plan asks for: it covers the Champion dying, evading,
            // leashing, being left behind, and the owner running away, because
            // every one of them ends with the owner out of combat. Records that
            // are still owed a coin roll stay in the vector with promoted
            // false; nothing about them touches the world again.
            //
            // The core can call OnPlayerLeaveCombat twice for one exit
            // (CombatManager.cpp:433 through EndAllCombat, then
            // Unit::ClearInCombat itself at Unit.cpp:10714). Restore() is a
            // no-op the second time.
            for (Champion& record : _champions)
                Restore(ctx.player, record);
        }

        void Champions::OnKill(Ctx& /*ctx*/, Creature* victim)
        {
            if (!victim)
                return;

            if (Champion* record = Find(victim->GetGUID()))
            {
                record->promoted = false;   // it is dead; there is nothing to undo
                record->slain    = true;    // and a coin roll is owed on the corpse
            }
        }

        void Champions::OnXP(Ctx& /*ctx*/, uint32& amount, Unit* victim)
        {
            if (!amount || !victim)
                return;

            Champion* record = Find(victim->GetGUID());
            if (!record)
                return;

            // KillRewarder fires this from inside Unit::Kill (Unit.cpp:14096,
            // KillRewarder.cpp:184) -- before combat stops at :14150 and well
            // before OnPlayerCreatureKill at :14306 -- so the record is still
            // here, and this is the earliest place that knows the Champion died.
            record->slain = true;

            uint64 const doubled = uint64(amount) * XP_MULT;
            amount = uint32(std::min<uint64>(doubled, std::numeric_limits<uint32>::max()));
        }

        void Champions::OnLoot(Ctx& /*ctx*/, ObjectGuid const& lootGuid, Loot* loot)
        {
            if (!loot || !loot->gold)
                return;

            // Creature::AddToWorld stamps the creature's own guid onto its loot
            // (Creature.cpp:331); lootGuid is the fallback for whatever
            // integration hands us.
            ObjectGuid const source = loot->sourceWorldObjectGUID.IsEmpty() ? lootGuid
                                                                           : loot->sourceWorldObjectGUID;
            if (source.IsEmpty())
                return;

            auto const it = std::find_if(_champions.begin(), _champions.end(),
                                         [&source](Champion const& c) { return c.guid == source; });
            if (it == _champions.end() || !it->slain)
                return;

            // "A guaranteed extra coin roll": the money is already rolled and
            // already has the server's money rate on it by the time this hook
            // runs, so a second roll of the same purse is a doubling. Rolling
            // creature_template's mingold..maxgold again would ignore that rate
            // and read as a different, smaller reward.
            uint64 const doubled = uint64(loot->gold) * 2;   // TODO(design)
            loot->gold = uint32(std::min<uint64>(doubled, std::numeric_limits<uint32>::max()));

            _champions.erase(it);
        }

        float Champions::DamageTakenMult(Ctx& /*ctx*/, Unit* attacker, SpellInfo const*)
        {
            if (_champions.empty() || !attacker)
                return 1.0f;

            // The +25% is owner-only by construction rather than by a check:
            // this map belongs to one carried affix, which belongs to one
            // RunState, which belongs to one player, and this callback is only
            // ever reached for damage that player is taking. Another player hit
            // by the same Champion asks their own Champions -- if they carry
            // one at all -- and it has never heard of this creature.
            Champion const* record = Find(attacker->GetGUID());
            return record && record->promoted ? OWNER_DAMAGE_MULT : 1.0f;
        }

        std::string Champions::Describe(AffixInstance const& self) const
        {
            uint8 const rank = RankOf(&self);

            // 6, 8 and 10 all take "th", so the ordinal needs no table.
            std::string out = "Every " + std::to_string(FIGHTS_PER_CHAMPION[rank - 1])
                            + "th fight you start opens against a Champion: "
                            + HEALTH_WORDS[rank - 1]
                            + " health, harder hits, and double the reward.";

            out += BoonClause(self.boon, self.boonMag);
            return out;
        }
    }

    GAUNTLET_MECHANIC(6, Champions);
}
