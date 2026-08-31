/*
 * mod-gauntlet - the GNT addon channel
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef MOD_GAUNTLET_ADDON_H
#define MOD_GAUNTLET_ADDON_H

#include "Gauntlet.h"
#include "ObjectGuid.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Player;

namespace Gauntlet
{
    // The GNT channel (plan section 4): a LANG_ADDON whisper from the player
    // to themself, which the client surfaces as CHAT_MSG_ADDON and never as a
    // visible line. The payload is "GNT\t<type>\t<fields...>"; the client
    // splits the first tab off itself, so the addon's CHAT_MSG_ADDON handler
    // sees prefix "GNT" and message "<type>\t<fields...>".
    //
    // The class name is not free: GauntletMechanic.h forward-declares
    // Gauntlet::Addon for Ctx::addon, so Phase 1's mechanics already talk to
    // this type.
    class Addon
    {
    public:
        static Addon* instance();

        // Announced in HELLO. It is GeneratorVersion because the addon's
        // Data.lua is generated from the registry: the change that moves a
        // mechanic id is exactly the change that must invalidate the addon's
        // table, and Protocol.lua's PROTOCOL_VERSION is pinned to the same
        // number.
        static constexpr uint16 Version = GeneratorVersion;

        // A hard ceiling in both directions, not a style rule: the core drops
        // an inbound addon message longer than this
        // ($CORE/src/server/game/Handlers/ChatHandler.cpp:290) and the 3.3.5
        // client's chat buffer is the same size.
        static constexpr std::size_t MaxPayload = 255;

        // Plan section 4's coalescing budget. The window is one second and
        // the flush cadence is 500 ms, so a saturated queue drains at most
        // four messages per flush.
        static constexpr uint32 RateLimitPerSecond = 8;
        static constexpr uint32 FlushIntervalMs    = 500;
        static constexpr uint32 RateWindowMs       = 1000;

        // The floor between two snapshots a client asked for. A snapshot is
        // six or more packets out for one packet in, so without it a client
        // that sent SYNC every frame would be an amplifier pointed at its own
        // session. Nothing the server decides to send is subject to it.
        static constexpr uint32 SnapshotFloorMs = 1000;

        // ------------------------------------------------------------------
        // Lifecycle
        // ------------------------------------------------------------------

        // HELLO plus the login snapshot. Safe to call for a retired run: the
        // addon wants to draw the tombstone too.
        void OnLogin(Player* player);

        // Drops every trace of a character. Called unconditionally from the
        // logout hook, so a session can never outlive the player who owned
        // it; erasing a guid that was never here is a no-op.
        void Forget(ObjectGuid guid);

        // Drives the 500 ms flush. Called from PlayerScript::OnPlayerUpdate,
        // which runs at the world tick rate for every in-world player, so the
        // first thing it does is the integer test below.
        void Update(Player* player, uint32 diffMs);

        // True when no player anywhere has anything queued, which in Phase 0
        // is always: nothing calls the Queue* functions yet.
        bool Idle() const { return _queued == 0; }

        // ------------------------------------------------------------------
        // Client -> server
        // ------------------------------------------------------------------

        // Handles one inbound chat message. Returns true when it was a GNT
        // frame, in which case the caller must veto the chat line so it never
        // echoes. Everything about `msg` is client-controlled.
        bool HandleIncoming(Player* player, std::string const& msg);

        // ------------------------------------------------------------------
        // Server -> client, emitted in Phase 0
        // ------------------------------------------------------------------

        void SendHello(Player* player);

        // RUN seed tier state class rerollCharges
        void SendRun(Player* player);

        // AFFIX slot id rarity cond boon boonMag, one per carried affix, then
        // AFFIX_END. The pair is the authoritative carried set: the addon
        // replaces rather than merges on AFFIX_END. The third field carried a
        // rank until the rank system went; the rarity took its place.
        void SendAffixes(Player* player);

        // OFFER i id rarity cond boon boonMag kind swapSlot, then OFFER_END.
        // Sends nothing at all when no offer is on the table, because
        // OFFER_END is what makes the addon raise its chooser.
        void SendOffers(Player* player);

        // RUN, the affix list and any pending offers: what login and SYNC
        // send, and what a pick sends afterwards.
        void SendSnapshot(Player* player);

        // TOP rank name tier level cause conducts, one per leaderboard row.
        void SendTop(Player* player, uint32 rank, std::string const& name, uint32 tier,
                     uint32 level, std::string const& cause, std::string const& conducts);

        // ------------------------------------------------------------------
        // Server -> client, declared and implemented for Phase 1
        //
        // Nothing in Phase 0 calls any of these. They exist now so a Phase 1
        // mechanic holding a Ctx::addon has a surface to call, and so the
        // wire format for the live traffic is fixed at the same time as the
        // snapshot's rather than being invented later.
        // ------------------------------------------------------------------

        // EVT key secs label - a scheduler countdown; secs == 0 means fired.
        // Bypasses the queue: a countdown that arrives late is wrong.
        void SendEvent(Player* player, std::string_view key, uint32 secs, std::string_view label);

        // KILLBY id name - which affix acted last on a death. Bypasses the
        // queue for the same reason: the session is ending.
        void SendKilledBy(Player* player, uint16 mechanic, std::string_view name);

        // SUMMON key 0/1 - a stalker or ambusher is alive for you.
        void SendSummon(Player* player, std::string_view key, bool alive);

        // TOTALS taken done heal maxhp speed xp gold - what the whole carried
        // set adds up to, each as a percentage (100 = unchanged).
        //
        // Every number here is the server's, not a sum the addon worked out.
        // Six of the seven are Mgr::Aggregate products, which are the values
        // after plan section 2.5's caps have clamped them -- so a run whose
        // damage taken would be x2.4 shows the x2.0 it actually gets. An addon
        // adding up the boons it can see would print the first number and the
        // player would take the second.
        void SendTotals(Player* player);

        // PACE timed budgetPct minSpacingSecs - what the run's timed affixes
        // are doing to every cadence in it.
        //
        // Every timed affix's blurb states the interval the *mechanic* asks
        // for, because that is the only number it knows. The scheduler then
        // multiplies it by the event budget, so a player carrying six timed
        // affixes reads "every 20 seconds" and waits forty-five. That is not a
        // number any one mechanic can correct -- the stretch is a property of
        // the whole carried set -- so it is stated once, here, and the addon
        // puts it where the affix list is read.
        void SendPace(Player* player, uint32 timed, uint32 budgetPct, uint32 minSpacingSecs);

        // The three coalescing types. Each keeps only the latest value per
        // key and is flushed on the next 500 ms tick, under the rate limit.
        void QueueCounter(Player* player, std::string_view key, uint32 value, uint32 max);
        void QueueStat(Player* player, std::string_view key, int32 value);
        void QueueCondition(Player* player, uint8 slot, bool active);

    private:
        // One character's channel state. Everything per-player lives here and
        // nowhere else, so Forget is the whole of the cleanup.
        struct Session
        {
            // Set when the client sends its first GNT frame. Phase 1 uses it
            // to skip queueing for a player with no addon; Phase 0's snapshot
            // is sent unconditionally at login, because at that moment the
            // answer is not known yet and six packets are cheaper than a
            // round trip.
            bool   handshaken = false;

            // Counts up to FlushIntervalMs out of the world tick's diff.
            uint32 flushTimerMs = 0;

            // The rate window is anchored to the server clock rather than
            // accumulated from diffs, because Update only runs while there is
            // something queued: an accumulator would freeze mid-window every
            // time the queue drained and throttle the next burst against a
            // count from minutes ago.
            uint64 windowStartMs = 0;
            uint32 sentInWindow  = 0;

            // When the last client-requested snapshot went out. Zero means
            // none has, which is why the first SYNC after login always
            // answers -- that one is how a client that missed the login HELLO
            // recovers, and refusing it would defeat the whole handshake.
            uint64 lastSnapshotMs = 0;

            // Latest value per key, in arrival order. A vector rather than a
            // map: the queue holds a handful of entries at most, and the
            // flush wants the order the mechanics produced them in.
            std::vector<std::pair<std::string, std::string>> queue;
        };

        Session& SessionFor(Player* player);
        Session* FindSession(Player* player);

        // Replaces the entry with this key, or appends it. `key` is the
        // coalescing key: the type and whatever identifies the reading.
        void Enqueue(Player* player, std::string key, std::string payload);

        void Flush(Player* player, Session& session);

        // Prepends "GNT\t", refuses anything over MaxPayload and writes the
        // packet. Every message on this channel goes through here.
        void Emit(Player* player, std::string const& body);

        // Eligibility, an open session and the module being switched on. The
        // single gate in front of every send.
        bool CanSend(Player* player) const;

        void HandlePick(Player* player, Session& session, std::string_view field);

        // A snapshot in answer to an inbound frame, refused when it comes
        // inside SnapshotFloorMs of the last one.
        void RequestedSnapshot(Player* player, Session& session);

        std::unordered_map<ObjectGuid, Session> _sessions;

        // How many sessions have a non-empty queue. Update runs for every
        // player on every world tick, so the common case has to be an integer
        // test rather than a hash lookup -- the same shape Mgr uses for its
        // pending deaths.
        uint32 _queued = 0;
    };
}

#define sGauntletAddon Gauntlet::Addon::instance()

#endif // MOD_GAUNTLET_ADDON_H
