/*
 * mod-gauntlet - the commons: one class behind every "lose X, gain Y" row
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletMechanic.h"

#include "GauntletRegistry.h"
#include "GauntletTrades.h"
#include "../Boons.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Language.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "WorldSession.h"

#include <string>

// Registry ids 75-84 and every Rarity::Common row after them. docs/rarity-plan.md
// section 2: a common is "one small trade -- lose X, gain Y, single axis, no
// state", and section 3's point is that sixty of them should be a table rather
// than sixty files. This is the one file. Each row's line in
// src/GauntletTrades.h says what it takes; the registry row's Boon says what
// it pays; and the same class reads both.
//
// Three kinds of taking, two of them here and one already elsewhere:
//
//   A coefficient is reported through AggregateFactor like Nimble's, so the
//   cap is applied once to the product by the one place that owns it.
//
//   A denial is answered from CanEquip, the equipment veto the core consults
//   before anything else in Player::CanEquipItem (PlayerStorage.cpp:1912).
//   The core turns a refusal into EQUIP_ERR_CANT_DO_RIGHT_NOW, which the
//   client shows as "You can't do that right now" and names nothing, so the
//   line saying which card and why is owed from here -- the same rule Allows()
//   works under for the economy vetoes.
//
//   The boon is not this file's at all. BoonFactor pays damage, experience and
//   health through the aggregate, BoonHealMult pays healing through
//   HealTakenMult, and BoonSpeed::Sync pays movement speed from
//   Mgr::RefreshStats. Every one of those existed before the first common did.
//
// A denial also has to bite what is already worn, or a warrior holding an axe
// who takes "you cannot wield an axe" has paid nothing. OnAttach puts a denied
// item into the bags the way the core itself does when a weapon stops being
// legal -- Player::AutoUnequipOffhandIfNeed, Player.cpp:12728 -- bags first
// and the mailbox when they are full, so nothing is ever destroyed. What was
// put away is remembered in the run's state store, and OnDetach puts it back
// on if it can: anything that takes something must give it back, and only what
// it took.

namespace Gauntlet
{
    namespace
    {
        // How many stripped items one card remembers. Two is the most any
        // denial takes off at once -- the rings -- and the keys are
        // "<registry key>.took0" and ".took1", well inside State::MaxKeyLen.
        constexpr uint32 MAX_TOOK = 2;

        // A second refusal of the same item inside this window says nothing.
        // The client asks CanEquipItem more than once for one click, and a
        // playerbot's kit logic asks constantly; one line per attempt is the
        // point, one per query would be a wall.
        constexpr uint32 TOLD_WINDOW_MS = 2000;

        class SimpleTrade final : public IMechanic
        {
        public:
            explicit SimpleTrade(TradeDef const& def) : _def(def) { }

            void OnAttach(Ctx& ctx) override;
            void OnDetach(Ctx& ctx) override;
            bool CanEquip(Ctx& ctx, ItemTemplate const* proto) override;

            // Curse and boon in one number per kind, as the aggregate expects
            // (GauntletAggregate.cpp). A coefficient line multiplies its own
            // kind; the boon rides on whichever kind BoonFactor pays; a denial
            // contributes only the boon here, because its cost is a slot.
            float AggregateFactor(AffixInstance const& self, AggregateKind kind) const override
            {
                float factor = BoonFactor(self, kind);
                if (_def.curse == TradeCurse::Coefficient && kind == _def.kind)
                    factor *= TradeFactor(_def);
                return factor;
            }

            // Boon::BonusHealing is paid here and not through the aggregate;
            // see Boons.h. A HealTaken *curse* is a coefficient and goes
            // through AggregateFactor above like any other.
            float HealTakenMult(Ctx& ctx, Unit*, SpellInfo const*) override
            {
                return ctx.self ? BoonHealMult(*ctx.self) : 1.0f;
            }

            std::string Describe(AffixInstance const& self) const override
            {
                return std::string(_def.text) + BoonClause(self.boon, self.boonMag);
            }

            std::string Diagnose(Ctx&) const override
            {
                std::string out = "trade: refused " + std::to_string(_refused) + " equip attempt(s), put away "
                                + std::to_string(_stripped) + " item(s), put back " + std::to_string(_returned);
                if (_returnRefused != 0)
                    out += ", " + std::to_string(_returnRefused) + " return(s) refused by the core (last error "
                         + std::to_string(_lastRefusal) + ")";
                return out;
            }

        private:
            bool        Denies(ItemTemplate const* proto) const;
            bool        PutAway(Player* player, uint8 slot, Item* item) const;
            std::string TookKey(uint32 i) const;
            char const* Name() const;

            TradeDef const& _def;

            // Up while OnDetach re-equips what OnAttach took: the card is still
            // in the carried set at that moment (Mgr::Pick detaches *after*
            // OnDetach), so without this its own veto would refuse the item on
            // the way back on.
            bool   _detaching  = false;

            uint32 _refused       = 0;
            uint32 _stripped      = 0;
            uint32 _returned      = 0;
            uint32 _returnRefused = 0;
            uint32 _lastRefusal   = 0;   // the InventoryResult the core answered
            uint32 _lastEntry     = 0;
            uint32 _lastToldMs    = 0;
        };

        char const* SimpleTrade::Name() const
        {
            MechanicDef const* def = FindMechanic(_def.id);
            return def ? def->name : "A trade";
        }

        std::string SimpleTrade::TookKey(uint32 i) const
        {
            MechanicDef const* def = FindMechanic(_def.id);
            return std::string(def ? def->key : "trade") + ".took" + std::to_string(i);
        }

        bool SimpleTrade::Denies(ItemTemplate const* proto) const
        {
            if (!proto)
                return false;

            switch (_def.curse)
            {
                case TradeCurse::DenyInventoryType:
                    return (_def.mask & InvTypeBit(proto->InventoryType)) != 0;
                case TradeCurse::DenyWeaponSubclass:
                    return proto->Class == ITEM_CLASS_WEAPON
                        && (_def.mask & WeaponBit(proto->SubClass)) != 0;
                default:
                    return false;
            }
        }

        bool SimpleTrade::CanEquip(Ctx& ctx, ItemTemplate const* proto)
        {
            if (_detaching || !Denies(proto))
                return true;

            ++_refused;

            uint32 const now = static_cast<uint32>(GameTime::GetGameTimeMS().count());
            if (ctx.player && ctx.player->GetSession()
                && (proto->ItemId != _lastEntry || now - _lastToldMs > TOLD_WINDOW_MS))
            {
                _lastEntry  = proto->ItemId;
                _lastToldMs = now;
                ChatHandler(ctx.player->GetSession()).PSendSysMessage(
                    "|cffff2020[Gauntlet]|r {} -- {}", Name(), _def.text);
            }

            return false;
        }

        // Player::AutoUnequipOffhandIfNeed (Player.cpp:12752-12770), for one
        // slot of the caller's choosing. True when the item is in a bag and can
        // come back; false when it went to the mailbox.
        bool SimpleTrade::PutAway(Player* player, uint8 slot, Item* item) const
        {
            ItemPosCountVec dest;
            if (player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) == EQUIP_ERR_OK)
            {
                player->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
                player->StoreItem(dest, item, true);
                return true;
            }

            // Bags full. The core's own answer is the mailbox, and it is the
            // right one: the item is the player's and a card must never cost
            // them a thing they own outright, only the wearing of it.
            player->MoveItemFromInventory(INVENTORY_SLOT_BAG_0, slot, true);

            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            item->DeleteFromInventoryDB(trans);
            item->SaveToDB(trans);

            std::string const subject = player->GetSession()->GetAcoreString(LANG_NOT_EQUIPPED_ITEM);
            MailDraft(subject, "There were problems with equipping one or several items")
                .AddItem(item)
                .SendMailTo(trans, player, MailSender(player, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
            CharacterDatabase.CommitTransaction(trans);
            return false;
        }

        void SimpleTrade::OnAttach(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !player->IsInWorld() || _def.curse == TradeCurse::Coefficient)
                return;

            uint32 took = 0;
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!item || !Denies(item->GetTemplate()))
                    continue;

                // Read before PutAway: the mailbox path hands the Item to the
                // mail and nothing here should touch it afterwards.
                std::string const  name = item->GetTemplate()->Name1;
                uint32 const       low  = item->GetGUID().GetCounter();

                bool const bagged = PutAway(player, slot, item);
                ++_stripped;

                if (bagged && ctx.state && took < MAX_TOOK)
                    ctx.state->Set(TookKey(took++), static_cast<int32>(low));

                if (!player->GetSession())
                    continue;

                ChatHandler ch(player->GetSession());
                if (bagged)
                    ch.PSendSysMessage("|cffff2020[Gauntlet]|r {}: your {} goes into your bags.", Name(), name);
                else
                    ch.PSendSysMessage("|cffff2020[Gauntlet]|r {}: your {} would not fit in your bags and has been "
                                       "mailed to you.", Name(), name);
            }
        }

        void SimpleTrade::OnDetach(Ctx& ctx)
        {
            Player* player = ctx.player;
            if (!player || !ctx.state || _def.curse == TradeCurse::Coefficient)
                return;

            _detaching = true;
            for (uint32 i = 0; i < MAX_TOOK; ++i)
            {
                int32 const low = ctx.state->Get(TookKey(i), 0);
                if (low == 0)
                    continue;

                Item* item = player->GetItemByGuid(
                    ObjectGuid::Create<HighGuid::Item>(static_cast<ObjectGuid::LowType>(low)));
                if (!item || item->IsEquipped())
                {
                    ctx.state->Set(TookKey(i), 0);   // sold, mailed, or already back on: nothing is owed
                    continue;
                }

                // Through the core's own check, so a slot filled since, or a
                // second card that refuses, is honoured rather than overwritten.
                //
                // The refusal that actually happens is the world's: armour
                // cannot be put on in combat (EQUIP_ERR_NOT_IN_COMBAT, from
                // ItemTemplate::CanChangeEquipStateInCombat), and a detach can
                // land mid-fight -- the leak audit found exactly that on a bot
                // that was fighting. The item is not lost, only still in the
                // bags, so the guid is *kept* for a later detach to try again,
                // and the player is told, because a helm that quietly stays in
                // the bag reads as the card being broken.
                uint16 dest = 0;
                InventoryResult const res = player->CanEquipItem(NULL_SLOT, dest, item, false);
                if (res != EQUIP_ERR_OK)
                {
                    ++_returnRefused;
                    _lastRefusal = res;
                    if (player->GetSession())
                    {
                        ChatHandler ch(player->GetSession());
                        if (res == EQUIP_ERR_NOT_IN_COMBAT)
                            ch.PSendSysMessage("|cffff2020[Gauntlet]|r {}: your {} stays in your bags -- it cannot be put "
                                               "on in combat.", Name(), item->GetTemplate()->Name1);
                        else
                            ch.PSendSysMessage("|cffff2020[Gauntlet]|r {}: your {} stays in your bags -- it could not be "
                                               "put back on.", Name(), item->GetTemplate()->Name1);
                    }
                    continue;
                }

                ctx.state->Set(TookKey(i), 0);
                player->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
                player->EquipItem(dest, item, true);
                ++_returned;
            }
            _detaching = false;
        }

        // One factory per row, and a null for a row with no line rather than a
        // crash: MakeMechanic's callers already treat "no implementation" as a
        // normal answer, and Trades.EveryCommonRowHasALine is what makes sure
        // it never happens on a shipped table.
        IMechanic* MakeTrade(uint16 id)
        {
            TradeDef const* line = FindTrade(id);
            if (!line)
            {
                LOG_ERROR("module", "Gauntlet: registry id {} is a common with no line in GauntletTrades.h; "
                                    "it will be offered and do nothing.", id);
                return nullptr;
            }
            return new SimpleTrade(*line);
        }
    }

    // Ten registrations, spelled out. GAUNTLET_MECHANIC_FN pastes its second
    // argument into the registrar's and the anchor's names, so a template
    // argument or an index cannot go there, and the anchor audit reads these
    // lines by text. Each anchor is one line in GauntletScripts.cpp, as for
    // every other card; that is the price the macro's design settled on.
    IMechanic* TradeBareheaded() { return MakeTrade(75); }
    IMechanic* TradeCloakless()  { return MakeTrade(76); }
    IMechanic* TradeRingless()   { return MakeTrade(77); }
    IMechanic* TradeCharmless()  { return MakeTrade(78); }
    IMechanic* TradeBareNecked() { return MakeTrade(79); }
    IMechanic* TradeAxeless()    { return MakeTrade(80); }
    IMechanic* TradeSwordless()  { return MakeTrade(81); }
    IMechanic* TradeGlass()      { return MakeTrade(82); }
    IMechanic* TradeFrail()      { return MakeTrade(83); }
    IMechanic* TradeThinBlood()  { return MakeTrade(84); }

    GAUNTLET_MECHANIC_FN(75, TradeBareheaded)
    GAUNTLET_MECHANIC_FN(76, TradeCloakless)
    GAUNTLET_MECHANIC_FN(77, TradeRingless)
    GAUNTLET_MECHANIC_FN(78, TradeCharmless)
    GAUNTLET_MECHANIC_FN(79, TradeBareNecked)
    GAUNTLET_MECHANIC_FN(80, TradeAxeless)
    GAUNTLET_MECHANIC_FN(81, TradeSwordless)
    GAUNTLET_MECHANIC_FN(82, TradeGlass)
    GAUNTLET_MECHANIC_FN(83, TradeFrail)
    GAUNTLET_MECHANIC_FN(84, TradeThinBlood)
}
