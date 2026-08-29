/*
 * mod-gauntlet - script hooks
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAddon.h"
#include "GauntletMgr.h"
#include "GauntletRegistry.h"
#include "GauntletSummons.h"
#include "Chat.h"
#include "Creature.h"
#include "Group.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"

using namespace Gauntlet;

// The .gauntlet tree moved to GauntletCommands.cpp with step 8, so plan
// section 2.6's "hook adapters only" is what is left here. The command script
// still has to be constructed from Addmod_gauntletScripts below -- the core's
// generated module loader calls that one name and nothing else -- so the seam
// is the free function AzerothCore uses for exactly this, declared here and
// defined beside the commands it registers.
void AddSC_gauntlet_commands();

// The same seam again, for the shared summon AI and the two creature scripts
// that keep it honest. Without this call the templates' ScriptName resolves to
// no AI at all: the module is archived into libmodules.a and linked plainly, so
// GauntletSummonAI.cpp, which nothing else references, is simply dropped and
// every creature this module spawns is an inert mob standing where it spawned.
void AddSC_gauntlet_summons();

static char const* GAUNTLET_RETIRED_MSG =
    "Your Gauntlet run has ended. This character is retired.";

class GauntletWorldScript : public WorldScript
{
public:
    GauntletWorldScript() : WorldScript("GauntletWorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sGauntlet->LoadConfig();
    }
};

class GauntletPlayerScript : public PlayerScript
{
public:
    GauntletPlayerScript() : PlayerScript("GauntletPlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->IsEligible(player))
            return;

        sGauntlet->Load(player);
        RunState* st = sGauntlet->Get(player);
        if (!st)
            return;

        // HELLO and the snapshot go out before the chat lines and before the
        // retired check: an addon that is listening wants the tombstone drawn
        // too, and the run line below is the fallback for one that is not.
        sGauntletAddon->OnLogin(player);

        ChatHandler ch(player->GetSession());
        if (st->dead)
        {
            ch.PSendSysMessage("|cffff2020[Gauntlet]|r {}", GAUNTLET_RETIRED_MSG);
            return;
        }

        ch.PSendSysMessage("|cffff2020[Gauntlet]|r Run seed |cffffff00{}|r - tier {} - {} affix(es) borne.",
                           st->seed, st->tier, uint32(st->affixes.size()));

        // A tier may have been reached while offline, or left unpicked.
        uint32 const due = player->GetLevel() / sGauntlet->Interval();
        if (due > st->tier)
        {
            sGauntlet->OfferTier(player, st->tier + 1);
            sGauntletAddon->SendOffers(player);
        }
    }

    // The core reuses the GUIDs of deleted characters, so a run keyed on the
    // GUID alone is inherited by whoever is created next -- retired flag,
    // tier and every affix. Fired from Player::DeleteFromDB
    // (Player.cpp:4384) inside the same transaction that removes the
    // character's own rows, so the two cannot come apart.
    void OnPlayerDeleteFromDB(CharacterDatabaseTransaction trans, uint32 guid) override
    {
        sGauntlet->PurgeCharacter(guid, trans);
    }

    void OnPlayerLogout(Player* player) override
    {
        // Unconditional and first: the addon's per-player state is keyed by
        // guid and must not outlive the session that owns it, and eligibility
        // is a config-dependent answer that may have changed since login.
        sGauntletAddon->Forget(player->GetGUID());

        if (!sGauntlet->IsEligible(player))
            return;

        // Logging out inside the death window used to be impossible, because
        // the run ended in OnPlayerJustDied. Now that it is a timer, quitting
        // while it runs would leave a dead character with a live run, so the
        // timer is settled here rather than abandoned.
        if (sGauntlet->IsPendingDeath(player))
            sGauntlet->EndRun(player, "slain");

        sGauntlet->Save(player);

        // CONTRACT-P1 section 2.4's first despawn path, and the one it calls
        // the worst failure this phase can produce: a creature left standing
        // after its owner has gone. It runs before DetachAll so that a
        // mechanic's own OnDetach finds nothing left to take out.
        sGauntletSummons->DespawnAll(player);

        // OnDetach is documented as firing on swap, logout and death. Logout
        // is here, the swap is in Mgr::Pick, and death deliberately is not:
        // the character stays in the world with its affixes listed by
        // `.gauntlet status`, and tearing them down at the moment of death
        // would erase the run's own record of itself while the player is still
        // looking at it. The logout that follows always settles it.
        sGauntlet->DetachAll(player);
        sGauntlet->Forget(player->GetGUID());
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->IsEligible(player))
            return;

        RunState* st = sGauntlet->Get(player);
        if (!st || st->dead)
            return;

        uint32 const due = player->GetLevel() / sGauntlet->Interval();
        if (due > st->tier)
        {
            sGauntlet->OfferTier(player, st->tier + 1);
            sGauntletAddon->SendOffers(player);
        }
    }

    // Death no longer ends the run here (plan section 6, decision 5): it arms
    // a timer, so that a Phase 3 bargain -- Ankh Pact, Stone of the Damned --
    // has a window in which to cancel it. Releasing the corpse or letting the
    // timer expire is what ends the run, which is Blizzard's own rule.
    void OnPlayerJustDied(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->IsEligible(player))
            return;

        // KILLBY, the empty queue, the despawns and the state write, all of
        // which are owed whether or not the realm is hardcore.
        sGauntlet->OnDied(player);

        if (sGauntlet->Hardcore())
            sGauntlet->BeginPendingDeath(player);
    }

    void OnPlayerReleasedGhost(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->Hardcore() || !sGauntlet->IsEligible(player))
            return;

        if (sGauntlet->IsPendingDeath(player))
        {
            sGauntlet->EndRun(player, "slain");

            // Beyond the plan's table, which lists RUN at login and after a
            // pick. Without it the addon goes on drawing a retired run as
            // alive until the next login, which is the one moment the panel
            // has to be right about.
            sGauntletAddon->SendRun(player);
        }
    }

    // The seam the cancel path will use. Player::ResurrectPlayer is the one
    // entry point (Player.cpp:4546) and it consults OnPlayerCanResurrect at
    // the top (Player.cpp:4548) before firing this at the bottom
    // (Player.cpp:4597), so in Phase 0 this cannot be reached: the veto below
    // has already refused.
    //
    // It deliberately does not call CancelPendingDeath. Decision 5 cancels a
    // pending death *with a bargain charge*, and Phase 0 has no charge to
    // spend -- Last Rites and Ankh Pact are Phase 3. Cancelling unconditionally
    // would mean that any future path reaching here, or any relaxation of the
    // veto, silently stops the module being hardcore. Phase 3 spends the
    // charge here and calls Mgr::CancelPendingDeath.
    void OnPlayerResurrect(Player* /*player*/, float /*restorePercent*/, bool& /*applySickness*/) override
    {
    }

    // The module's one clock. Everything with a cadence hangs off it: the
    // hardcore death timer, the grace window, the scheduler, IMechanic::OnTick
    // and the periodic state write. This hook is the world tick, not a fixed
    // cadence -- it fires once per World::Update, which MinWorldUpdateTime
    // leaves at roughly a millisecond and load stretches -- so everything below
    // accumulates `diff` rather than assuming an interval.
    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (sGauntlet->Enabled() && sGauntlet->IsEligible(player))
        {
            if (sGauntlet->AnyPendingDeath())
                sGauntlet->UpdateDeathTimer(player, diff);

            sGauntlet->Tick(player, diff);
        }

        // Addon::Update returns on an integer test whenever nothing anywhere is
        // queued, so it stays outside the eligibility check it makes itself.
        sGauntletAddon->Update(player, diff);
    }

    // AzerothCore exposes proper veto hooks, so the run simply cannot be
    // revived - no kicking, no re-killing, no disconnection.
    //
    // The veto now covers the death window as well as the finished run. It has
    // to: before decision 5 the run was already over by the time anything
    // could offer a resurrection, and leaving the window open would let a
    // hardcore character be raised out of a death that has not resolved yet.
    // Phase 3 is what makes the window mean something, by spending a charge
    // here instead of refusing.
    bool OnPlayerCanResurrect(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->Hardcore() || !sGauntlet->IsEligible(player))
            return true;

        RunState* st = sGauntlet->Get(player);
        if (st && (st->dead || st->pendingDeath))
        {
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cffff2020[Gauntlet]|r {}", GAUNTLET_RETIRED_MSG);
            return false;
        }
        return true;
    }

    // Releasing, by contrast, stays allowed while the timer runs: it is the
    // action that ends the run, and vetoing it would strand the corpse.
    bool OnPlayerCanRepopAtGraveyard(Player* player) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->Hardcore() || !sGauntlet->IsEligible(player))
            return true;

        RunState* st = sGauntlet->Get(player);
        return !(st && st->dead);
    }

    // The client half of the GNT channel. The addon whispers itself with
    // SendAddonMessage("GNT", ...), which reaches Player::Whisper
    // ($CORE/src/server/game/Entities/Player/Player.cpp:9680) and this hook
    // before the packet is built, so returning false both acts on the message
    // and stops it ever being sent back out.
    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg,
                            Player* /*receiver*/) override
    {
        if (lang != uint32(LANG_ADDON))
            return true;

        // False only for a frame the module recognised; every other addon's
        // traffic passes through untouched.
        return !sGauntletAddon->HandleIncoming(player, msg);
    }

    // The experience path: IMechanic::OnXP over the carried set, then
    // Gauntlet.Summons.XpRate for a kill on a creature this module spawned,
    // then the Experience aggregate. Mgr::OnGiveXP is the order.
    // Blood Magic's only hook. It fires once per cast that gets as far as
    // being cast (PlayerScript.h:331), which is where a cost is paid; the
    // mechanic reads the spell's own power cost to decide whether this was a
    // spell at all, so a wand swing and an auto-attack cost nothing.
    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->IsEligible(player))
            return;

        sGauntlet->OnSpellCast(player, spell);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* victim, uint8 /*source*/) override
    {
        sGauntlet->OnGiveXP(player, amount, victim);
    }

    // The out-of-combat edge, and only that edge. CombatManager.cpp:423 is the
    // one call site in the core, inside UpdateOwnerCombatState, which returns
    // early unless the combat state actually changed (:412-413) and sets
    // UNIT_FLAG_IN_COMBAT at :417, six lines before the hook. That is what makes
    // Champions a counter of fights rather than of creatures, and it is why
    // wasOutOfCombat is passed as true rather than read off the player: by the
    // time the hook runs, IsInCombat() is already the flag set at :417.
    void OnPlayerEnterCombat(Player* player, Unit* enemy) override
    {
        sGauntlet->OnEnterCombat(player, enemy);
    }

    // The core has two call sites for this one -- CombatManager.cpp:433 and
    // Unit::ClearInCombat at Unit.cpp:10714 -- so it can be reached twice for
    // one exit and every mechanic behind it has to be idempotent. Champions'
    // Restore() and Falling Sky's Disarm() both are.
    void OnPlayerLeaveCombat(Player* player) override
    {
        sGauntlet->OnLeaveCombat(player);
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        sGauntlet->OnCreatureKill(killer, killed, false);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        sGauntlet->OnCreatureKill(owner, killed, true);
    }

    // Plan section 2.5's floor on maximum health, and Deep Wounds' wound, both
    // applied here and the floor exactly once over the finished value.
    // PlayerScript.h:476; Player::UpdateMaxHealth rebuilds `value` from the stat
    // chain on every call, so nothing here compounds.
    void OnPlayerAfterUpdateMaxHealth(Player* player, float& value) override
    {
        sGauntlet->OnMaxHealth(player, value);
    }

    // Champions' guaranteed extra coin roll. PlayerScript.h:290, "called before
    // looted money is added to a player", which is after the server's money
    // rate has already been applied -- so doubling the purse is the honest
    // reading of the card and re-rolling mingold..maxgold is not.
    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override
    {
        sGauntlet->OnLootMoney(player, loot);
    }

    // Carrion counts the corpses whose loot window the owner opens, which is
    // this hook and not the money one: a corpse with no coin on it is still a
    // corpse that was rifled. Player.cpp:8369, inside SendLoot, after the
    // permission check and before the packet is built, so a window the player
    // is not allowed to open never reaches it.
    void OnPlayerBeforeSendLoot(Player* player, ObjectGuid lootGuid, Loot* loot) override
    {
        sGauntlet->OnLootWindow(player, lootGuid, loot);
    }

    // The grace window re-opens on a zone change, and every summon comes out of
    // the world with it (CONTRACT-P1 section 2.4).
    // The three refusals Self-found is made of. All three are [[nodiscard]]
    // veto hooks (PlayerScript.h:628, :512, :448) where false stops the
    // action, and the module answers true for everyone carrying nothing that
    // refuses -- which is every player on the realm but a handful.
    //
    // The mechanic writes the chat line, not this adapter: it is the one that
    // knows which affix refused and why, and a veto the player cannot account
    // for is indistinguishable from a broken button.
    bool OnPlayerCanInitTrade(Player* player, Player* /*target*/) override
    {
        return sGauntlet->Allows(player, Restricted::Trade);
    }

    bool OnPlayerCanSendMail(Player* player, ObjectGuid /*receiverGuid*/, ObjectGuid /*mailbox*/,
                             std::string& /*subject*/, std::string& /*body*/, uint32 /*money*/,
                             uint32 /*COD*/, Item* /*item*/) override
    {
        return sGauntlet->Allows(player, Restricted::Mail);
    }

    bool OnPlayerCanPlaceAuctionBid(Player* player, AuctionEntry* /*auction*/) override
    {
        return sGauntlet->Allows(player, Restricted::AuctionBid);
    }

    // Iron Purse. discountMod arrives as the reputation price discount and the
    // core spends it as a multiplier on the bill (NPCHandler.cpp:780-782,
    // Player.cpp:4955), so a mechanic makes repairs dearer by raising it.
    void OnPlayerBeforeDurabilityRepair(Player* player, ObjectGuid /*npcGUID*/,
                                        ObjectGuid /*itemGUID*/, float& discountMod,
                                        uint8 /*guildBank*/) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->IsEligible(player))
            return;

        sGauntlet->OnRepair(player, discountMod);
    }

    void OnPlayerUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
    {
        if (!sGauntlet->Enabled() || !sGauntlet->IsEligible(player))
            return;

        sGauntlet->OnZoneChanged(player);
    }
};

class GauntletUnitScript : public UnitScript
{
public:
    GauntletUnitScript() : UnitScript("GauntletUnitScript", true) { }

    // The three Modify* hooks are where the aggregate is *applied*, and they
    // now pass the other unit and the spell through, because Mgr::AggregateAt
    // folds IMechanic::DamageTakenMult and its two siblings into the product
    // before the cap. Champions' +25% is one of those, and applied after the
    // cap it would sail straight past the 2.0x ceiling.
    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        if (Player* victim = target ? target->ToPlayer() : nullptr)
            damage = uint32(damage * sGauntlet->AggregateAt(victim, AggregateKind::DamageTaken, attacker, nullptr));
        if (Player* dealer = attacker ? attacker->ToPlayer() : nullptr)
            damage = uint32(damage * sGauntlet->AggregateAt(dealer, AggregateKind::DamageDone, target, nullptr));
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (Player* victim = target ? target->ToPlayer() : nullptr)
            damage = int32(damage * sGauntlet->AggregateAt(victim, AggregateKind::DamageTaken, attacker, spellInfo));
        if (Player* dealer = attacker ? attacker->ToPlayer() : nullptr)
            damage = int32(damage * sGauntlet->AggregateAt(dealer, AggregateKind::DamageDone, target, spellInfo));
    }

    void ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellInfo const* spellInfo) override
    {
        Player* p = target ? target->ToPlayer() : nullptr;
        if (!p)
            return;

        heal = uint32(heal * sGauntlet->AggregateAt(p, AggregateKind::HealTaken, healer, spellInfo));

        // And then the absolute limits, which a multiplier cannot express.
        // Last Rites' Mark says "you cannot be healed above half", and half is
        // a place rather than a fraction of the incoming heal.
        sGauntlet->OnHeal(p, heal);
    }

    // The only place IMechanic::OnDamageTaken is dispatched from, and
    // deliberately not the three hooks above. This one runs once per blow, from
    // Unit::DealDamage ($CORE/src/server/game/Entities/Unit/Unit.cpp:984),
    // after absorbs and resists have already come off and before the health is
    // applied -- which is what the card means by "after mitigation", and what
    // lets Deep Wounds see the health the blow is about to come out of and so
    // refuse to make a wound out of overkill. Dispatching from the Modify*
    // hooks as well would count every hit twice.
    //
    // The damage is returned exactly as it arrived: this is an observer, and
    // the multipliers have already been applied above.
    uint32 DealDamage(Unit* attacker, Unit* victim, uint32 damage, DamageEffectType /*type*/) override
    {
        Player* p = victim ? victim->ToPlayer() : nullptr;
        if (!p)
            return damage;

        // The cheat-death clamp comes first, and the observer sees what is
        // left. Unit::DealDamage takes this return value straight back
        // ($CORE/src/server/game/Entities/Unit/Unit.cpp:984) and ScriptMgr
        // chains every registered script's answer
        // (ScriptDefines/UnitScript.cpp:52-64), so a blow reduced here really
        // is the blow that lands.
        //
        // Order matters and this is the whole reason it does: Deep Wounds
        // wounds off the damage taken, and if it observed the pre-clamp figure
        // a Last Rites save would hand it a wound made out of overkill that
        // never happened -- turning the affix that saved the run into the one
        // that ends it.
        damage = sGauntlet->OnLethal(p, damage);

        sGauntlet->OnDamageTaken(p, attacker, damage);
        return damage;
    }

    // The other side of the same blow, and the only hook in the core that can
    // see a creature's health *about* to cross a threshold: OnDamage runs at
    // Unit.cpp:999, twenty-five lines before the health is applied, with the
    // damage already final. Craven's "flees the first time it drops below 25%"
    // is exactly that subtraction and cannot be written anywhere else.
    //
    // The credited player is the attacker, or the attacker's owner when a pet,
    // totem or guardian struck the blow -- an affix that keys on the owner's
    // fights must not be blind to the half of them a hunter's pet fights.
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        Creature* creature = victim ? victim->ToCreature() : nullptr;
        if (!creature || !attacker || !damage)
            return;

        Player* owner = attacker->ToPlayer();
        if (!owner)
            owner = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();   // Unit.h:1295
        if (!owner)
            return;

        sGauntlet->OnCreatureDamaged(owner, creature, damage);
    }
};

// Loot rolls, for the one boon that is a drop rate rather than an amount.
// GlobalScript::OnItemRoll is consulted once per candidate item per loot per
// player (LootMgr.cpp:315 and :1276) and passes the chance by reference; a
// false return drops the item outright, which this module never wants, so it
// always answers true.
class GauntletGlobalScript : public GlobalScript
{
public:
    GauntletGlobalScript() : GlobalScript("GauntletGlobalScript") { }

    bool OnItemRoll(Player const* player, LootStoreItem const* /*item*/, float& chance,
                    Loot& /*loot*/, LootStore const& /*store*/) override
    {
        sGauntlet->OnItemRoll(player, chance);
        return true;
    }

    // Where a chest's contents are actually decided, and therefore where
    // "chests hold twice the loot" has to be written. Consulted once per loot
    // group per fill (GlobalScript.h:66) with the group's item count by
    // reference.
    void OnAfterCalculateLootGroupAmount(Player const* player, Loot& /*loot*/, uint16 /*lootMode*/,
                                         uint32& groupAmount, LootStore const& /*store*/) override
    {
        sGauntlet->OnLootGroupAmount(player, groupAmount);
    }
};

// The catch-all for "the owner is no longer where its creature is": a
// teleport, a dungeon portal, a battleground queue popping, a logout. The
// summons worker called this the important one, and it is: every other despawn
// path is a specific event this module happens to hook, and this one is the
// core telling us the player has left the map the creature is standing on.
// Map::RemovePlayerFromMap fires it for all of them.
// Group membership, for the one affix that cares. The module had no
// GroupScript at all before Phase 3.
//
// Why observation and not a veto: OnPlayerCanGroupInvite is the *inviter's*
// hook (PlayerScript.h:506) and never sees the player being invited, so even
// the card's original "you cannot join a group" could not have been written as
// a refusal on the joining side. What the core does offer is the fact of
// membership after it changes, which is exactly what a live, reversible
// penalty needs.
//
// All three hooks do the same thing, because all three change the answer to
// "am I in a group": OnAddMember and OnRemoveMember for a join and a leave
// (GroupScript.h:48, :54), and OnDisband for the case nothing else covers --
// the last member leaving takes the group with it and no OnRemoveMember is
// sent for the player left holding it.
class GauntletGroupScript : public GroupScript
{
public:
    GauntletGroupScript() : GroupScript("GauntletGroupScript", { GROUPHOOK_ON_ADD_MEMBER,
                                                                GROUPHOOK_ON_REMOVE_MEMBER,
                                                                GROUPHOOK_ON_DISBAND }) { }

    void OnAddMember(Group* /*group*/, ObjectGuid guid) override { Touch(guid); }

    void OnRemoveMember(Group* /*group*/, ObjectGuid guid, RemoveMethod /*method*/,
                        ObjectGuid /*kicker*/, char const* /*reason*/) override
    {
        Touch(guid);
    }

    // Every member at once, and the group is still whole when this fires, so
    // the members are still reachable through it.
    void OnDisband(Group* group) override
    {
        if (!group)
            return;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                sGauntlet->OnGroupChanged(member);
    }

private:
    // Offline members are not a problem to solve here: the penalty is read off
    // the live group at the next UpdateMaxHealth, and logging in runs the whole
    // stat chain anyway.
    static void Touch(ObjectGuid guid)
    {
        if (Player* player = ObjectAccessor::FindPlayer(guid))
            sGauntlet->OnGroupChanged(player);
    }
};

class GauntletMapScript : public AllMapScript
{
public:
    GauntletMapScript() : AllMapScript("GauntletMapScript") { }

    void OnPlayerLeaveAll(Map* /*map*/, Player* player) override
    {
        sGauntletSummons->DespawnAll(player);
    }
};

// The addon's half of "a stalker is alive for you". Summons calls this whenever
// a creature it owns appears or disappears, including the despawns no mechanic
// initiated -- a cap eviction, a leash, a zone change -- which is exactly the
// set the mechanics cannot see for themselves. It is deliberately narrowed to
// MF_Stalker rows: SUMMON means "something is hunting you", and Falling Sky's
// invisible ground trigger appearing every twenty seconds is not that.
//
// The owner arrives as a guid because a summon frequently outlives its owner's
// session by the few milliseconds it takes to despawn it.
static void GauntletSummonChanged(ObjectGuid ownerGuid, uint16 mechanic, uint32 /*entry*/, bool alive)
{
    MechanicDef const* def = FindMechanic(mechanic);
    if (!def || !(def->flags & MF_Stalker))
        return;

    if (Player* owner = ObjectAccessor::FindPlayer(ownerGuid))
        sGauntletAddon->SendSummon(owner, def->key, alive);
}

// Static-archive anchors. The module is archived into libmodules.a and linked
// plainly, so a translation unit nothing references is dropped and the
// GAUNTLET_MECHANIC registrar inside it never runs -- leaving MakeMechanic to
// answer nullptr for a mechanic whose source is right there, and the affix to
// be offered and do nothing. Measured with the module's own registrar and the
// four Player-free scalars archived exactly this way: 0 of 4 registered without
// the anchors, 4 of 4 with them. There are eight now, and the four added for
// Phase 1 cannot be measured on this machine because they need Player.h -- but
// an anchor name that does not match its GAUNTLET_MECHANIC invocation is an
// undefined symbol at link, not another silent nullptr. Every macro defines one
// of these; every one has to be named here. See the comment on
// GAUNTLET_MECHANIC.
// Declared in namespace Gauntlet because that is where each mechanic file
// invokes the macro, and the macro defines the anchor wherever it is invoked.
namespace Gauntlet
{
    // Phase 1's four, declared through GAUNTLET_MECHANIC, so the name carries
    // the class. Registry ids 1, 6, 14 and 19.
    void AddSC_gauntlet_mechanic_Shade();
    void AddSC_gauntlet_mechanic_Champions();
    void AddSC_gauntlet_mechanic_FallingSky();
    void AddSC_gauntlet_mechanic_DeepWounds();

    // Phase 2's fifteen. tests/compile-check.sh audits this list against every
    // GAUNTLET_MECHANIC in src/, in both directions, before it compiles
    // anything -- which is the only cheap way to catch the failure this whole
    // apparatus exists for.
    void AddSC_gauntlet_mechanic_Echo();               // 2
    void AddSC_gauntlet_mechanic_Carrion();            // 3
    void AddSC_gauntlet_mechanic_Reinforcements();     // 4
    void AddSC_gauntlet_mechanic_Ambush();             // 5
    void AddSC_gauntlet_mechanic_Craven();             // 7
    void AddSC_gauntlet_mechanic_CallToArms();         // 8
    void AddSC_gauntlet_mechanic_DeathRattle();        // 9
    void AddSC_gauntlet_mechanic_Grudge();             // 10
    void AddSC_gauntlet_mechanic_Nimble();             // 11
    void AddSC_gauntlet_mechanic_Cunning();            // 12
    void AddSC_gauntlet_mechanic_KeenNosed();          // 13
    void AddSC_gauntlet_mechanic_Frenzy();             // 15
    void AddSC_gauntlet_mechanic_Overextended();       // 16
    void AddSC_gauntlet_mechanic_Falter();             // 17
    void AddSC_gauntlet_mechanic_Hubris();             // 18

    void AddSC_gauntlet_mechanic_BloodMagic();         // 20
    void AddSC_gauntlet_mechanic_LastRites();          // 26
    void AddSC_gauntlet_mechanic_CursedHoard();        // 27
    void AddSC_gauntlet_mechanic_SelfFound();          // 23
    void AddSC_gauntlet_mechanic_LoneWolf();           // 24
    void AddSC_gauntlet_mechanic_IronPurse();          // 25
}

static void AnchorMechanics()
{
    AddSC_gauntlet_mechanic_Shade();
    AddSC_gauntlet_mechanic_Champions();
    AddSC_gauntlet_mechanic_FallingSky();
    AddSC_gauntlet_mechanic_DeepWounds();

    AddSC_gauntlet_mechanic_Echo();
    AddSC_gauntlet_mechanic_Carrion();
    AddSC_gauntlet_mechanic_Reinforcements();
    AddSC_gauntlet_mechanic_Ambush();
    AddSC_gauntlet_mechanic_Craven();
    AddSC_gauntlet_mechanic_CallToArms();
    AddSC_gauntlet_mechanic_DeathRattle();
    AddSC_gauntlet_mechanic_Grudge();
    AddSC_gauntlet_mechanic_Nimble();
    AddSC_gauntlet_mechanic_Cunning();
    AddSC_gauntlet_mechanic_KeenNosed();
    AddSC_gauntlet_mechanic_Frenzy();
    AddSC_gauntlet_mechanic_Overextended();
    AddSC_gauntlet_mechanic_Falter();
    AddSC_gauntlet_mechanic_Hubris();

    AddSC_gauntlet_mechanic_BloodMagic();
    AddSC_gauntlet_mechanic_LastRites();
    AddSC_gauntlet_mechanic_CursedHoard();
    AddSC_gauntlet_mechanic_IronPurse();
    AddSC_gauntlet_mechanic_SelfFound();
    AddSC_gauntlet_mechanic_LoneWolf();
}

void Addmod_gauntletScripts()
{
    new GauntletWorldScript();
    new GauntletPlayerScript();
    new GauntletUnitScript();
    new GauntletMapScript();
    new GauntletGlobalScript();
    new GauntletGroupScript();
    AddSC_gauntlet_commands();
    AddSC_gauntlet_summons();
    AnchorMechanics();

    // Installed before anything can summon, so no appearance is missed.
    sGauntletSummons->SetObserver(&GauntletSummonChanged);
}
