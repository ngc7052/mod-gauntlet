/*
 * mod-gauntlet - the .gauntlet command tree
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAddon.h"
#include "GauntletAudit.h"
#include "GauntletBench.h"
#include "GauntletGenerator.h"
#include "GauntletMechanic.h"
#include "GauntletMgr.h"
#include "GauntletRegistry.h"
#include "GauntletWire.h"
#include "GauntletScheduler.h"
#include "GauntletSummons.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Util.h"
#include "WorldSession.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

// Plan section 2.6 splits the module's two script concerns: GauntletScripts.cpp
// keeps the hook adapters and nothing else, and everything a player or a game
// master can type lives here. The registration seam is AddSC_gauntlet_commands
// at the bottom -- the core's own convention for a script that lives in its own
// translation unit -- called from Addmod_gauntletScripts(), which has to stay in
// one place because the generated module loader calls exactly that name.

using namespace Acore::ChatCommands;
using namespace Gauntlet;

// Two of this module's names collide with the core's own globals: `Condition`
// (a class in ConditionMgr.h) and `MECHANIC_NONE` (the spell Mechanics enum in
// SharedDefines.h). Outside the `namespace Gauntlet` block below, an
// unqualified use of either is ambiguous and clang rejects it outright, so
// they are written `Gauntlet::Condition` and `Gauntlet::MECHANIC_NONE` out
// here. Those two are the whole set -- every other name Gauntlet.h exports was
// checked against the core's headers and is unique.

namespace Gauntlet
{
    namespace
    {
        // =============================================================
        // GAUNTLET_EXPORT_BEGIN
        //
        // The Data.lua generator. Everything between this marker and its
        // closing twin below reads nothing but Gauntlet.h, GauntletRegistry.h
        // and GauntletAddon.h -- no Player, no ChatHandler, no database -- so
        // build/export-check/run.sh can cut the block out of this file
        // verbatim, compile it against the real registry and inspect the
        // Data.lua it produces without a running server. The markers exist so
        // that check compiles this source rather than a copy of it, which
        // would be a second thing that can drift.
        // =============================================================

        // TODO(design): every icon below. The design names none and MechanicDef
        // has no icon column, so the whole table is invented. Three layers,
        // narrowest first: a per-mechanic override where a stock icon obviously
        // fits the card; then, for family Class -- 44 of the 73 entries -- the
        // class's own ClassIcon_*, which is a third layer the task did not ask
        // for and is here because one icon shared by 44 curses is unreadable in
        // the addon's browser; then the family baseline. Only paths that ship
        // with an unmodified 3.3.5a client are used, and the addon falls back
        // to INV_Misc_QuestionMark for a texture it cannot load
        // (addon/GauntletUI/Panel.lua:87), so a wrong guess degrades quietly.
        constexpr char const* ICON_FAMILY[static_cast<size_t>(Family::MAX)] =
        {
            "Interface\\Icons\\Spell_Shadow_RaiseDead",        // Spawn
            "Interface\\Icons\\Ability_Warrior_BattleShout",   // Enemy
            "Interface\\Icons\\Spell_Nature_TimeStop",         // Tempo
            "Interface\\Icons\\Spell_Shadow_ShadowWordPain",   // Attrition
            "Interface\\Icons\\INV_Scroll_03",                 // Rules
            "Interface\\Icons\\Spell_Shadow_DemonicPact",      // Bargain
            "Interface\\Icons\\Spell_Holy_MindVision",         // Class
        };

        struct ClassIcon
        {
            uint32      mask;   // 1 << (class - 1), the core's convention
            char const* icon;
        };

        // Only a single-class curse takes one of these; Faint (68) is every
        // mana user and Unspent (69) is every class, and both fall through to
        // the family baseline.
        constexpr ClassIcon ICON_CLASS[] =
        {
            { 1u << (1 - 1),  "Interface\\Icons\\ClassIcon_Warrior"     },
            { 1u << (2 - 1),  "Interface\\Icons\\ClassIcon_Paladin"     },
            { 1u << (3 - 1),  "Interface\\Icons\\ClassIcon_Hunter"      },
            { 1u << (4 - 1),  "Interface\\Icons\\ClassIcon_Rogue"       },
            { 1u << (5 - 1),  "Interface\\Icons\\ClassIcon_Priest"      },
            { 1u << (6 - 1),  "Interface\\Icons\\ClassIcon_DeathKnight" },
            { 1u << (7 - 1),  "Interface\\Icons\\ClassIcon_Shaman"      },
            { 1u << (8 - 1),  "Interface\\Icons\\ClassIcon_Mage"        },
            { 1u << (9 - 1),  "Interface\\Icons\\ClassIcon_Warlock"     },
            { 1u << (11 - 1), "Interface\\Icons\\ClassIcon_Druid"       },
        };

        struct IconOverride
        {
            uint16      id;
            char const* icon;
        };

        // The four Phase 0 scalars deliberately repeat the choices the addon's
        // chat fallback already makes for the same effects (Panel.lua:52-66),
        // so a run looks the same whether Data.lua is loaded or not.
        constexpr IconOverride ICON_MECHANIC[] =
        {
            {  1, "Interface\\Icons\\Spell_Shadow_Shadowfiend"    },   // The Shade
            {  3, "Interface\\Icons\\Ability_Racial_Cannibalize"  },   // Carrion
            {  5, "Interface\\Icons\\Ability_Rogue_Ambush"        },   // Ambush
            {  6, "Interface\\Icons\\Ability_Warrior_Rampage"     },   // Champions
            {  7, "Interface\\Icons\\Ability_Rogue_FeignDeath"    },   // Craven
            {  9, "Interface\\Icons\\INV_Misc_Bone_HumanSkull_01" },   // Death Rattle
            { 10, "Interface\\Icons\\Ability_Warrior_Revenge"     },   // Grudge
            { 11, "Interface\\Icons\\Ability_Rogue_Sprint"        },   // Nimble
            { 12, "Interface\\Icons\\Ability_Kick"                },   // Cunning
            { 13, "Interface\\Icons\\Ability_Tracking"            },   // Keen-nosed
            { 14, "Interface\\Icons\\Spell_Fire_SelfDestruct"     },   // Falling Sky
            { 15, "Interface\\Icons\\Spell_Shadow_UnholyFrenzy"   },   // Frenzy
            { 17, "Interface\\Icons\\INV_Misc_PocketWatch_01"     },   // Falter
            { 19, "Interface\\Icons\\Ability_BackStab"            },   // Deep Wounds
            { 24, "Interface\\Icons\\Ability_Hunter_Pet_Wolf"     },   // Lone Wolf
            { 25, "Interface\\Icons\\INV_Misc_Coin_01"            },   // Iron Purse
            { 26, "Interface\\Icons\\Spell_Holy_LayOnHands"       },   // Last Rites
            { 27, "Interface\\Icons\\INV_Misc_Coin_02"            },   // Cursed Hoard
            { 29, "Interface\\Icons\\Ability_Racial_BloodRage"    },   // Berserker's Bargain
            { 30, "Interface\\Icons\\Ability_Warrior_DefensiveStance" }, // Iron Discipline
            { 33, "Interface\\Icons\\Spell_Holy_RighteousFury"    },   // Consecrated Ground
            { 34, "Interface\\Icons\\Spell_Holy_DivineProtection" },   // No Sanctuary
            { 36, "Interface\\Icons\\Ability_Hunter_BeastTaming"  },   // Half-Tamed
            { 41, "Interface\\Icons\\Ability_Poisons"             },   // Poisoned Blades
            { 44, "Interface\\Icons\\Spell_Holy_PowerWordShield"  },   // Frail Soul
            { 46, "Interface\\Icons\\Spell_Holy_Silence"          },   // Penance of Silence
            { 48, "Interface\\Icons\\INV_Misc_Rune_01"            },   // Rune-starved
            { 50, "Interface\\Icons\\Spell_DeathKnight_FrostPresence" }, // Cold Presence
            { 52, "Interface\\Icons\\Spell_Nature_EarthBind"      },   // One Totem
            { 56, "Interface\\Icons\\Spell_Frost_FrostNova"       },   // Cold Feet
            { 58, "Interface\\Icons\\Spell_Shadow_ManaBurn"       },   // Mana Burn
            { 62, "Interface\\Icons\\Spell_Shadow_SoulGem"        },   // Shard Economy
            { 64, "Interface\\Icons\\Ability_Racial_BearForm"     },   // Bound Skin
            { 67, "Interface\\Icons\\Ability_Druid_CatForm"       },   // Two Faces
            { 70, "Interface\\Icons\\Spell_Nature_Reincarnation"  },   // Ankh Pact
        };

        char const* IconForMechanic(MechanicDef const& def)
        {
            for (IconOverride const& o : ICON_MECHANIC)
                if (o.id == def.id)
                    return o.icon;

            if (def.family == Family::Class)
                for (ClassIcon const& c : ICON_CLASS)
                    if (def.classMask == c.mask)
                        return c.icon;

            size_t const f = static_cast<size_t>(def.family);
            return f < static_cast<size_t>(Family::MAX) ? ICON_FAMILY[f]
                                                        : "Interface\\Icons\\INV_Misc_QuestionMark";
        }

        // One Lua 5.1 string literal. The backslashes are the point: every icon
        // path carries two of them and the addon loads Data.lua as source, so
        // an unescaped "\I" is a syntax error rather than a wrong texture.
        // Anything outside printable ASCII leaves as a three-digit decimal
        // escape -- three digits and not fewer, because Lua reads up to three
        // and "\9" followed by a "7" in a blurb would become "\97".
        std::string LuaQuote(std::string_view s)
        {
            std::string out;
            out.reserve(s.size() + 2);
            out += '"';

            for (char const ch : s)
            {
                unsigned char const c = static_cast<unsigned char>(ch);
                switch (c)
                {
                    case '\\': out += "\\\\"; continue;
                    case '"':  out += "\\\""; continue;
                    case '\n': out += "\\n";  continue;
                    case '\r': out += "\\r";  continue;
                    case '\t': out += "\\t";  continue;
                    default:   break;
                }

                if (c < 0x20 || c >= 0x7F)
                {
                    char buf[5];
                    buf[0] = '\\';
                    buf[1] = static_cast<char>('0' + (c / 100));
                    buf[2] = static_cast<char>('0' + ((c / 10) % 10));
                    buf[3] = static_cast<char>('0' + (c % 10));
                    buf[4] = '\0';
                    out += buf;
                    continue;
                }

                out += ch;
            }

            out += '"';
            return out;
        }

        std::string LuaIndexedLine(uint32 index, std::string const& value)
        {
            return "        [" + std::to_string(index) + "] = " + value + ",\n";
        }

        // The whole of addon/GauntletUI/Data.lua, in the shape frozen by the
        // worker contract section 11.1. `version` is Addon::Version
        // (src/GauntletAddon.h:39) rather than a literal, because the number
        // the addon checks Data.lua against is the one HELLO announces and the
        // two drifting apart is exactly the failure this file exists to
        // prevent. Ids ascend, so regenerating produces no spurious diff.
        std::string BuildAddonData()
        {
            std::vector<MechanicDef> table = AllMechanics();
            std::sort(table.begin(), table.end(),
                      [](MechanicDef const& a, MechanicDef const& b) { return a.id < b.id; });

            std::string out;
            out.reserve(24 * 1024);

            out += "-- Generated by .gauntlet debug export-addon. Do not edit.\n";
            out += "GauntletData = {\n";
            out += "    version = " + std::to_string(static_cast<uint32>(Addon::Version)) + ",\n";

            out += "    families = {\n";
            for (uint8 f = 0; f < static_cast<uint8>(Family::MAX); ++f)
                out += LuaIndexedLine(f, LuaQuote(FamilyName(static_cast<Family>(f))));
            out += "    },\n";

            out += "    conditions = {\n";
            for (uint8 c = 0; c < static_cast<uint8>(Condition::MAX); ++c)
                out += LuaIndexedLine(c, LuaQuote(ConditionName(static_cast<Condition>(c))));
            out += "    },\n";

            out += "    boons = {\n";
            for (uint8 b = 0; b < static_cast<uint8>(Boon::MAX); ++b)
                out += LuaIndexedLine(b, LuaQuote(BoonName(static_cast<Boon>(b))));
            out += "    },\n";

            // Name and colour together, and the colour from the server rather
            // than a table in the addon: the chat line and the panel must paint
            // the same card the same blue, and two palettes is how they would
            // not.
            out += "    rarities = {\n";
            for (uint8 r = 0; r < static_cast<uint8>(Rarity::MAX); ++r)
                out += LuaIndexedLine(r, "{ name = " + LuaQuote(RarityName(static_cast<Rarity>(r)))
                                         + ", color = " + LuaQuote(RarityColor(static_cast<Rarity>(r))) + " }");
            out += "    },\n";

            out += "    mechanics = {\n";
            for (MechanicDef const& def : table)
            {
                // `key` is the registry key, and the addon needs it because the
                // live channel is keyed on it: EVT, CTR, STAT and SUMMON all
                // carry "frenzy" or "deep_wounds" rather than an id, so Hud.lua
                // cannot find a mechanic's name or icon without a way back from
                // the key to the row. Exporting it here is what keeps the two
                // tables from drifting -- the same reason the names and icons
                // are exported rather than written into the addon by hand.
                out += "        [" + std::to_string(static_cast<uint32>(def.id)) + "] = { name = "
                     + LuaQuote(def.name)
                     + ", key = " + LuaQuote(def.key)
                     + ", family = " + std::to_string(static_cast<uint32>(def.family))
                     + ", rarity = " + std::to_string(static_cast<uint32>(def.rarity)) + ",\n"
                     + "                 icon = " + LuaQuote(IconForMechanic(def))
                     + ", desc = " + LuaQuote(def.blurb) + " },\n";
            }
            out += "    },\n";
            out += "}\n";
            return out;
        }

        // =============================================================
        // GAUNTLET_EXPORT_END
        // =============================================================

        // Plan section 2.5: `.gauntlet status` prints the current products, so
        // a player can read the ceiling they are against rather than infer it
        // from six affix descriptions. Deliberately not numbered: the addon's
        // chat fallback scrapes any line beginning "<n>. " as an affix
        // (addon/GauntletUI/Panel.lua:430).
        // What the run's timed affixes are doing to each other's timing.
        //
        // "Cadence" is this codebase's word for how often a timed affix acts --
        // the "every 20 seconds" on its card. It is a fine word in a comment and
        // a bad one in a chat line, which is what the player-facing text below
        // learned the hard way: the person who commissioned the module had to
        // ask what it meant.
        //
        // Each timed affix's blurb states the interval its own mechanic asks
        // for, because that is the only number a mechanic knows. The scheduler
        // multiplies it by the event budget, so a player carrying six of them
        // reads "every 20 seconds" and waits forty-five, and no blurb can
        // correct that -- the stretch belongs to the whole carried set. Said
        // once, here, rather than qualified thirty times.
        void PrintPacing(ChatHandler* handler, Player* player)
        {
            Gauntlet::Scheduler const* clock = sGauntlet->ClockFor(player);
            if (!clock)
                return;

            uint32 const timed = clock->TimedAffixes();
            float  const mult  = clock->Budget();

            if (timed <= 1)
            {
                handler->PSendSysMessage(
                    "  Pacing: {} affix acts on a timer; it acts as often as its card says.", timed);
                return;
            }

            handler->PSendSysMessage(
                "  Pacing: {} affixes act on a timer, so each one waits x{:.2f} longer than its "
                "card says (a card saying 20s acts every {}s). No two of them land within {}s "
                "of each other.",
                timed, mult, uint32(20.0f * mult + 0.5f), clock->MinSpacingMs() / 1000u);
        }

        void PrintProducts(ChatHandler* handler, Player* player)
        {
            std::string line;
            for (uint8 k = 0; k < static_cast<uint8>(AggregateKind::MAX); ++k)
            {
                AggregateKind const kind = static_cast<AggregateKind>(k);
                if (!line.empty())
                    line += " | ";
                line += Acore::StringFormat("{} x{:.2f}", AggregateKindName(kind),
                                            sGauntlet->Aggregate(player, kind));
            }
            handler->PSendSysMessage("  Now: {}", line);
        }

        // Every line the debug subtree prints says "[Gauntlet debug]" and not
        // "[Gauntlet]". The addon treats a "[Gauntlet]" system line as its own:
        // it scrapes it in fallback mode and, with suppressChat on, swallows it
        // whole (addon/GauntletUI/Panel.lua:452-461). A game master's dump is
        // not protocol, and a game master running the addon should still be
        // able to read it, so it stays outside the pattern the addon matches.
        // The player-facing pick/status/top lines keep the plain prefix, whose
        // shape the worker contract section 13 pins.

        // ------------------------------------------------------------------
        // Shared gates. Every one of them answers through SendErrorMessage,
        // which sets the handler's sentErrorMessage flag, so a handler that
        // returns false after a refusal prints its reason instead of the
        // command's usage text (ChatCommand.cpp:338-342) -- and, because it
        // returned false, is not written to the GM command log as an action
        // that happened.
        // ------------------------------------------------------------------

        // SEC_GAMEMASTER in the table is the first gate; this is the second.
        // Read on every call rather than cached at config load, so a realm can
        // switch the subtree off with `.reload config` and no restart. showLogs
        // is false because "the key is absent" is the default-off case rather
        // than a misconfiguration, and the alternative is one "missing
        // property" line per typed command.
        bool DebugAllowed(ChatHandler* handler)
        {
            if (sConfigMgr->GetOption<bool>("Gauntlet.Debug.Enable", false, false))
                return true;

            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r The debug commands are compiled in but "
                                      "switched off on this realm. Set Gauntlet.Debug.Enable = 1 in "
                                      "mod_gauntlet.conf and run .reload config.");
            return false;
        }

        // No run at all is the normal state of a character the module has never
        // loaded -- a bot, or anyone at all with Gauntlet.Enable = 0 -- so it is
        // answered rather than treated as a malformed command. A retired run is
        // returned: `status` and `dump` must still print the tombstone.
        RunState* ReadableRun(ChatHandler* handler, Player* player)
        {
            RunState* st = player ? sGauntlet->Get(player) : nullptr;
            if (!st)
                handler->SendErrorMessage("|cffff2020[Gauntlet]|r No Gauntlet run is loaded for this character.");
            return st;
        }

        // A cheat may not touch a retired run. The row is a hardcore
        // character's record of how its run ended, and the leaderboard has
        // already been written from it.
        RunState* MutableRun(ChatHandler* handler, Player* player)
        {
            RunState* st = ReadableRun(handler, player);
            if (st && st->dead)
            {
                handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r This run is retired. A retired run "
                                          "cannot be edited, by a game master or by anyone else.");
                return nullptr;
            }
            return st;
        }

        // "21" or "exposed". Numeric first: every key in the registry starts
        // with a letter, so the two namespaces cannot collide.
        MechanicDef const* LookupMechanic(std::string_view token)
        {
            if (Optional<uint16> const id = Acore::StringTo<uint16>(token))
                return FindMechanic(*id);
            return FindMechanic(token);
        }

        // A numeric index or the condition's own adjective, matched without
        // regard to case, so a game master can type the word the affix name
        // already shows them.
        bool ParseCondition(std::string_view token, Condition& out)
        {
            if (Optional<uint32> const n = Acore::StringTo<uint32>(token))
            {
                if (*n >= static_cast<uint32>(Condition::MAX))
                    return false;

                out = static_cast<Condition>(*n);
                return true;
            }

            for (uint8 c = 0; c < static_cast<uint8>(Condition::MAX); ++c)
                if (StringEqualI(ConditionName(static_cast<Condition>(c)), token))
                {
                    out = static_cast<Condition>(c);
                    return true;
                }

            return false;
        }

        // gauntlet_affix is keyed on (guid, slot) and RunState::Attach does not
        // check for a collision, so a cheat that reused an occupied slot would
        // leave two affixes in memory behind one database row. A real pick uses
        // the tier it was taken at; a debug give takes the lowest free slot,
        // which is predictable and cannot collide.
        uint8 LowestFreeSlot(RunState const& st)
        {
            for (uint32 s = 1; s <= 255; ++s)
            {
                bool taken = false;
                for (AffixInstance const& a : st.affixes)
                    if (a.slot == s)
                    {
                        taken = true;
                        break;
                    }

                if (!taken)
                    return static_cast<uint8>(s);
            }
            return 0;
        }

        std::string Actor(ChatHandler* handler)
        {
            WorldSession* session = handler->GetSession();
            if (!session)
                return "console";

            return Acore::StringFormat("account {} ({})", session->GetAccountId(), session->GetPlayerName());
        }
        // One cheat attach, shared by `give` and `give-class`.
        //
        // Everything `give` did inline, so the two cannot drift in what they
        // store, what they log, or the order they do it in -- the scheduler's
        // budget has to move before OnAttach arms anything, and getting that
        // wrong in one of two copies is the kind of thing that only shows up as
        // an affix firing on the wrong cadence.
        //
        // Returns the slot it used, or 0 if the run had none free.
        uint8 AttachCheat(ChatHandler* handler, Player* p, RunState* st,
                          MechanicDef const* def, uint32 rank, Condition condition)
        {
            uint8 const slot = LowestFreeSlot(*st);
            if (slot == 0)
                return 0;

            AffixInstance instance;
            instance.mechanic   = def->id;
            instance.rank       = static_cast<uint8>(rank);
            instance.condition  = condition;

            // The registry's boon, at the magnitude an offer would pay for it.
            //
            // This used to attach Boon::None with a zero magnitude, on the
            // reasoning that the cheat hands over the curse and the boon is
            // what an offer pays for it. The cost of that was invisible and
            // large: no boon has ever been exercised by any audit, so
            // Boon::BonusMoveSpeed could sit in the registry on three rows with
            // no implementation behind it at all, printing "you move 5% faster"
            // on the offer card and doing nothing. It took a player to notice.
            //
            // BoonMagnitude is exported for exactly this, so the affix the
            // audits attach is now the affix an offer would build.
            instance.boon       = def->boon;
            instance.boonMag    = static_cast<uint16>(BoonMagnitude(def->id, def->boon, rank));
            instance.slot       = slot;
            instance.genVersion = GeneratorVersion;

            AffixInstance& stored = st->Attach(instance);

            // The carried set moved, so the scheduler's event budget did too,
            // and it has to move before OnAttach arms anything.
            sGauntlet->SyncTimedAffixCount(p);

            if (stored.impl)
            {
                Ctx ctx = sGauntlet->MakeCtx(p, st, &stored);
                stored.impl->OnAttach(ctx);
            }

            uint32 const low = p->GetGUID().GetCounter();
            CharacterDatabase.Execute(
                "REPLACE INTO `gauntlet_affix` "
                "(`guid`, `slot`, `mechanic`, `rank`, `cond`, `boon`, `boon_mag`, `gen_version`) "
                "VALUES ({}, {}, {}, {}, {}, 0, 0, {})",
                low, static_cast<uint32>(slot), static_cast<uint32>(def->id), rank,
                static_cast<uint32>(condition), static_cast<uint32>(GeneratorVersion));

            // Deliberately not written to gauntlet_affix_log. Its `action`
            // column is an ENUM of the five things a run can legitimately do to
            // an affix, so a cheat goes to the server log instead of being
            // disguised as a pick.
            LOG_INFO("module", "Gauntlet: {} gave {} (id {}) rank {} condition {} to {} (guid {}) in slot {}.",
                     Actor(handler), def->name, static_cast<uint32>(def->id), rank,
                     ConditionName(condition), p->GetName(), low, static_cast<uint32>(slot));

            return slot;
        }

        // The audit's own attach and detach, and deliberately not AttachCheat
        // above.
        //
        // AttachCheat persists the affix to `gauntlet_affix` and writes a line
        // to the server log, both of which are right for a game master handing
        // themselves a curse to play with. `.gauntlet debug leaks` attaches
        // sixty-nine affixes and detaches every one of them before the command
        // returns: a hundred and thirty-eight rows and log lines describing a
        // state that never outlived the message would leave the cheat log
        // useless for the one thing it exists for, which is telling a run that
        // was edited from a run that was played.
        //
        // Everything else is the same, in the same order -- Attach, then
        // SyncTimedAffixCount, then OnAttach -- because that order is what
        // makes the scheduler's budget right before a mechanic arms anything,
        // and an audit that attached differently from the real path would be
        // auditing a path nothing takes.
        AffixInstance* AuditAttach(Player* p, RunState* st, MechanicDef const& def, uint32 rank)
        {
            uint8 const slot = LowestFreeSlot(*st);
            if (slot == 0)
                return nullptr;

            AffixInstance instance;
            instance.mechanic   = def.id;
            instance.rank       = static_cast<uint8>(rank);

            // Always, not a rolled condition: a gated affix whose condition is
            // false does nothing on attach and would be indistinguishable from
            // a mechanic that leaks nothing. The audit wants the affix awake.
            instance.condition  = Gauntlet::Condition::Always;

            // The registry's boon, at the magnitude an offer would pay.
            //
            // The audits attached Boon::None with a zero magnitude, and the
            // cost of that was invisible and large: no boon has ever been
            // exercised by leaks, soak or bench. Boon::BonusMoveSpeed could sit
            // on three registry rows with no implementation anywhere behind it,
            // printing "you move 5% faster" on the offer card and doing
            // nothing at all, and every audit called those cards clean. It took
            // a player to notice.
            //
            // An affix a player carries has a boon, so the affix the audits
            // attach has one too.
            instance.boon       = def.boon;
            instance.boonMag    = static_cast<uint16>(BoonMagnitude(def.id, def.boon, rank));
            instance.slot       = slot;
            instance.genVersion = GeneratorVersion;

            AffixInstance& stored = st->Attach(instance);
            sGauntlet->SyncTimedAffixCount(p);

            if (stored.impl)
            {
                Ctx ctx = sGauntlet->MakeCtx(p, st, &stored);
                stored.impl->OnAttach(ctx);
            }

            // What Mgr::Load and Mgr::Pick both do after OnAttach
            // (GauntletMgr.cpp:705 and :1006), and leaving it out made the
            // audit lie. A MaxHealth factor does not reach GetMaxHealth until
            // the stat chain is rerun, so a synchronous Capture read the old
            // number -- and Arcane Frailty duly reported halving the character's
            // maximum health and never putting it back, with the next card
            // reporting the restore as its own leak. Neither was true.
            sGauntlet->RefreshStats(p);

            // Re-read rather than returning the reference: OnAttach may have
            // attached something else -- a Bargain's rematch does -- and the
            // vector would have moved under it.
            return st->AtSlot(slot);
        }

        // How long the soak drives a mechanic for. Forty ticks is twenty
        // seconds of its own clock, which is enough for anything that
        // integrates over time; three fires is enough to catch the *cycle*
        // rather than the first event, and the cycle is where a summon or an
        // aura gets left behind.
        constexpr uint32 SOAK_TICKS = 40;
        constexpr uint32 SOAK_FIRES = 3;

        // Make the mechanic act, so that what it leaves behind can be seen.
        //
        // `leaks` attaches and detaches and does nothing else, so a curse whose
        // whole behaviour hangs off a hook reports inert -- sixty-three of
        // sixty-nine on a typical character. That is honest but thin, and it is
        // exactly how Berserker's Bargain hid: its leak needed a real cooldown
        // to already be running.
        //
        // Two drivers, and they are the only two that need nothing fabricated.
        // OnTick takes a duration. The scheduler's warn and fire take an event
        // id the mechanic armed for itself, delivered through Mgr::FireNow --
        // the same path `.gauntlet debug fire` uses, telegraph included.
        // Everything else on IMechanic wants a Unit or a Creature, and handing
        // a mechanic a fabricated enemy tests the fabrication.
        // Returns the number of the mechanic's own events actually released,
        // which is the only direct evidence that the soak drove anything. A
        // mechanic that never arms returns zero, and zero is why it reports
        // inert -- a different statement from "it leaked nothing".
        uint32 AuditExercise(Player* p, RunState* st, uint8 slot, uint16 mechanic)
        {
            uint32 fired = 0;

            for (uint32 i = 0; i < SOAK_TICKS; ++i)
            {
                // Re-read every time rather than holding the pointer: OnTick
                // may attach something -- a Bargain's rematch does -- and the
                // affix vector would have moved underneath it.
                AffixInstance* a = st->AtSlot(slot);
                if (!a || !a->impl || st->dead || !p->IsAlive())
                    return fired;

                Ctx ctx = sGauntlet->MakeCtx(p, st, a);
                a->impl->OnTick(ctx, Scheduler::TICK_MS);
            }

            for (uint32 i = 0; i < SOAK_FIRES; ++i)
            {
                if (st->dead || !p->IsAlive())
                    return fired;

                // False means it had nothing queued, which is the normal answer
                // for a mechanic that is not timed. There is no point asking a
                // second time.
                if (!sGauntlet->FireNow(p, mechanic))
                    return fired;

                ++fired;
            }

            return fired;
        }

        // `diagAfter`, when given, receives the mechanic's Diagnose() read
        // *after* OnDetach and before the instance is destroyed. The leak audit
        // used to read it before detaching -- the only moment the instance was
        // both alive and reachable from outside -- and printed the result as
        // "its own counters at detach", which for a card whose work happens in
        // OnDetach was a lie: the commons' "put back 0" read as the return
        // failing while the footprint showed the item back on.
        void AuditDetach(Player* p, RunState* st, uint8 slot, std::string* diagAfter = nullptr)
        {
            AffixInstance* a = st->AtSlot(slot);
            if (!a)
                return;

            if (a->impl)
            {
                Ctx ctx = sGauntlet->MakeCtx(p, st, a);
                a->impl->OnDetach(ctx);
                if (diagAfter)
                    *diagAfter = a->impl->Diagnose(ctx);
            }
            st->DetachSlot(slot);
            sGauntlet->SyncTimedAffixCount(p);

            // Same reason as the attach above: the real teardown refreshes
            // (GauntletMgr.cpp:1127), so an audit that does not is measuring a
            // state the game never leaves a character in.
            sGauntlet->RefreshStats(p);
        }

        // Who did it, for the cheat log. The account id is the part that
        // matters when two game masters share a character.

        void PrintAffixLine(ChatHandler* handler, uint32 index, AffixInstance const& a)
        {
            handler->PSendSysMessage("  {}. {} - {}", index,
                                     sGauntlet->NameOf(a.mechanic, a.condition, a.boon),
                                     sGauntlet->DescribeOf(a));
        }
    }
}

class GauntletCommandScript : public CommandScript
{
public:
    GauntletCommandScript() : CommandScript("GauntletCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        // Three levels deep, which the builder supports: a table entry whose
        // second field is another ChatCommandTable becomes a subcommand node
        // (Acore::ChatCommands::ChatCommandBuilder, ChatCommand.h:245-246), and
        // the core nests to this depth itself in cs_arena.cpp:35-61. Each table
        // is static because the node holds a reference_wrapper to it
        // (ChatCommand.h:232) and must outlive the call.
        static ChatCommandTable debug =
        {
            { "give",         HandleDebugGive,        SEC_GAMEMASTER, Console::No },
            { "give-class",   HandleDebugGiveClass,   SEC_GAMEMASTER, Console::No },
            { "remove",       HandleDebugRemove,      SEC_GAMEMASTER, Console::No },
            { "rank",         HandleDebugRank,        SEC_GAMEMASTER, Console::No },
            // Console::Yes: it reads the registry and builds throwaway
            // instances. It needs no character and no run, and requiring a
            // logged-in game master to audit the offer text would mean the
            // check could only be run by someone already playing.
            { "cards",        HandleDebugCards,       SEC_GAMEMASTER, Console::Yes },
            // Console::Yes, unlike what it looks like: this one needs a
            // character, but it does not need the character to be *yours*.
            // Named from the console it audits anyone online, which is how it
            // gets run without a game client at all -- see the note on the
            // handler.
            { "leaks",        HandleDebugLeaks,       SEC_GAMEMASTER, Console::Yes },
            // The same audit with the mechanic driven in between. Separate verb
            // rather than a flag on `leaks`: it summons, it fires, and it can
            // kill the character it is auditing, none of which belongs behind
            // an optional argument someone might pass by accident.
            { "soak",         HandleDebugSoak,        SEC_GAMEMASTER, Console::Yes },
            // The whole hook surface, per card, with the verdict derived rather
            // than hand-written -- so a card added later is covered without any
            // new test code. See the note on the handler.
            { "bench",        HandleDebugBench,       SEC_GAMEMASTER, Console::Yes },
            { "dump",         HandleDebugDump,        SEC_GAMEMASTER, Console::No },
            // Console::Yes for the audits' reason: it is the one command that
            // shows what the builder would put in front of a character, and
            // on the test realm the character is a playerbot with no client.
            // The name comes *after* the tier -- see the handler.
            { "offers",       HandleDebugOffers,      SEC_GAMEMASTER, Console::Yes },
            { "seed",         HandleDebugSeed,        SEC_GAMEMASTER, Console::No },
            // Console::Yes: this one reads the registry and writes a file. It
            // needs no player, and requiring a logged-in game master to
            // regenerate a build artefact would mean the addon table could
            // only be refreshed from inside the game.
            { "export-addon", HandleDebugExportAddon, SEC_GAMEMASTER, Console::Yes },

            // Plan section 5.2's last three, wired in Phase 2. `fire` releases
            // a queued event now and keeps the warning it already sent; `set`
            // writes a gauntlet_state key straight through; `events on|off` is
            // the realm switch for the length of a worldserver session.
            { "fire",         HandleDebugFire,        SEC_GAMEMASTER, Console::No },
            { "set",          HandleDebugSet,         SEC_GAMEMASTER, Console::No },
            { "events",       HandleDebugEvents,      SEC_GAMEMASTER, Console::No },

            // Phase 3. Deals a share of the caster's own maximum health
            // through the real damage pipeline, which is the only way to
            // exercise Last Rites without dying for it.
            { "hurt",         HandleDebugHurt,        SEC_GAMEMASTER, Console::No },

            // The player commands with a name in front, for the audits'
            // reason: reroll and skip need a Player with an offer pending, and
            // on the test realm the Player is a bot with no client. Mgr's own
            // chat answers go to the bot; the console gets the verdict.
            { "reroll",       HandleDebugReroll,      SEC_GAMEMASTER, Console::Yes },
            { "skip",         HandleDebugSkip,        SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable sub =
        {
            { "pick",   HandlePick,   SEC_PLAYER, Console::No },
            { "reroll", HandleReroll, SEC_PLAYER, Console::No },
            { "skip",   HandleSkip,   SEC_PLAYER, Console::No },
            { "status", HandleStatus, SEC_PLAYER, Console::No },
            { "top",    HandleTop,    SEC_PLAYER, Console::No },
            { "debug",  debug },
        };
        static ChatCommandTable root =
        {
            { "gauntlet", sub },
        };
        return root;
    }

    // ------------------------------------------------------------------
    // Player commands.
    // ------------------------------------------------------------------

    static bool HandlePick(ChatHandler* handler, uint32 index)
    {
        Player* p = handler->GetPlayer();
        if (!p)
            return false;

        if (!sGauntlet->Pick(p, index))
        {
            handler->PSendSysMessage("|cffff2020[Gauntlet]|r Nothing to pick, or invalid choice.");
            return true;
        }

        // The run line and the carried set both moved; the addon is told the
        // same way whether the pick came from this command or from its own
        // button.
        sGauntletAddon->SendSnapshot(p);
        return true;
    }

    static bool HandleReroll(ChatHandler* handler)
    {
        Player* p = handler->GetPlayer();
        if (!p)
            return false;

        // Mgr says why when it refuses; the snapshot moves the addon's chooser
        // onto the new set only when there is one.
        if (sGauntlet->Reroll(p))
            sGauntletAddon->SendSnapshot(p);
        return true;
    }

    static bool HandleSkip(ChatHandler* handler)
    {
        Player* p = handler->GetPlayer();
        if (!p)
            return false;

        if (sGauntlet->Skip(p))
            sGauntletAddon->SendSnapshot(p);
        return true;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        Player* p = handler->GetPlayer();
        RunState* st = ReadableRun(handler, p);
        if (!st)
            return false;

        handler->PSendSysMessage("|cffff2020[Gauntlet]|r seed {} | tier {} | {}",
                                 st->seed, st->tier, st->dead ? "RETIRED" : "alive");
        for (uint32 i = 0; i < st->affixes.size(); ++i)
            PrintAffixLine(handler, i + 1, st->affixes[i]);

        // Plan section 2.5. The products are what the affixes above actually
        // add up to after the caps clamp them, which is not something six
        // separate descriptions can be read off.
        PrintProducts(handler, p);
        PrintPacing(handler, p);
        return true;
    }

    static bool HandleTop(ChatHandler* handler)
    {
        Player* p = handler->GetPlayer();

        // conducts joins the select for the addon's TOP line, which is where
        // it belongs -- the plan gives it to the addon's leaderboard tab and
        // Phase 5 finally built one, so a player with the addon hovers a run
        // and reads the whole list in a tooltip.
        //
        // It is also printed here, on a line of its own under the run it
        // belongs to and only when there is one. The earlier reading was that
        // a conduct list is too long for a chat frame, and it is too long to
        // append to the run's own line -- but a player without the addon would
        // otherwise never see the column at all, and a run's conducts are the
        // reason its place on this list means anything.
        QueryResult r = CharacterDatabase.Query(
            "SELECT `name`, `tier`, `level`, `cause`, `conducts` FROM `gauntlet_leaderboard` "
            "ORDER BY `tier` DESC, `level` DESC LIMIT 10");

        handler->PSendSysMessage("|cffff2020[Gauntlet]|r Furthest runs:");
        if (!r)
        {
            handler->PSendSysMessage("  No completed runs yet.");
            return true;
        }

        uint32 rank = 1;
        do
        {
            Field* f = r->Fetch();
            std::string const name  = f[0].Get<std::string>();
            uint32 const      tier  = f[1].Get<uint32>();
            uint32 const      level = f[2].Get<uint32>();
            std::string const cause = f[3].Get<std::string>();

            std::string const conducts = f[4].Get<std::string>();

            handler->PSendSysMessage("  {}. {} - tier {} at level {} ({})", rank,
                                     name, tier, level, cause);
            if (!conducts.empty())
                handler->PSendSysMessage("     |cff60c060conducts:|r {}", conducts);

            if (p)
                sGauntletAddon->SendTop(p, rank, name, tier, level, cause, conducts);

            ++rank;
        } while (r->NextRow());
        return true;
    }

    // ------------------------------------------------------------------
    // The game master subtree. Everything below mutates or inspects a live
    // hardcore run and is a cheat; each of the three that write say so in
    // chat and write a line to the module's log, because a run that was
    // edited and a run that was played have to be tellable apart afterwards.
    // ------------------------------------------------------------------

    static bool HandleDebugGive(ChatHandler* handler, std::string_view keyOrId,
                                Optional<uint32> rankArg, Optional<std::string_view> condArg)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = handler->GetPlayer();
        RunState* st = MutableRun(handler, p);
        if (!st)
            return false;

        MechanicDef const* def = LookupMechanic(keyOrId);
        if (!def)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r No mechanic '{}'. Give a registry id (1-73) or a "
                                      "key such as 'exposed'.", keyOrId);
            return false;
        }

        if (st->Find(def->id))
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r {} is already carried. Use .gauntlet debug rank to "
                                      "change it, or .gauntlet debug remove first.", def->name);
            return false;
        }

        uint32 const ceiling = std::min<uint32>(def->maxRank, MAX_RANK);
        uint32 const rank    = rankArg ? *rankArg : 1u;
        if (rank < 1 || rank > ceiling)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Rank {} is out of range for {}: 1 to {}.",
                                      rank, def->name, ceiling);
            return false;
        }

        Gauntlet::Condition condition = Gauntlet::Condition::Always;
        if (condArg && !ParseCondition(*condArg, condition))
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r '{}' is not a condition. Give an index 0-{} or an "
                                      "adjective such as 'Desperate'.", *condArg,
                                      static_cast<uint32>(Gauntlet::Condition::MAX) - 1);
            return false;
        }

        uint8 const slot = AttachCheat(handler, p, st, def, rank, condition);
        if (slot == 0)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r This run has no free affix slot.");
            return false;
        }

        AffixInstance const* stored = st->AtSlot(slot);
        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r CHEAT: attached slot {} - {}",
                                 static_cast<uint32>(slot),
                                 sGauntlet->NameOf(def->id, condition, Boon::None));
        if (stored)
            handler->PSendSysMessage("  {}", sGauntlet->DescribeOf(*stored));
        if (!stored || !stored->impl)
            handler->PSendSysMessage("  No implementation in this build: it is carried and stored, but inert.");
        return true;
    }

    // Every affix this character's class can be offered, attached at once.
    //
    // `.gauntlet debug give` takes one key and there are forty-four class
    // curses; testing a class meant typing four commands and looking up which
    // four. This takes the question the tester actually has -- "what can a
    // paladin get, and what does it do to me" -- and answers it in one line.
    //
    //   .gauntlet debug give-class              the Class family, at rank 1
    //   .gauntlet debug give-class 4            the Class family, at rank IV
    //   .gauntlet debug give-class 3 all        everything relevant, rank III
    //   .gauntlet debug give-class 1 spawn      one family
    //
    // Relevance is the generator's own, through LivePlayerView, so what lands
    // is exactly what this character could be offered -- including the spell
    // and talent-tree gates, which is the part worth reporting rather than
    // silently applying. A curse keyed on a spell you have not trained is
    // carried and inert, and looks identical to a broken one; the summary says
    // which those are so a tester does not file the difference as a bug.
    //
    // MAX_CARRIED is deliberately not enforced. It is what makes a *run* a set
    // of choices, and this is not a run.
    static bool HandleDebugGiveClass(ChatHandler* handler, Optional<uint32> rankArg,
                                     Optional<std::string_view> familyArg)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = handler->GetPlayer();
        RunState* st = MutableRun(handler, p);
        if (!st)
            return false;

        uint32 const rank = rankArg ? *rankArg : 1u;
        if (rank < 1 || rank > MAX_RANK)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Rank {} is out of range: 1 to {}.",
                                      rank, static_cast<uint32>(MAX_RANK));
            return false;
        }

        // Default to the Class family, because that is the question the command
        // was asked for. "all" is every family.
        bool everyFamily = false;
        Family wanted = Family::Class;
        if (familyArg && !familyArg->empty())
        {
            if (StringEqualI(*familyArg, "all"))
                everyFamily = true;
            else
            {
                bool found = false;
                for (uint8 f = 0; f < static_cast<uint8>(Family::MAX); ++f)
                    if (StringEqualI(FamilyName(static_cast<Family>(f)), *familyArg))
                    {
                        wanted = static_cast<Family>(f);
                        found  = true;
                        break;
                    }

                if (!found)
                {
                    handler->SendErrorMessage(
                        "|cffff2020[Gauntlet debug]|r No family called \"{}\". Try one of "
                        "spawn, enemy, tempo, attrition, rules, bargain, class -- or all.",
                        *familyArg);
                    return false;
                }
            }
        }

        LivePlayerView const view(p);

        uint32 attached = 0, already = 0, gated = 0;
        std::string gatedNames;

        for (MechanicDef const& def : AllMechanics())
        {
            if (!everyFamily && def.family != wanted)
                continue;
            if (def.classMask != 0 && (def.classMask & view.GetClassMask()) == 0)
                continue;
            if (st->Find(def.id))
            {
                ++already;
                continue;
            }

            // The gates the generator would also apply. Reported rather than
            // obeyed: a tester wants the curse on so they can train the spell
            // and watch it start working.
            bool const needsSpell = def.requiresSpell != 0 && !view.HasSpell(def.requiresSpell);
            bool const needsTree  = def.requiresTree  != 0 && def.requiresTree != view.GetTalentTree();

            uint32 const capped = std::min<uint32>(rank, std::min<uint8>(def.maxRank, MAX_RANK));
            uint8 const slot = AttachCheat(handler, p, st, &def, capped, Gauntlet::Condition::Always);
            if (slot == 0)
            {
                handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Ran out of affix slots.");
                break;
            }

            ++attached;
            handler->PSendSysMessage("  |cffffff00{}|r  slot {}  rank {}{}", def.name,
                                     static_cast<uint32>(slot), capped,
                                     capped < rank ? " (its ceiling)" : "");

            if (needsSpell || needsTree)
            {
                ++gated;
                if (!gatedNames.empty())
                    gatedNames += ", ";
                gatedNames += def.name;
            }
        }

        handler->PSendSysMessage(
            "|cffff2020[Gauntlet debug]|r CHEAT: attached {} affix(es) at rank {}{}.",
            attached, rank, already ? Acore::StringFormat(", {} already carried", already) : "");

        if (gated != 0)
            handler->PSendSysMessage(
                "  |cffff8040{} of them are gated on a spell or a talent tree you do not have "
                "yet: {}. They are carried and will do nothing until you train it -- that is "
                "the gate, not a bug.|r", gated, gatedNames);

        if (attached != 0)
            handler->PSendSysMessage("  .gauntlet status lists them; .gauntlet debug dump has each "
                                     "one's own counters.");
        return true;
    }

    static bool HandleDebugRemove(ChatHandler* handler, uint32 slotArg)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = handler->GetPlayer();
        RunState* st = MutableRun(handler, p);
        if (!st)
            return false;

        AffixInstance* a = slotArg <= 255 ? st->AtSlot(static_cast<uint8>(slotArg)) : nullptr;
        if (!a)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Nothing is carried in slot {}. "
                                      ".gauntlet debug dump lists the slots.", slotArg);
            return false;
        }

        uint8 const slot = static_cast<uint8>(slotArg);

        uint16 const mechanic = a->mechanic;
        std::string const name = sGauntlet->NameOf(a->mechanic, a->condition, a->boon);

        // Detach before the row goes, in the order Mgr::Pick's swap uses: the
        // implementation may want the run to still look the way it did while
        // the affix was carried.
        if (a->impl)
        {
            Ctx ctx = sGauntlet->MakeCtx(p, st, a);
            a->impl->OnDetach(ctx);
        }
        st->DetachSlot(slot);
        sGauntlet->SyncTimedAffixCount(p);

        uint32 const low = p->GetGUID().GetCounter();
        CharacterDatabase.Execute("DELETE FROM `gauntlet_affix` WHERE `guid` = {} AND `slot` = {}",
                                  low, static_cast<uint32>(slot));

        LOG_INFO("module", "Gauntlet: {} removed {} (id {}) from {} (guid {}), slot {}.",
                 Actor(handler), name, static_cast<uint32>(mechanic), p->GetName(), low,
                 static_cast<uint32>(slot));

        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r CHEAT: detached slot {} - {}",
                                 static_cast<uint32>(slot), name);
        return true;
    }

    static bool HandleDebugRank(ChatHandler* handler, uint32 slotArg, uint32 rankArg)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = handler->GetPlayer();
        RunState* st = MutableRun(handler, p);
        if (!st)
            return false;

        AffixInstance* a = slotArg <= 255 ? st->AtSlot(static_cast<uint8>(slotArg)) : nullptr;
        if (!a)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Nothing is carried in slot {}. "
                                      ".gauntlet debug dump lists the slots.", slotArg);
            return false;
        }

        // A mechanic this build has no registry row for still has to be
        // rankable: the affix is on a live character either way.
        MechanicDef const* def = FindMechanic(a->mechanic);
        uint32 const ceiling = def ? std::min<uint32>(def->maxRank, MAX_RANK) : MAX_RANK;
        if (rankArg < 1 || rankArg > ceiling)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Rank {} is out of range for that affix: 1 to {}.",
                                      rankArg, ceiling);
            return false;
        }

        uint8 const was = a->rank;
        a->rank = static_cast<uint8>(rankArg);

        // In place and in the same slot, which is what Mgr::Pick's rank-up
        // path does; no detach and no re-attach, because a rank is a number
        // the implementation reads rather than state it holds.
        uint32 const low = p->GetGUID().GetCounter();
        CharacterDatabase.Execute("UPDATE `gauntlet_affix` SET `rank` = {} WHERE `guid` = {} AND `slot` = {}",
                                  rankArg, low, slotArg);

        LOG_INFO("module", "Gauntlet: {} set slot {} of {} (guid {}) from rank {} to rank {}.",
                 Actor(handler), slotArg, p->GetName(), low, static_cast<uint32>(was), rankArg);

        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r CHEAT: slot {} rank {} -> {}",
                                 slotArg, static_cast<uint32>(was), rankArg);
        handler->PSendSysMessage("  {}", sGauntlet->DescribeOf(*a));

        // A rank moves every product the affix contributes to.
        PrintProducts(handler, p);
        return true;
    }

    // The three sections plan section 5.4 asks a dump to show and Phase 0 had
    // nothing to show for. They are the only window on the framework there is:
    // nobody can watch a scheduler queue from inside the game any other way.
    static void PrintScheduler(ChatHandler* handler, Player* p, RunState const& st)
    {
        Scheduler const* sched = sGauntlet->ClockFor(p);
        if (!sched)
        {
            handler->PSendSysMessage("  scheduler: no clock for this run.");
            return;
        }

        uint32 const now = sched->NowMs();
        handler->PSendSysMessage("  scheduler: now {} ms | budget x{:.2f} | grace {} ms | queue {}",
                                 now, sched->Budget(), st.graceMs,
                                 static_cast<uint32>(sched->Queue().size()));

        // The first thing to read when an affix "is not firing". All six of
        // these stop the whole queue and none of them is visible in game.
        std::string const held = sGauntlet->SuppressionReason(p);
        if (held.empty())
            handler->PSendSysMessage("  |cff60c060nothing is holding the queue back|r");
        else
            handler->PSendSysMessage("  |cffff2020EVERY event is suppressed:|r {}", held);

        for (ScheduledEvent const& ev : sched->Queue())
        {
            MechanicDef const* def = FindMechanic(ev.mechanic);

            // dueMs is absolute on the scheduler's own clock, so the useful
            // number is the difference. A negative one means the event is due
            // and something is holding it -- a suppression, or the spacing --
            // which is exactly the state this dump exists to show.
            int64 const inMs = static_cast<int64>(ev.dueMs) - static_cast<int64>(now);
            handler->PSendSysMessage("    {} id {} | {} | in {} ms",
                                     def ? def->key : "<not in this registry>",
                                     ev.id,
                                     ev.kind == EventKind::Warn ? "warn" : "fire",
                                     inMs);
        }

        // What the run last blamed. It is what KILLBY would name if the
        // character died now.
        if (st.lastActor != Gauntlet::MECHANIC_NONE)
        {
            MechanicDef const* def = FindMechanic(st.lastActor);
            handler->PSendSysMessage("  last actor: {} (for another {} ms)",
                                     def ? def->key : "<not in this registry>", st.lastActorMs);
        }
        else
        {
            handler->PSendSysMessage("  last actor: none");
        }
    }

    static void PrintSummons(ChatHandler* handler, Player* p)
    {
        // Summons exposes a count, a stalker flag and per-creature lookups, and
        // no way to walk one owner's list; adding one would mean editing
        // GauntletSummons.h, which this task does not own. The two numbers are
        // what there is. See the report.
        handler->PSendSysMessage("  summons: {} alive (cap {}) | stalker {}",
                                 sGauntletSummons->AliveFor(p),
                                 static_cast<uint32>(SUMMON_CAP_TOTAL),
                                 sGauntletSummons->HasStalker(p) ? "yes" : "no");
    }

    static void PrintState(ChatHandler* handler, Player* p, RunState const& st)
    {
        // gauntlet_state is the enumerable half: State itself has no iterator,
        // so the keys come from the table and the live value beside each one
        // comes from the map. A key that has been set this session and never
        // saved therefore does not appear -- which the line says, rather than
        // leaving a reader to think the map is empty.
        uint32 const low = p->GetGUID().GetCounter();
        handler->PSendSysMessage("  per-player state (saved rows, live value beside; unsaved new keys are not "
                                 "listed): dirty {}", st.state.Dirty() ? "yes" : "no");

        QueryResult r = CharacterDatabase.Query("SELECT `k`, `v` FROM `gauntlet_state` WHERE `guid` = {} "
                                                "ORDER BY `k` ASC", low);
        if (!r)
        {
            handler->PSendSysMessage("    (no rows)");
            return;
        }

        do
        {
            Field* f = r->Fetch();
            std::string const key = f[0].Get<std::string>();
            int32 const saved     = f[1].Get<int32>();
            int32 const live      = st.state.Get(key, saved);
            if (live == saved)
                handler->PSendSysMessage("    {} = {}", key, saved);
            else
                handler->PSendSysMessage("    {} = {} (saved {})", key, live, saved);
        } while (r->NextRow());
    }

    // Every card at every rank, and a shout when two of them are the same.
    //
    // This exists because Half-Tamed shipped with rank I and rank II identical
    // -- same happiness threshold, same hostile duration, and Describe() reads
    // only those two arrays, so the offer card for rank II was the same *string*
    // as rank I. A player taking that rank-up spent a tier on nothing and the
    // card told them nothing had changed.
    //
    // Nothing could have caught it off-line: Describe() is a method on the
    // mechanic, the mechanics need a Player, and the unit-test harness compiles
    // neither. So the check lives where the mechanics do. It reads the registry
    // and builds one throwaway instance per rank; it commits nothing and needs
    // no run.
    static bool HandleDebugCards(ChatHandler* handler, Optional<std::string_view> keyArg)
    {
        if (!DebugAllowed(handler))
            return false;

        uint32 dead = 0;
        uint32 wide = 0;
        uint32 shown = 0;

        for (MechanicDef const& def : AllMechanics())
        {
            if (keyArg && *keyArg != def.key)
                continue;

            uint8 const top = std::min<uint8>(def.maxRank, MAX_RANK);
            handler->PSendSysMessage("|cffff2020[{}]|r {} - {}, ranks 1..{}", def.key, def.name,
                                     RarityName(def.rarity), top);

            std::string previous;
            for (uint8 rank = 1; rank <= top; ++rank)
            {
                AffixInstance preview;
                preview.mechanic = def.id;
                preview.rank     = rank;
                preview.boon     = def.boon;

                // Magnitude deliberately left at zero, which suppresses the
                // boon clause. The boon ladders by rank on its own, so leaving
                // it in would make two otherwise identical cards read
                // differently and hide exactly the fault this is looking for.
                // What is compared here is the curse.
                preview.boonMag  = 0;

                std::unique_ptr<IMechanic> const impl(MakeMechanic(def.id));
                preview.impl = impl.get();

                std::string const text = sGauntlet->DescribeOf(preview);
                handler->PSendSysMessage("  rank {}: {}", rank, text);

                // The one shape the addon's split/rejoin cannot survive: a
                // single word longer than a chunk has no space to cut at, so
                // the cut is hard and the rejoin inserts a space that was never
                // there. tests/WireTest.cpp proves the round trip for
                // everything else; this is the live half of that guard, over
                // all sixty-nine mechanics at every rank, because Describe()
                // needs a Player and the test harness cannot build one.
                std::size_t longest = 0, run = 0;
                for (char c : text)
                {
                    if (c == ' ') { run = 0; continue; }
                    longest = std::max<std::size_t>(longest, ++run);
                }
                if (longest >= DESC_CHUNK)
                {
                    ++wide;
                    handler->PSendSysMessage(
                        "  |cffff2020^ a {}-character word: the addon splits this mid-word and "
                        "rejoins it with a space that was not there|r", longest);
                }

                if (rank > 1 && text == previous)
                {
                    ++dead;
                    handler->PSendSysMessage(
                        "  |cffff2020^ rank {} reads exactly as rank {}: this rank-up costs a tier "
                        "and changes nothing|r", rank, rank - 1);
                }
                previous = text;
            }
            ++shown;
        }

        if (shown == 0)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r No mechanic with that key.");
            return false;
        }

        handler->PSendSysMessage(
            "|cffff2020[Gauntlet debug]|r {} mechanic(s), {} dead rank(s), {} unsplittable word(s).",
            shown, dead, wide);
        return true;
    }

    // The audit auditing itself, and the reason it exists is phase 9's own
    // closing note: verify a new audit fails before trusting it.
    //
    // The pure half is covered -- tests/AuditTest.cpp moves each field and
    // checks Diff reports it. Capture is the half no test can reach, because it
    // needs a character, and its failure mode is silent and total: a Capture
    // wired to the wrong getter returns an empty footprint, an empty footprint
    // compares equal to another empty footprint, and the command cheerfully
    // reports sixty-nine mechanics clean.
    //
    // So this asks whether the live half can see anything at all. It applies
    // nothing to the character. Everything below is a read of what is already
    // there, except one scheduler entry, which is the only dimension that can
    // be moved and put back leaving nothing behind.
    static bool AuditSelfTest(ChatHandler* handler, Player* p, RunState* st)
    {
        uint32 failed = 0;
        auto check = [&](bool ok, char const* what)
        {
            handler->PSendSysMessage("  {} {}", ok ? "|cff60c060ok  |r" : "|cffff2020FAIL|r", what);
            if (!ok)
                ++failed;
        };

        Footprint const at_rest = Capture(p, st);

        check(Diff(at_rest, at_rest).empty(), "a reading compares equal to itself");
        check(at_rest.auras.size() == p->GetAppliedAuras().size(),
              "the aura list is as long as the character's");
        check(std::is_sorted(at_rest.auras.begin(), at_rest.auras.end()),
              "the aura list is sorted, which Diff's multiset walk requires");
        check(at_rest.cooldowns.size() == p->GetSpellCooldownMap().size(),
              "the cooldown list is as long as the character's");
        check(std::is_sorted(at_rest.cooldowns.begin(), at_rest.cooldowns.end()),
              "the cooldown list is sorted");
        check(at_rest.maxHealth == p->GetMaxHealth() && at_rest.maxHealth != 0,
              "max health is the character's and is not zero");
        check(at_rest.carried == st->affixes.size(), "the carried count is the run's");

        // If arming an event is not visible here, the audit is blind to every
        // Timed mechanic in the module -- which is most of the Spawn and Tempo
        // families -- and would report all of them clean.
        Scheduler* clock = sGauntlet->ClockFor(p);

        // A real registry id that this run does not carry. It has to be real:
        // Scheduler::Arm refuses MECHANIC_NONE outright and returns without
        // queueing anything (GauntletScheduler.cpp:146), so the obvious
        // "sentinel nobody uses" probe arms nothing and measures nothing.
        //
        // That is not a hypothetical. This self-test was written with the
        // sentinel, and its first run against a live character failed on
        // exactly this line -- which is the check doing its job, on its own
        // probe, before it was ever pointed at the module.
        //
        // Not carried matters because Cancel takes a mechanic rather than an
        // event: cancelling the probe must not take a carried affix's real
        // queue with it. A run carries at most a handful of sixty-nine, so
        // there is always one free.
        uint16 probe = Gauntlet::MECHANIC_NONE;
        for (MechanicDef const& def : AllMechanics())
            if (!st->Find(def.id))
            {
                probe = def.id;
                break;
            }

        if (!clock)
        {
            check(false, "this run has a clock to arm");
        }
        else if (probe == Gauntlet::MECHANIC_NONE)
        {
            check(false, "there is a registry id this run does not carry, to probe with");
        }
        else
        {
            clock->Arm(probe, 1u, 600000, 0);
            Footprint const armed = Capture(p, st);
            check(!Diff(at_rest, armed).empty(), "arming an event is visible to the audit");

            clock->Cancel(probe);
            Footprint const back = Capture(p, st);
            check(Diff(at_rest, back).empty(), "cancelling it puts the reading back");
        }

        if (failed == 0)
            handler->PSendSysMessage(
                "|cffff2020[Gauntlet debug]|r self-test passed: the audit can see this character. "
                "A clean report from .gauntlet debug leaks means something.");
        else
            handler->PSendSysMessage(
                "|cffff2020[Gauntlet debug]|r self-test FAILED {} check(s). Until these pass, "
                "|cffff2020ignore every verdict .gauntlet debug leaks gives|r -- a blind audit "
                "reports everything clean.", failed);
        return true;
    }

    // Attach every affix, detach it again, and say what did not come back.
    //
    //   .gauntlet debug leaks self            check the audit can see anything
    //   .gauntlet debug leaks                 every mechanic, at its top rank
    //   .gauntlet debug leaks class           one family
    //   .gauntlet debug leaks shade 2         one mechanic, at a rank you name
    //
    // The bug class this exists for is the one nothing else in the repo can
    // reach. `OnAttach` and `OnDetach` were written by hand sixty-nine times;
    // the unit tests cannot call either, because both take a Player and the
    // harness has no world; and reading them is what phase 9 did, which is why
    // phase 9's own report says an audit that has never failed is a claim
    // rather than a check.
    //
    // What it cannot do is decide whether an effect is *correct*. It only
    // answers "is the character the way it was found", which is a smaller
    // question with a machine-checkable answer -- and the whole of
    // docs/checklists.md is the bigger one.
    // The body behind both `leaks` and `soak`. They differ in one bool: whether
    // the mechanic is made to act between the attach and the detach.
    static bool RunLeakAudit(ChatHandler* handler, Optional<PlayerIdentifier> whoArg,
                             Optional<std::string_view> whatArg, Optional<uint32> rankArg,
                             bool exercise)
    {
        if (!DebugAllowed(handler))
            return false;

        // The optional name in front is what makes this runnable from the
        // server console and over SOAP, and that is the whole reason it is
        // here: everything this audit measures needs a Player, and a Player
        // needs someone logged in. With mod-playerbots on a test realm there
        // is one without a game client, which is the only way this command can
        // ever be run by anything but a person sitting at the game.
        //
        // Optional<PlayerIdentifier> first is the core's own shape for this
        // (cs_misc.cpp's kick, unstuck and additem all read this way): in game
        // the parser tries the leading word as a character name, fails, and
        // backs off to the next argument, so `.gauntlet debug leaks class`
        // still means the family. A character actually named "self", "all" or
        // "class" would shadow the keyword; naming one that is the way to find
        // out.
        Player* p = whoArg ? whoArg->GetConnectedPlayer() : handler->GetPlayer();
        if (!p)
        {
            handler->SendErrorMessage(
                "|cffff2020[Gauntlet debug]|r No character to audit. From the console or SOAP, name one "
                "who is online: .gauntlet debug {} <name> [what] [rank]", exercise ? "soak" : "leaks");
            return false;
        }

        RunState* st = MutableRun(handler, p);
        if (!st)
            return false;

        // `leaks self` audits the audit rather than the module. It is the
        // first thing to run and the answer decides whether anything below is
        // worth reading.
        if (whatArg && StringEqualI(*whatArg, "self"))
            return AuditSelfTest(handler, p, st);

        // What to audit. A key wins over a family name, so `leaks class` is the
        // family and `leaks c13_cold_trail` is the one mechanic; no registry
        // key collides with a family name.
        bool everyFamily = true;
        Family wanted = Family::Class;
        MechanicDef const* onlyOne = nullptr;

        if (whatArg && !whatArg->empty() && !StringEqualI(*whatArg, "all"))
        {
            for (uint8 f = 0; f < static_cast<uint8>(Family::MAX); ++f)
                if (StringEqualI(FamilyName(static_cast<Family>(f)), *whatArg))
                {
                    wanted = static_cast<Family>(f);
                    everyFamily = false;
                    break;
                }

            if (everyFamily)
            {
                onlyOne = LookupMechanic(*whatArg);
                if (!onlyOne)
                {
                    handler->SendErrorMessage(
                        "|cffff2020[Gauntlet debug]|r \"{}\" is neither a family nor a mechanic. Give a "
                        "registry key, one of spawn, enemy, tempo, attrition, rules, bargain, class, "
                        "all -- or self, to check the audit itself.", *whatArg);
                    return false;
                }
            }
        }

        // The top rank by default: a leak that only exists at rank IV is still
        // a leak, and the ranks below it are the ones a hand-read is most
        // likely to have got right.
        uint32 const askedRank = rankArg ? *rankArg : uint32(MAX_RANK);
        if (askedRank < 1 || askedRank > MAX_RANK)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Rank {} is out of range: 1 to {}.",
                                      askedRank, static_cast<uint32>(MAX_RANK));
            return false;
        }

        uint32 audited = 0, leaked = 0, clean = 0, inert = 0, skipped = 0, offClass = 0;

        // The generator's own view of this character, so the class filter below
        // is the same one the chooser applies.
        LivePlayerView const view(p);

        // Soak only: how many of the mechanics' own events were actually
        // released, and across how many mechanics.
        uint32 fired = 0, drivenMechanics = 0;

        for (MechanicDef const& def : AllMechanics())
        {
            if (onlyOne)
            {
                if (def.id != onlyOne->id)
                    continue;
            }
            else if (!everyFamily && def.family != wanted)
                continue;

            // Already carried: attaching a second copy is not a state this
            // module can reach, so auditing it would be auditing a fiction.
            if (st->Find(def.id))
            {
                ++skipped;
                continue;
            }

            // Neither is a class curse on the wrong class. The generator filters
            // on classMask, so a warlock is never offered a hunter's curse -- and
            // auditing one anyway does not find bugs, it invents them.
            //
            // Half-Tamed is the case that put this here. Attached to a warlock it
            // dismissed the demon, which took Fel Vitality with it, and the soak
            // duly reported max power down by 780 and never restored. All of that
            // is true and none of it is reachable: on a hunter the dismissal is
            // the card's own text ("the real pet can be called back afterwards").
            //
            // Naming a mechanic explicitly overrides this. `soak <name>
            // c09_half_tamed 4` still audits it on whoever is standing there,
            // because an explicit request is a decision rather than a sweep.
            if (!onlyOne && def.classMask != 0 && (def.classMask & view.GetClassMask()) == 0)
            {
                ++offClass;
                continue;
            }

            uint32 const rank = std::min<uint32>(askedRank, std::min<uint8>(def.maxRank, MAX_RANK));

            Footprint const before = Capture(p, st);

            AffixInstance* attached = AuditAttach(p, st, def, rank);
            if (!attached)
            {
                handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Ran out of affix slots.");
                break;
            }

            uint8 const slot = attached->slot;

            // Held first, then the exercise. `held` is what the attach alone
            // did, which is what the clean/inert verdict is about; making the
            // mechanic act afterwards must not change that reading.
            Footprint const held = Capture(p, st);

            uint32 const firedHere = exercise ? AuditExercise(p, st, slot, def.id) : 0u;
            fired += firedHere;
            if (firedHere != 0)
                ++drivenMechanics;

            // The mechanic's own counters, read while it is still carried. This
            // is the evidence that the soak actually drove something: a fire
            // count that is still zero after three FireNow calls means the
            // mechanic never armed anything, which is a different statement
            // from "it leaked nothing".
            std::string diag;
            if (onlyOne)
                if (AffixInstance* live = st->AtSlot(slot))
                    if (live->impl)
                    {
                        Ctx dctx = sGauntlet->MakeCtx(p, st, live);
                        diag = live->impl->Diagnose(dctx);
                    }

            // A mechanic's own event can be lethal -- Falling Sky's is -- and a
            // death retires the run, which makes every mechanic after this one
            // unauditable. Stop and say so rather than reporting sixty
            // mechanics that were never looked at.
            if (st->dead || !p->IsAlive())
            {
                AuditDetach(p, st, slot);
                handler->SendErrorMessage(
                    "|cffff2020[Gauntlet debug]|r {} killed the character. {} mechanic(s) audited before "
                    "that; the rest were not. That is a finding, not a crash.", def.name, audited);
                return false;
            }

            // The same counters again once OnDetach has run, for the cards
            // whose work is the detach -- a common putting a helm back on.
            std::string diagAfter;
            AuditDetach(p, st, slot, onlyOne ? &diagAfter : nullptr);
            Footprint const after = Capture(p, st);

            ++audited;

            std::vector<std::string> const lines = Diff(before, after);

            // What it changed at attach, with the carried count normalised out.
            // The affix being carried is bookkeeping rather than an effect, and
            // leaving it in makes every mechanic look like it did something --
            // which is precisely what the first real run of this audit
            // reported: 69 clean, 0 inert, on a character whose class most of
            // those curses are not even for.
            Footprint attachBase = before;
            attachBase.carried = held.carried;
            std::vector<std::string> const did = Diff(attachBase, held);

            if (!lines.empty())
            {
                ++leaked;
                handler->PSendSysMessage("|cffff2020LEAK|r {} (rank {})", def.name, rank);
                for (std::string const& line : lines)
                    handler->PSendSysMessage("    {}", line);
            }
            else if (did.empty())
            {
                // Nothing measurable changed at attach, so there was nothing to
                // put back and "clean" would be flattering. Most of these are
                // mechanics that act on a hook rather than on attach, and the
                // rest are class curses for a class this character is not --
                // both are absences of evidence, not evidence.
                ++inert;
            }
            else
            {
                ++clean;
            }

            if (onlyOne)
            {
                handler->PSendSysMessage("  what it changed at attach ({}):",
                                         did.empty() ? "nothing" : "below");
                for (std::string const& line : did)
                    handler->PSendSysMessage("    {}", line);

                if (exercise)
                    handler->PSendSysMessage("  events released by the soak: {}", firedHere);

                if (!diag.empty())
                    handler->PSendSysMessage("  its own counters {}: {}",
                                             exercise ? "after the soak" : "before detach", diag);
                if (!diagAfter.empty() && diagAfter != diag)
                    handler->PSendSysMessage("  its own counters after detach: {}", diagAfter);
            }

            // After the verdict, never before it: cleaning up first would erase
            // the evidence and report every mechanic clean.
            //
            // Only these two are cleaned. A queued event would fire later with
            // nothing carried to blame it on, and a summon with no affix behind
            // it is the orphan-stalker failure -- both are the audit's own mess
            // and both are worse than the leak they came from. An aura or a
            // cooldown is left exactly as found: it is on the game master's own
            // character, a relog clears it, and stripping it here would make
            // the audit unable to tell a leak from its own cleanup on a second
            // run.
            if (Scheduler* clock = sGauntlet->ClockFor(p))
                clock->Cancel(def.id);
            sGauntletSummons->DespawnFor(p, def.id);
        }

        if (audited == 0)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Nothing to audit: {} already carried.",
                                      skipped);
            return false;
        }

        handler->PSendSysMessage(
            "|cffff2020[Gauntlet debug]|r {} at rank {}: {} audited, "
            "|cffff2020{} leaked|r, {} clean, {} inert{}.",
            exercise ? "soak" : "attach/detach audit",
            askedRank, audited, leaked, clean, inert,
            skipped ? Acore::StringFormat(", {} skipped as already carried", skipped) : "");

        if (offClass != 0)
            handler->PSendSysMessage(
                "  {} class curse(s) are not for this character's class and were not attached: the "
                "generator would never offer them, so a leak found there would not be reachable. Name "
                "one explicitly to audit it anyway.", offClass);

        if (leaked == 0)
            handler->PSendSysMessage("  Every one of them put back what it took.");

        handler->PSendSysMessage(
            "  \"inert\" means nothing measurable changed at attach -- a hook-driven mechanic, or a "
            "class curse for another class. It is not a pass.");

        if (exercise)
            handler->PSendSysMessage(
                "  Each was ticked {} times; {} event(s) were actually released across {} mechanic(s). "
                "The rest never armed anything, which is why they read inert. Hooks that need an enemy "
                "-- kills, damage, casts -- were not driven; those are still only in "
                "docs/checklists.md.", SOAK_TICKS, fired, drivenMechanics);
        else
            handler->PSendSysMessage(
                "  Nothing was driven: this is attach and detach only. .gauntlet debug soak makes each "
                "mechanic act first, which is the only way a hook-driven curse can leak in front of it.");

        handler->PSendSysMessage(
            "  Queued events and summons were cleaned up; any leaked aura or cooldown is still on you.");
        return true;
    }

    // Attach and detach, and nothing in between.
    static bool HandleDebugLeaks(ChatHandler* handler, Optional<PlayerIdentifier> whoArg,
                                 Optional<std::string_view> whatArg, Optional<uint32> rankArg)
    {
        return RunLeakAudit(handler, whoArg, whatArg, rankArg, /*exercise*/ false);
    }

    // The same audit, with the mechanic driven in between: ticked, and its own
    // events released. Slower, noisier, and the only one of the two that can
    // see what a hook-driven curse leaves behind.
    static bool HandleDebugSoak(ChatHandler* handler, Optional<PlayerIdentifier> whoArg,
                                Optional<std::string_view> whatArg, Optional<uint32> rankArg)
    {
        return RunLeakAudit(handler, whoArg, whatArg, rankArg, /*exercise*/ true);
    }

    // Every card, through every hook, with the answer derived rather than
    // declared.
    //
    //   .gauntlet debug bench                 every card this character can carry
    //   .gauntlet debug bench <name> class 4  from the console, one family
    //   .gauntlet debug bench <name> shade 4  one card, with its probe list
    //
    // `leaks` proved OnDetach puts things back. `soak` added the card's own
    // clock. Neither answers the question that actually matters as the registry
    // grows: **is this card reachable at all, and by what?**
    //
    // The bench attaches one affix and drives the whole of Mgr's dispatch
    // surface at it -- experience, healing, max health, the lethal path, loot
    // rolls, the repair bill, talent points, the three economy vetoes, all six
    // aggregate products flat and against a target, combat entry and exit,
    // damage dealt and taken, pet damage, periodic damage, a kill, a pet kill,
    // a zone change, a group change, forty ticks and its own events -- and
    // records which of them saw the card do something.
    //
    // Nothing in it is written per card and nothing has to be. A card added
    // next year is covered the day its registry row lands. What the summary
    // ends with is the list of cards **no probe reached**, which is the number
    // to watch: it is either a card that needs a condition the bench cannot
    // produce, or a card that does nothing at all, and both are worth knowing
    // before a player finds out.
    static bool HandleDebugBench(ChatHandler* handler, Optional<PlayerIdentifier> whoArg,
                                 Optional<std::string_view> whatArg, Optional<uint32> rankArg)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = whoArg ? whoArg->GetConnectedPlayer() : handler->GetPlayer();
        if (!p)
        {
            handler->SendErrorMessage(
                "|cffff2020[Gauntlet debug]|r No character to bench. From the console, name one who is "
                "online: .gauntlet debug bench <name> [what] [rank]");
            return false;
        }

        RunState* st = MutableRun(handler, p);
        if (!st)
            return false;

        bool everyFamily = true;
        Family wanted = Family::Class;
        MechanicDef const* onlyOne = nullptr;

        if (whatArg && !whatArg->empty() && !StringEqualI(*whatArg, "all"))
        {
            for (uint8 f = 0; f < static_cast<uint8>(Family::MAX); ++f)
                if (StringEqualI(FamilyName(static_cast<Family>(f)), *whatArg))
                {
                    wanted = static_cast<Family>(f);
                    everyFamily = false;
                    break;
                }

            if (everyFamily)
            {
                onlyOne = LookupMechanic(*whatArg);
                if (!onlyOne)
                {
                    handler->SendErrorMessage(
                        "|cffff2020[Gauntlet debug]|r \"{}\" is neither a family nor a mechanic.", *whatArg);
                    return false;
                }
            }
        }

        uint32 const askedRank = rankArg ? *rankArg : uint32(MAX_RANK);
        if (askedRank < 1 || askedRank > MAX_RANK)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Rank {} is out of range: 1 to {}.",
                                      askedRank, static_cast<uint32>(MAX_RANK));
            return false;
        }

        LivePlayerView const view(p);

        // Once, around the whole sweep. Inside a card's before/after it would
        // register as that card leaking a run-speed change, which is exactly
        // what the first hostile-target run reported against Reinforcements.
        BenchSetup const quieted = BenchQuiet(p, st);

        uint32 benched = 0, reached = 0, leaked = 0, offClass = 0, skipped = 0, spawnFailures = 0;
        std::string spawnFailed;
        std::string silent;
        std::string leakedNames;

        for (MechanicDef const& def : AllMechanics())
        {
            if (onlyOne)
            {
                if (def.id != onlyOne->id)
                    continue;
            }
            else if (!everyFamily && def.family != wanted)
                continue;

            if (st->Find(def.id))
            {
                ++skipped;
                continue;
            }

            if (!onlyOne && def.classMask != 0 && (def.classMask & view.GetClassMask()) == 0)
            {
                ++offClass;
                continue;
            }

            uint32 const rank = std::min<uint32>(askedRank, std::min<uint8>(def.maxRank, MAX_RANK));

            Footprint const before = Capture(p, st);

            AffixInstance* attached = AuditAttach(p, st, def, rank);
            if (!attached)
            {
                handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Ran out of affix slots.");
                break;
            }

            uint8 const slot = attached->slot;

            // What attaching alone changed. Probe's baseline is read *after*
            // attach, so a card whose whole effect lands in OnAttach -- a
            // standing aura, a helm put into the bags -- was invisible to the
            // bench's verdict and could read "reached by nothing" while
            // working; docs/handoff.md listed it as the harness's first blind
            // spot. The carried count moves on every attach and is not a
            // finding, so it is put back before the two readings are compared.
            Footprint attachedMark = Capture(p, st);
            attachedMark.carried = before.carried;
            std::vector<std::string> const onAttach = Diff(before, attachedMark);

            ProbeResult probe = Probe(p, st, slot, def.id, def.requiresSpell);
            for (std::string const& line : onAttach)
                probe.reached.emplace_back("on attach: " + line);

            if (st->dead || !p->IsAlive())
            {
                AuditDetach(p, st, slot);
                handler->SendErrorMessage(
                    "|cffff2020[Gauntlet debug]|r {} killed the character. {} benched before that; the "
                    "rest were not.", def.name, benched);
                return false;
            }

            AuditDetach(p, st, slot);

            // The bench's own residue, cleared after the detach rather than
            // before it. See ProbeResult::castCooldowns.
            for (uint32 id : probe.castCooldowns)
                p->RemoveSpellCooldown(id, /*update*/ true);

            Footprint const after = Capture(p, st);

            ++benched;

            std::vector<std::string> const lines = Diff(before, after);
            if (!lines.empty())
            {
                ++leaked;
                handler->PSendSysMessage("|cffff2020LEAK|r {} (rank {})", def.name, rank);
                for (std::string const& line : lines)
                    handler->PSendSysMessage("    {}", line);
                if (!leakedNames.empty())
                    leakedNames += ", ";
                leakedNames += def.key;
            }

            // A Spawn card that never put a creature in the world has failed,
            // whatever else it moved. "It armed a timer" is not the job.
            if (def.family == Family::Spawn && probe.maxSummons == 0)
            {
                ++spawnFailures;
                handler->PSendSysMessage(
                    "|cffff2020FAILED|r {} is a Spawn card and summoned nothing in the whole probe "
                    "({} event(s) released).", def.name, probe.eventsFired);
                if (!spawnFailed.empty())
                    spawnFailed += ", ";
                spawnFailed += def.key;
            }

            if (probe.Reached())
            {
                ++reached;
            }
            else
            {
                if (!silent.empty())
                    silent += ", ";
                silent += def.key;
            }

            if (onlyOne)
            {
                handler->PSendSysMessage("|cffff2020[{}]|r {} at rank {}", def.key, def.name, rank);
                if (probe.Reached())
                    for (std::string const& r : probe.reached)
                        handler->PSendSysMessage("    answered: {}", r);
                else
                    handler->PSendSysMessage(
                        "    |cffff8040no probe reached it|r -- it needs a condition this bench does not "
                        "produce, or it does nothing.");
                handler->PSendSysMessage("    events released: {}", probe.eventsFired);
                if (!probe.diagnose.empty())
                    handler->PSendSysMessage("    its own counters: {}", probe.diagnose);
            }

            if (Scheduler* clock = sGauntlet->ClockFor(p))
                clock->Cancel(def.id);
            sGauntletSummons->DespawnFor(p, def.id);
        }

        BenchRestore(p, st, quieted);

        if (benched == 0)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Nothing to bench.");
            return false;
        }

        handler->PSendSysMessage(
            "|cffff2020[Gauntlet debug]|r bench at rank {}: {} card(s), |cff60c060{} answered a probe|r, "
            "{} answered nothing, |cffff2020{} leaked|r.",
            askedRank, benched, reached, benched - reached, leaked);

        if (offClass != 0)
            handler->PSendSysMessage("  {} class curse(s) are not for this class and were not benched.",
                                     offClass);
        if (skipped != 0)
            handler->PSendSysMessage("  {} already carried and were not benched.", skipped);

        // The number that matters as the registry grows. A card nothing reached
        // is not a passing card; it is a card with no evidence either way, and
        // the list is what a new card should be checked against on the day it
        // lands.
        if (!silent.empty())
            handler->PSendSysMessage(
                "  |cffff8040Reached by nothing:|r {}. Each needs a condition this bench cannot produce "
                "-- or does nothing at all. That is the list to shrink.", silent);

        if (!leakedNames.empty())
            handler->PSendSysMessage("  |cffff2020Leaked:|r {}", leakedNames);

        if (spawnFailures != 0)
            handler->PSendSysMessage(
                "  |cffff2020Spawned nothing:|r {}. A Spawn card that summons nothing is broken, "
                "however many probes it answered.", spawnFailed);

        return true;
    }

    static bool HandleDebugDump(ChatHandler* handler)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = handler->GetPlayer();
        RunState* st = ReadableRun(handler, p);
        if (!st)
            return false;

        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r dump for {} (guid {}):",
                                 p->GetName(), p->GetGUID().GetCounter());
        handler->PSendSysMessage("  run: seed {} | tier {} | {} | generator {} | class {}",
                                 st->seed, st->tier, st->dead ? "RETIRED" : "alive",
                                 static_cast<uint32>(st->genVersion), static_cast<uint32>(st->playerClass));
        handler->PSendSysMessage("  death timer: {}", st->pendingDeath
                                 ? Acore::StringFormat("armed, {} ms left", st->deathTimerMs)
                                 : std::string("not armed"));
        handler->PSendSysMessage("  offers on the table: {} (built for tier {})",
                                 static_cast<uint32>(st->pending.size()), st->pendingTier);
        handler->PSendSysMessage("  reroll charges: {} | pending tier rerolled {} time(s)",
                                 static_cast<uint32>(Mgr::RerollCharges(*st)),
                                 static_cast<uint32>(Mgr::PendingRerolls(*st, st->pendingTier)));
        handler->PSendSysMessage("  unsaved changes to gauntlet_run: {}", st->dirty ? "yes" : "no");

        handler->PSendSysMessage("  affixes ({}):", static_cast<uint32>(st->affixes.size()));
        for (AffixInstance const& a : st->affixes)
        {
            MechanicDef const* def = FindMechanic(a.mechanic);
            handler->PSendSysMessage("    slot {} | id {} {} | rank {} | {} | cond {} ({}) | boon {} ({}) mag {}",
                                     static_cast<uint32>(a.slot), static_cast<uint32>(a.mechanic),
                                     def ? def->key : "<not in this registry>",
                                     static_cast<uint32>(a.rank), def ? RarityName(def->rarity) : std::string("?"),
                                     static_cast<uint32>(a.condition),
                                     ConditionName(a.condition), static_cast<uint32>(a.boon),
                                     BoonName(a.boon), static_cast<uint32>(a.boonMag));
            handler->PSendSysMessage("           generator {} | impl {} | offerable {}",
                                     static_cast<uint32>(a.genVersion),
                                     a.impl ? "yes" : "none",
                                     def ? (IsImplemented(*def) ? "yes" : "no") : "unknown");
            handler->PSendSysMessage("           {}", sGauntlet->DescribeOf(a));

            if (a.impl)
            {
                Ctx ctx = sGauntlet->MakeCtx(p, st, const_cast<AffixInstance*>(&a));
                std::string const internals = a.impl->Diagnose(ctx);
                if (!internals.empty())
                    handler->PSendSysMessage("           {}", internals);
            }
        }

        PrintProducts(handler, p);
        PrintScheduler(handler, p, *st);
        PrintSummons(handler, p);
        PrintState(handler, p, *st);
        return true;
    }

    static bool HandleDebugReroll(ChatHandler* handler, Optional<PlayerIdentifier> whoArg)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = whoArg ? whoArg->GetConnectedPlayer() : handler->GetPlayer();
        if (!p)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r No character. From the console, name one "
                                      "who is online: .gauntlet debug reroll <name>");
            return false;
        }

        bool const moved = sGauntlet->Reroll(p);
        if (moved)
            sGauntletAddon->SendSnapshot(p);

        RunState* st = sGauntlet->Get(p);
        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r reroll for {}: {}{}", p->GetName(),
                                 moved ? "done" : "refused",
                                 st ? Acore::StringFormat(" ({} charge(s) left, pending tier {} rerolled {} time(s))",
                                                          static_cast<uint32>(Mgr::RerollCharges(*st)), st->pendingTier,
                                                          static_cast<uint32>(Mgr::PendingRerolls(*st, st->pendingTier)))
                                    : std::string());
        return true;
    }

    static bool HandleDebugSkip(ChatHandler* handler, Optional<PlayerIdentifier> whoArg)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = whoArg ? whoArg->GetConnectedPlayer() : handler->GetPlayer();
        if (!p)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r No character. From the console, name one "
                                      "who is online: .gauntlet debug skip <name>");
            return false;
        }

        bool const moved = sGauntlet->Skip(p);
        if (moved)
            sGauntletAddon->SendSnapshot(p);

        RunState* st = sGauntlet->Get(p);
        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r skip for {}: {}{}", p->GetName(),
                                 moved ? "done" : "refused",
                                 st ? Acore::StringFormat(" (tier {}, {} charge(s) held)", st->tier,
                                                          static_cast<uint32>(Mgr::RerollCharges(*st)))
                                    : std::string());
        return true;
    }

    // .gauntlet debug offers <tier> [name]
    //
    // Tier first and the name second, the other way round from the audits,
    // because the tier is required: a leading Optional<PlayerIdentifier>
    // would try "45" as a character name first, and the core's parser backs
    // off from that only for an argument that can be *omitted*.
    static bool HandleDebugOffers(ChatHandler* handler, uint32 tierArg, Optional<PlayerIdentifier> whoArg)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = whoArg ? whoArg->GetConnectedPlayer() : handler->GetPlayer();
        if (!p)
        {
            handler->SendErrorMessage(
                "|cffff2020[Gauntlet debug]|r No character to build offers for. From the console or SOAP, "
                "name one who is online: .gauntlet debug offers <tier> <name>");
            return false;
        }

        RunState* st = ReadableRun(handler, p);
        if (!st)
            return false;

        if (tierArg < 1 || tierArg > 255)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Tier {} is out of range: 1 to 255.", tierArg);
            return false;
        }

        // Nothing here touches st->pending, so an offer already on the table
        // is left exactly as it was and this cannot be picked from.
        LivePlayerView view(p);
        // The realm's own view and carry cap, not the defaults: a family
        // switched off by Gauntlet.Family.<X>.Enable must be as absent here as
        // it is in the chooser, or this command answers a question nobody
        // asked.
        // The pending tier's reroll count rides along, so what this prints for
        // that tier is what the player's chooser is actually showing.
        uint8 const rerolls = Mgr::PendingRerolls(*st, tierArg);
        OfferSet const set = BuildOffers(st->seed, static_cast<uint8>(tierArg), view, st->affixes,
                                         sGauntlet->Choices(), sGauntlet->OfferView(),
                                         sGauntlet->MaxAffixes(), rerolls);

        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r would offer {} at tier {} - seed {}, class {}, level {}, "
                                 "{} carried{} (nothing is committed):",
                                 p->GetName(), tierArg, st->seed, static_cast<uint32>(p->getClass()),
                                 static_cast<uint32>(p->GetLevel()), static_cast<uint32>(st->affixes.size()),
                                 rerolls != 0 ? Acore::StringFormat(", {} reroll(s) folded in",
                                                                    static_cast<uint32>(rerolls))
                                              : std::string());

        for (uint32 i = 0; i < set.offers.size(); ++i)
        {
            Offer const& o = set.offers[i];
            if (o.mechanic == Gauntlet::MECHANIC_NONE)
            {
                handler->PSendSysMessage("  [{}] (empty) - nothing in the table fits this character at this "
                                         "tier.", i + 1);
                continue;
            }

            // The same throwaway implementation Mgr::OfferTier builds to
            // describe a line that no RunState owns; it lives for one message.
            AffixInstance preview;
            preview.mechanic  = o.mechanic;
            preview.rank      = o.rank;
            preview.condition = o.condition;
            preview.boon      = o.boon;
            preview.boonMag   = o.boonMag;

            std::unique_ptr<IMechanic> const impl(MakeMechanic(o.mechanic));
            preview.impl = impl.get();

            // "[n]" and not "n." on purpose: a preview is not an offer, and
            // the addon scrapes "<n>. name - desc" as one (Panel.lua:430).
            handler->PSendSysMessage("  [{}] {} - {}", i + 1,
                                     sGauntlet->NameOf(o.mechanic, o.condition, o.boon),
                                     sGauntlet->DescribeOf(preview));
            handler->PSendSysMessage("     id {} | rank {} | {} | {} | boon mag {}{}",
                                     static_cast<uint32>(o.mechanic), static_cast<uint32>(o.rank),
                                     RarityName(o.rarity), OfferKindName(o.kind), static_cast<uint32>(o.boonMag),
                                     o.kind == OfferKind::Swap
                                         ? Acore::StringFormat(" | swaps out slot {}",
                                                               static_cast<uint32>(o.swapSlot))
                                         : std::string());
        }

        // Which rules the builder had to drop to fill the set. The point of
        // printing it is that the degradation is visible rather than inferred:
        // the pool thins out at the last tiers of a long run, and this says so.
        std::string relaxed;
        if (set.relaxations & GR_RepeatedFamily)
            relaxed += relaxed.empty() ? "repeated family" : ", repeated family";
        if (set.relaxations & GR_RepeatedMechanic)
            relaxed += relaxed.empty() ? "repeated mechanic" : ", repeated mechanic";
        if (set.relaxations & GR_NoCandidate)
            relaxed += relaxed.empty() ? "no candidate" : ", no candidate";
        if (set.relaxations & GR_NoRewardShaped)
            relaxed += relaxed.empty() ? "no reward-shaped offer" : ", no reward-shaped offer";

        handler->PSendSysMessage("  relaxations: {}", relaxed.empty() ? "none" : relaxed);
        return true;
    }

    static bool HandleDebugSeed(ChatHandler* handler, uint32 seed)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = handler->GetPlayer();
        RunState* st = MutableRun(handler, p);
        if (!st)
            return false;

        uint32 const was = st->seed;
        st->seed = seed;

        // Mgr::Save writes tier and dead only, so the seed goes to its column
        // here rather than waiting for a save that would never carry it.
        uint32 const low = p->GetGUID().GetCounter();
        CharacterDatabase.Execute("UPDATE `gauntlet_run` SET `seed` = {} WHERE `guid` = {}", seed, low);

        LOG_INFO("module", "Gauntlet: {} reseeded {} (guid {}) from {} to {}.",
                 Actor(handler), p->GetName(), low, was, seed);

        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r CHEAT: seed {} -> {}", was, seed);

        // Offers are a function of the seed and are never stored, so any set
        // still on the table was built from the old one. Rebuilding for the
        // same tier both settles that and shows the new seed's answer; the
        // chat lines are OfferTier's own, unchanged.
        if (!st->pending.empty() && st->pendingTier != 0)
        {
            sGauntlet->OfferTier(p, st->pendingTier);
            sGauntletAddon->SendOffers(p);
        }
        return true;
    }

    static bool HandleDebugExportAddon(ChatHandler* handler, Optional<Tail> pathArg)
    {
        if (!DebugAllowed(handler))
            return false;

        // The worldserver's working directory is not this module's repository
        // and cannot be found from in here, so the default is a bare filename
        // and the absolute path is reported back and logged. A path argument
        // is how the file reaches somewhere a `docker cp` can pick it up.
        std::string path = "Data.lua";
        if (pathArg)
        {
            std::string_view arg = *pathArg;
            while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
                arg.remove_prefix(1);
            while (!arg.empty() && (arg.back() == ' ' || arg.back() == '\t'))
                arg.remove_suffix(1);

            if (!arg.empty())
                path = std::string(arg);
        }

        std::error_code ec;
        std::filesystem::path const target = std::filesystem::absolute(path, ec);
        if (ec)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Cannot resolve '{}': {}", path, ec.message());
            return false;
        }

        // Best effort; the open below is the real test, and a path whose
        // parent already exists produces no error either way.
        if (target.has_parent_path())
            std::filesystem::create_directories(target.parent_path(), ec);
        ec.clear();

        std::string const body = BuildAddonData();

        std::ofstream file(target, std::ios::binary | std::ios::trunc);
        file.write(body.data(), static_cast<std::streamsize>(body.size()));
        file.close();
        if (!file)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Could not write {}. Check the worldserver's "
                                      "permission on that directory.", target.string());
            return false;
        }

        LOG_INFO("module", "Gauntlet: {} exported the addon data table to {} ({} mechanics, version {}).",
                 Actor(handler), target.string(), static_cast<uint32>(AllMechanics().size()),
                 static_cast<uint32>(Addon::Version));

        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r Wrote {} mechanics at protocol version {}, {} bytes:",
                                 static_cast<uint32>(AllMechanics().size()),
                                 static_cast<uint32>(Addon::Version), static_cast<uint32>(body.size()));
        handler->PSendSysMessage("  {}", target.string());
        handler->PSendSysMessage("  Copy it to addon/GauntletUI/Data.lua; the addon loads it before Protocol.lua.");
        return true;
    }

    // ------------------------------------------------------------------
    // Present, and honest about being empty. Each takes a permissive tail so
    // that whatever a game master types reaches the answer instead of an
    // argument-parse failure that would print usage for a command that has
    // none yet.
    // ------------------------------------------------------------------

    // Plan section 5.2's last three. All of them existed as stubs from step 8
    // and said so; the scheduler entry point and the state store they needed
    // are here now.

    static bool HandleDebugFire(ChatHandler* handler, std::string_view keyOrId)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = handler->GetPlayer();
        RunState* st = MutableRun(handler, p);
        if (!st)
            return false;

        MechanicDef const* def = LookupMechanic(keyOrId);
        if (!def)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r No mechanic '{}'. Give a registry id or a "
                                      "key such as 'falling_sky'.", keyOrId);
            return false;
        }

        if (!st->Find(def->id))
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r {} is not carried. Use .gauntlet debug give "
                                      "first.", def->name);
            return false;
        }

        if (!sGauntlet->FireNow(p, def->id))
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r {} has nothing queued. Its clock arms when the "
                                      "affix's own conditions are met -- Falling Sky and Falter in combat, Ambush "
                                      "while standing still. .gauntlet debug dump shows the queue.", def->name);
            return false;
        }

        LOG_INFO("module", "Gauntlet: {} fired {} (id {}) early for {} (guid {}).",
                 Actor(handler), def->name, static_cast<uint32>(def->id),
                 p->GetName(), p->GetGUID().GetCounter());

        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r CHEAT: released {}'s queued event now. Its warning "
                                 "was delivered first if it had not already gone out.", def->name);
        return true;
    }

    static bool HandleDebugSet(ChatHandler* handler, std::string_view key, int32 value)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* p = handler->GetPlayer();
        RunState* st = MutableRun(handler, p);
        if (!st)
            return false;

        if (key.empty() || key.size() > State::MaxKeyLen)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r A state key is 1 to {} characters. "
                                      "gauntlet_state.k is VARCHAR({}), and State::Set refuses anything longer "
                                      "rather than truncating two keys onto the same row.",
                                      static_cast<uint32>(State::MaxKeyLen),
                                      static_cast<uint32>(State::MaxKeyLen));
            return false;
        }

        int32 const was = st->state.Get(key, 0);
        st->state.Set(key, value);

        // Written straight through rather than left to the sixty-second
        // periodic save: a game master who sets a key and then relogs to see
        // what happens must not find the old value waiting.
        st->state.SaveTo(p->GetGUID().GetCounter());

        // The mechanics read gauntlet_state in OnAttach and cache what they
        // find, so a key set under a live affix is in the database and not in
        // the object. Say so rather than letting it look like nothing happened;
        // Deep Wounds is the one this matters for, and re-attaching it with
        // .gauntlet debug remove / give is how to make it take.
        LOG_INFO("module", "Gauntlet: {} set state '{}' from {} to {} for {} (guid {}).",
                 Actor(handler), key, was, value, p->GetName(), p->GetGUID().GetCounter());

        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r CHEAT: {} = {} (was {}), written to gauntlet_state.",
                                 key, value, was);
        handler->PSendSysMessage("  A carried mechanic reads its keys once, in OnAttach, so this reaches it on the "
                                 "next login -- or now, with .gauntlet debug remove <slot> and give.");
        return true;
    }

    // `.gauntlet debug hurt <percent>` -- take a blow of your own choosing.
    //
    // It exists because of Last Rites. The affix only acts on a blow that
    // would kill, and on a hardcore realm the only way to produce one is to
    // stake the run on the answer: if the cheat death is broken, finding that
    // out costs the character. That is not a test anyone can run twice.
    //
    // The damage goes through Unit::DealDamage -- the same static entry point
    // every real blow takes -- so it passes through this module's own
    // UnitScript override and therefore through IMechanic::OnLethal and
    // OnDamageTaken exactly as a mob's swing would. Nothing here special-cases
    // the caller; `hurt 100` is a genuine killing blow and will end the run if
    // no bargain catches it.
    //
    // It is also the readiest way to see Deep Wounds make a wound, Cursed
    // Hoard's triple actually triple, and the Mark's heal ceiling bite.
    static bool HandleDebugHurt(ChatHandler* handler, Optional<uint32> pct)
    {
        if (!DebugAllowed(handler))
            return false;

        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player || !player->IsAlive())
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r No living character.");
            return false;
        }

        uint32 const share = pct ? std::min<uint32>(*pct, 100u) : 100u;
        uint32 const max   = uint32(player->GetMaxHealth());
        uint32 const want  = uint32(uint64(max) * share / 100u);

        if (want == 0)
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r That is no damage at all.");
            return false;
        }

        uint32 const before = uint32(player->GetHealth());

        handler->PSendSysMessage(
            "|cffff2020[Gauntlet debug]|r Dealing {} damage ({}% of {} maximum). Health is {}.",
            want, share, max, before);

        // GM protection would swallow it, and a game master is exactly who
        // runs this, so allowGM is set. Durability is spared: this is a test,
        // not a death.
        Unit::DealDamage(player, player, want, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL,
                         nullptr, /*durabilityLoss*/ false, /*allowGM*/ true);

        if (player->IsAlive())
            handler->PSendSysMessage(
                "|cffff2020[Gauntlet debug]|r Still standing at {} of {}.",
                uint32(player->GetHealth()), max);

        return true;
    }

    static bool HandleDebugEvents(ChatHandler* handler, Optional<std::string_view> onOff)
    {
        if (!DebugAllowed(handler))
            return false;

        if (!onOff)
        {
            handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r scheduled events are {}.",
                                     sGauntlet->EventsEnabled() ? "on" : "off");
            return true;
        }

        bool enable;
        if (*onOff == "on" || *onOff == "1")
            enable = true;
        else if (*onOff == "off" || *onOff == "0")
            enable = false;
        else
        {
            handler->SendErrorMessage("|cffff2020[Gauntlet debug]|r Say 'on' or 'off'.");
            return false;
        }

        sGauntlet->SetEventsEnabled(enable);

        LOG_INFO("module", "Gauntlet: {} switched scheduled events {}.", Actor(handler), enable ? "on" : "off");

        handler->PSendSysMessage("|cffff2020[Gauntlet debug]|r CHEAT: scheduled events {} for every player, until "
                                 "the worldserver restarts or the config is reloaded. The file's own value is "
                                 "Gauntlet.Events.Enable.", enable ? "on" : "off");
        if (!enable)
            handler->PSendSysMessage("  Every queued event was cancelled; the clocks re-arm when events come back.");
        return true;
    }
};

void AddSC_gauntlet_commands()
{
    new GauntletCommandScript();
}
