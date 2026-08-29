/*
 * mod-gauntlet - the GNT addon channel
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletAddon.h"
#include "GauntletMgr.h"
#include "Chat.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <cstddef>
#include <algorithm>
#include <string>

namespace Gauntlet
{
    namespace
    {
        // The prefix is a field of its own, not part of the type. The 3.3.5
        // client splits the first tab off an addon whisper and hands the two
        // halves to CHAT_MSG_ADDON as `prefix` and `message`, which is why the
        // core's own handler writes "AzerothCore\ta"
        // ($CORE/src/server/game/Chat/Chat.cpp:1090) and why Protocol.lua
        // expects to receive "GNT" separately from "<type>\t<fields...>".
        constexpr char const* GNT_PREFIX = "GNT";

        // The wire spellings of OfferKind. Deliberately not OfferKindName(),
        // which returns the player-facing "Rank up": these are the tokens
        // Panel.lua's KIND_COLOR table is keyed by.
        char const* WireKind(OfferKind kind)
        {
            switch (kind)
            {
                case OfferKind::RankUp:  return "rankup";
                case OfferKind::Swap:    return "swap";
                case OfferKind::Bargain: return "bargain";
                default:                 return "new";
            }
        }

        // Backs `n` off to the nearest UTF-8 boundary at or below itself, so
        // truncating a character name never leaves half a sequence on the
        // wire. Continuation bytes are 10xxxxxx, so a cut at any byte that is
        // not one lands on the start of a character and everything before it
        // is whole.
        std::size_t Utf8Floor(std::string_view s, std::size_t n)
        {
            if (n >= s.size())
                return s.size();

            while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
                --n;
            return n;
        }

        // A parser for one client-supplied decimal field. Refuses an empty
        // field, anything that is not a digit, and anything above `limit`, so
        // no inbound value can overflow or index past the end of a vector.
        // This is why the module has no atoi in it.
        bool ParseUInt(std::string_view s, uint32 limit, uint32& out)
        {
            if (s.empty() || s.size() > 10)
                return false;

            uint64 value = 0;
            for (char c : s)
            {
                if (c < '0' || c > '9')
                    return false;
                value = value * 10 + static_cast<uint64>(c - '0');
                if (value > limit)
                    return false;
            }
            out = static_cast<uint32>(value);
            return true;
        }

        // Cuts a comma-separated list down to `budget` bytes at the last
        // complete entry that fits, marking that something was dropped.
        std::string TrimList(std::string_view list, std::size_t budget)
        {
            if (list.size() <= budget)
                return std::string(list);

            constexpr std::string_view marker = ", ...";
            if (budget <= marker.size())
                return std::string();

            std::size_t const room = budget - marker.size();
            std::size_t cut = list.rfind(", ", room);
            if (cut == std::string_view::npos)
                cut = Utf8Floor(list, room);   // one enormous entry: hard cut

            return std::string(list.substr(0, cut)) + std::string(marker);
        }

        // One outbound message under construction. Every field goes through
        // here, which is the only place a tab is ever written, and the only
        // place a string from the database is scrubbed.
        class Frame
        {
        public:
            explicit Frame(char const* type)
            {
                _text.reserve(64);
                _text  = GNT_PREFIX;
                _text += '\t';
                _text += type;
            }

            // int64 rather than an overload set: every field the protocol
            // carries is a small non-negative integer or a signed reading,
            // both of which print identically through one parameter, and a
            // single signature cannot be picked wrongly by promotion.
            Frame& Num(int64 v)
            {
                _text += '\t';
                _text += std::to_string(v);
                return *this;
            }

            Frame& Text(std::string_view v, std::size_t budget)
            {
                _text += '\t';

                std::size_t const n = Utf8Floor(v, budget);
                for (std::size_t i = 0; i < n; ++i)
                {
                    // A tab would forge a field boundary and a newline would
                    // forge a message. Neither can come out of the registry,
                    // but a character name and a leaderboard row are data
                    // nobody in this module has promised to keep clean.
                    unsigned char const c = static_cast<unsigned char>(v[i]);
                    _text += (c < 0x20 || c == 0x7F) ? ' ' : v[i];
                }
                return *this;
            }

            // Bytes a further field may carry, its separating tab already
            // subtracted.
            std::size_t Room() const
            {
                return _text.size() + 1 >= Addon::MaxPayload ? 0 : Addon::MaxPayload - _text.size() - 1;
            }

            std::string const& Str() const { return _text; }

        private:
            std::string _text;
        };
    }

    Addon* Addon::instance()
    {
        static Addon instance;
        return &instance;
    }

    // =====================================================================
    // Transport
    // =====================================================================

    bool Addon::CanSend(Player* player) const
    {
        return player && player->GetSession() && sGauntlet->Enabled() && sGauntlet->IsEligible(player);
    }

    void Addon::Emit(Player* player, std::string const& body)
    {
        if (body.size() > MaxPayload)
        {
            // Every builder in this file sizes its own fields, so reaching
            // here is a bug rather than a big leaderboard row. Dropping is the
            // only safe answer: the client's chat buffer is 255 bytes and the
            // core refuses the same length inbound.
            LOG_ERROR("module", "Gauntlet: refusing a {}-byte GNT frame (max {}): {}",
                      body.size(), MaxPayload, body);
            return;
        }

        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, body);
        player->GetSession()->SendPacket(&data);
    }

    // =====================================================================
    // Per-player state
    // =====================================================================

    Addon::Session& Addon::SessionFor(Player* player)
    {
        return _sessions[player->GetGUID()];
    }

    Addon::Session* Addon::FindSession(Player* player)
    {
        if (!player)
            return nullptr;

        auto it = _sessions.find(player->GetGUID());
        return it == _sessions.end() ? nullptr : &it->second;
    }

    void Addon::Forget(ObjectGuid guid)
    {
        auto it = _sessions.find(guid);
        if (it == _sessions.end())
            return;

        if (!it->second.queue.empty() && _queued != 0)
            --_queued;

        _sessions.erase(it);
    }

    void Addon::OnLogin(Player* player)
    {
        if (!CanSend(player))
            return;

        // The session exists from login rather than from the first inbound
        // frame, so the flush timer has somewhere to live even for a player
        // whose addon never answers; `handshaken` is what distinguishes them.
        SessionFor(player);

        SendHello(player);
        SendSnapshot(player);
    }

    void Addon::Update(Player* player, uint32 diffMs)
    {
        // Runs for every in-world player on every world tick, so the first
        // test has to be an integer one. In Phase 0 nothing queues and this is
        // where the function always returns.
        if (_queued == 0)
            return;

        Session* session = FindSession(player);
        if (!session || session->queue.empty())
            return;

        if (!CanSend(player))
        {
            // Switched off, or the player stopped being eligible, while
            // something was queued. The queue is a live readout: dropping it
            // is right and holding it is not, and it keeps _queued honest.
            session->queue.clear();
            if (_queued != 0)
                --_queued;
            return;
        }

        session->flushTimerMs += diffMs;
        if (session->flushTimerMs < FlushIntervalMs)
            return;

        session->flushTimerMs = 0;
        Flush(player, *session);
    }

    void Addon::Enqueue(Player* player, std::string key, std::string payload)
    {
        if (!CanSend(player))
            return;

        // No addon on the other end means no reason to hold a queue: the
        // coalesced types are a HUD feed and nothing else consumes them.
        Session* session = FindSession(player);
        if (!session || !session->handshaken)
            return;

        for (auto& entry : session->queue)
        {
            if (entry.first == key)
            {
                entry.second = std::move(payload);   // only the latest value survives
                return;
            }
        }

        bool const wasEmpty = session->queue.empty();
        session->queue.emplace_back(std::move(key), std::move(payload));
        if (wasEmpty)
            ++_queued;
    }

    void Addon::Flush(Player* player, Session& session)
    {
        uint64 const now = static_cast<uint64>(GameTime::GetGameTimeMS().count());
        if (now - session.windowStartMs >= RateWindowMs)
        {
            session.windowStartMs = now;
            session.sentInWindow  = 0;
        }

        std::size_t sent = 0;
        while (sent < session.queue.size() && session.sentInWindow < RateLimitPerSecond)
        {
            Emit(player, session.queue[sent].second);
            ++session.sentInWindow;
            ++sent;
        }

        if (sent == 0)
            return;

        session.queue.erase(session.queue.begin(), session.queue.begin() + static_cast<std::ptrdiff_t>(sent));
        if (session.queue.empty() && _queued != 0)
            --_queued;
    }

    // =====================================================================
    // Server -> client, Phase 0
    // =====================================================================

    void Addon::SendHello(Player* player)
    {
        if (!CanSend(player))
            return;

        Frame f("HELLO");
        f.Num(Version);
        Emit(player, f.Str());
    }

    void Addon::SendRun(Player* player)
    {
        if (!CanSend(player))
            return;

        RunState const* st = sGauntlet->Get(player);
        if (!st)
            return;

        Frame f("RUN");
        f.Num(st->seed);
        f.Num(st->tier);
        // Panel.lua lowercases this and compares it to "alive"; the chat
        // fallback it replaces reads the same two words off `.gauntlet status`.
        f.Text(st->dead ? "retired" : "alive", 16);
        f.Num(st->playerClass);
        Emit(player, f.Str());
    }

    // "GNT\tODESC\t<index>\t" is thirteen bytes at three digits of index, and
    // Frame refuses anything over MaxPayload, so this leaves a wide margin
    // rather than sitting on the limit.
    constexpr std::size_t DESC_CHUNK = 200;

    // One mechanic's own sentence, sent as however many frames it takes.
    //
    // Both the offer list and the carried list need it and for the same
    // reason: MechanicDef::blurb is one static line per registry row, so it
    // says the same thing at rank I and rank III. Reinforcements III really
    // draws an enemy after twenty seconds and then every ten, and the blurb
    // said "longer than 30 seconds ... every 15 seconds" -- the rank I numbers
    // -- in the panel of a player carrying rank III.
    //
    // Split on a space so a chunk boundary never lands inside a word, and drop
    // the space itself: the addon rejoins the pieces with exactly one.
    //
    // Leaving it on the end of a chunk did not survive the trip. The first
    // version broke "... in dungeons. In exchange, you have 5% more health."
    // into a chunk ending in "dungeons. " and one starting "In exchange", and
    // what arrived was "dungeons.In exchange" -- the trailing space is trimmed
    // somewhere between here and CHAT_MSG_ADDON. Neither side may rely on a
    // space at a message boundary, so neither side sends one.
    template <typename Emitter>
    void EmitDescription(char const* type, int64 key, std::string const& desc, Emitter&& emit)
    {
        for (std::size_t at = 0; at < desc.size(); )
        {
            std::size_t take = std::min<std::size_t>(DESC_CHUNK, desc.size() - at);
            std::size_t skip = 0;

            if (at + take < desc.size())
            {
                std::size_t const space = desc.rfind(' ', at + take);
                if (space != std::string::npos && space > at)
                {
                    take = space - at;   // up to, not including, the space
                    skip = 1;
                }
            }

            emit(type, key, desc.substr(at, take));
            at += take + skip;
        }
    }

    void Addon::SendAffixes(Player* player)
    {
        if (!CanSend(player))
            return;

        RunState const* st = sGauntlet->Get(player);
        if (!st)
            return;

        for (AffixInstance const& a : st->affixes)
        {
            Frame f("AFFIX");
            f.Num(a.slot);
            f.Num(a.mechanic);
            f.Num(a.rank);
            f.Num(static_cast<int64>(a.condition));
            f.Num(static_cast<int64>(a.boon));
            f.Num(a.boonMag);
            Emit(player, f.Str());

            // The carried affix's own sentence at the rank it is actually at,
            // keyed by slot. Without it the panel's tooltip draws the registry
            // blurb, which is the rank I wording for every rank.
            EmitDescription("ADESC", a.slot, sGauntlet->DescribeOf(a),
                            [this, player](char const* type, int64 key, std::string const& part)
                            {
                                Frame d(type);
                                d.Num(key);
                                d.Text(part, DESC_CHUNK);
                                Emit(player, d.Str());
                            });
        }

        // Always sent, even for an empty carried set: AFFIX_END is what makes
        // the list authoritative, and the addon replaces rather than merges on
        // it. A run with nothing borne has to be able to say so.
        Emit(player, Frame("AFFIX_END").Str());
    }

    void Addon::SendOffers(Player* player)
    {
        if (!CanSend(player))
            return;

        RunState const* st = sGauntlet->Get(player);

        // Nothing at all when there is no offer. OFFER_END is what raises the
        // addon's chooser, so an empty pair would pop an empty window rather
        // than say "no offers"; the protocol has no way to say the latter.
        if (!st || st->pending.empty())
            return;

        for (std::size_t i = 0; i < st->pending.size(); ++i)
        {
            Offer const& o = st->pending[i];

            // An offer the generator could not fill is still sent. Its index
            // has to stay aligned with `.gauntlet pick <number>` and with
            // Mgr::Pick's own bounds check, and the addon renders an unknown
            // mechanic id as a question mark rather than crashing on it.
            Frame f("OFFER");
            f.Num(static_cast<int64>(i + 1));
            f.Num(o.mechanic);
            f.Num(o.rank);
            f.Num(static_cast<int64>(o.condition));
            f.Num(static_cast<int64>(o.boon));
            f.Num(o.boonMag);
            f.Text(WireKind(o.kind), 16);
            f.Num(o.swapSlot);
            Emit(player, f.Str());

            // The mechanic's own sentence, at the rank this offer is
            // promising. Without it the panel can only draw
            // MechanicDef::blurb, which is one static line per row -- so a
            // RANK UP offer read exactly like the NEW offer beside it and told
            // the player nothing about what the rank changed. They are being
            // asked to accept a permanent number that nothing has shown them.
            //
            // Sent as its own frame rather than a field on OFFER because the
            // sentences run to 250 characters and MaxPayload is 255. Long ones
            // arrive as several ODESC frames for the same index and the addon
            // joins them, so nothing has to be truncated at a word boundary
            // that a future rank might move.
            EmitDescription("ODESC", static_cast<int64>(i + 1), sGauntlet->DescribeOffer(o),
                            [this, player](char const* type, int64 key, std::string const& part)
                            {
                                Frame d(type);
                                d.Num(key);
                                d.Text(part, DESC_CHUNK);
                                Emit(player, d.Str());
                            });
        }

        Emit(player, Frame("OFFER_END").Str());
    }

    void Addon::SendSnapshot(Player* player)
    {
        SendRun(player);
        SendAffixes(player);
        SendOffers(player);
    }

    void Addon::SendTop(Player* player, uint32 rank, std::string const& name, uint32 tier,
                        uint32 level, std::string const& cause, std::string const& conducts)
    {
        if (!CanSend(player))
            return;

        Frame f("TOP");
        f.Num(rank);
        f.Text(name, 48);      // gauntlet_leaderboard.name is VARCHAR(12)
        f.Num(tier);
        f.Num(level);
        f.Text(cause, 48);     // gauntlet_leaderboard.cause is VARCHAR(32)

        // The one Phase 0 message that can pass 255 bytes: conducts is a
        // VARCHAR(255) and everything in front of it can take sixty. The
        // protocol has no continuation field -- splitting a TOP in two would
        // make the addon print two leaderboard rows with the same rank -- so
        // the list is cut at the last complete conduct that fits and marked,
        // rather than split. // TODO(design): truncation was chosen over a
        // TOP_CONT frame because the addon parser is already frozen.
        std::size_t const room = f.Room();
        f.Text(TrimList(conducts, room), room);

        Emit(player, f.Str());
    }

    // =====================================================================
    // Server -> client, declared and implemented for Phase 1
    //
    // Nothing below is called anywhere in Phase 0. Each one is the whole of
    // its message, so a Phase 1 mechanic holding a Ctx::addon calls it and
    // nothing else has to be written.
    // =====================================================================

    void Addon::SendEvent(Player* player, std::string_view key, uint32 secs, std::string_view label)
    {
        if (!CanSend(player))
            return;

        Frame f("EVT");
        f.Text(key, 32);
        f.Num(secs);

        // The label is free text from a mechanic, so it takes whatever is
        // left rather than a fixed budget.
        std::size_t const room = f.Room();
        f.Text(label, room);
        Emit(player, f.Str());
    }

    void Addon::SendKilledBy(Player* player, uint16 mechanic, std::string_view name)
    {
        if (!CanSend(player))
            return;

        Frame f("KILLBY");
        f.Num(mechanic);

        std::size_t const room = f.Room();
        f.Text(name, room);
        Emit(player, f.Str());
    }

    void Addon::SendSummon(Player* player, std::string_view key, bool alive)
    {
        if (!CanSend(player))
            return;

        // Plan section 4 names only STAT, COND and CTR as coalescing and only
        // EVT and KILLBY as bypassing the queue, leaving SUMMON unsaid. It is
        // sent immediately: it fires when a stalker spawns or dies, which is
        // rare and which the player is being warned about.
        // TODO(design): SUMMON's queueing is unspecified; chosen immediate.
        Frame f("SUMMON");
        f.Text(key, 32);
        f.Num(alive ? 1 : 0);
        Emit(player, f.Str());
    }

    void Addon::SendPace(Player* player, uint32 timed, uint32 budgetPct, uint32 minSpacingSecs)
    {
        if (!CanSend(player))
            return;

        // Sent immediately rather than queued: it moves only when the carried
        // set does, which is once a level at most, and it is read alongside the
        // affix list it explains.
        Frame f("PACE");
        f.Num(timed);
        f.Num(budgetPct);
        f.Num(minSpacingSecs);
        Emit(player, f.Str());
    }

    void Addon::QueueCounter(Player* player, std::string_view key, uint32 value, uint32 max)
    {
        Frame f("CTR");
        f.Text(key, 32);
        f.Num(value);
        f.Num(max);
        Enqueue(player, "CTR:" + std::string(key), f.Str());
    }

    void Addon::QueueStat(Player* player, std::string_view key, int32 value)
    {
        Frame f("STAT");
        f.Text(key, 32);
        f.Num(value);
        Enqueue(player, "STAT:" + std::string(key), f.Str());
    }

    void Addon::QueueCondition(Player* player, uint8 slot, bool active)
    {
        Frame f("COND");
        f.Num(slot);
        f.Num(active ? 1 : 0);
        Enqueue(player, "COND:" + std::to_string(static_cast<uint32>(slot)), f.Str());
    }

    // =====================================================================
    // Client -> server
    //
    // Everything here is client-controlled. The core has already refused
    // anything over 255 bytes ($CORE/src/server/game/Handlers/ChatHandler.cpp:290),
    // and nothing below trusts a length, an index or a field count.
    // =====================================================================

    void Addon::RequestedSnapshot(Player* player, Session& session)
    {
        uint64 const now = static_cast<uint64>(GameTime::GetGameTimeMS().count());
        if (session.lastSnapshotMs != 0 && now - session.lastSnapshotMs < SnapshotFloorMs)
            return;

        session.lastSnapshotMs = now;
        SendSnapshot(player);
    }

    void Addon::HandlePick(Player* player, Session& session, std::string_view field)
    {
        // Parsed only far enough to be a safe uint32: Mgr::Pick owns the
        // validation -- an empty table, a dead run, a zero index, an index
        // past the end -- and duplicating it here would let the two disagree.
        // The cap is generous rather than tight for the same reason.
        uint32 index = 0;
        if (!ParseUInt(field, 255, index))
        {
            RequestedSnapshot(player, session);
            return;
        }

        if (sGauntlet->Pick(player, index))
        {
            // A real state change: the run line and the carried set both
            // moved, so this one is never withheld.
            session.lastSnapshotMs = static_cast<uint64>(GameTime::GetGameTimeMS().count());
            SendSnapshot(player);
            return;
        }

        // The pick was refused. The client's idea of the offers had gone
        // stale, so correct it -- under the floor, because a refused pick is
        // something a broken client can repeat without limit.
        RequestedSnapshot(player, session);
    }

    bool Addon::HandleIncoming(Player* player, std::string const& msg)
    {
        // "GNT\t" and at least one character of verb.
        if (msg.size() < 5 || msg.compare(0, 4, "GNT\t") != 0)
            return false;

        // From here the frame is ours, whatever it turns out to say: the
        // caller vetoes the chat line either way, so a malformed or unknown
        // message is swallowed rather than echoed.
        if (!CanSend(player))
            return true;

        std::string_view body(msg);
        body.remove_prefix(4);

        std::string_view const verb = body.substr(0, body.find('\t'));
        std::string_view rest;
        if (verb.size() < body.size())
            rest = body.substr(verb.size() + 1);

        // Only a real frame counts as a handshake, and it is what turns on the
        // Phase 1 queue for this character.
        Session& session = SessionFor(player);
        session.handshaken = true;

        if (verb == "SYNC")
        {
            // HELLO goes out again as well as the snapshot. SYNC is sent on
            // every /reload as well as on login, and it is the addon's only
            // way to ask; answering with the version too is what makes the
            // handshake survive a HELLO that arrived before the addon's frame
            // was listening. It is outside the floor for the same reason: a
            // version answer is one packet and is what the client is missing.
            SendHello(player);
            RequestedSnapshot(player, session);
            return true;
        }

        if (verb == "PICK")
        {
            HandlePick(player, session, rest.substr(0, rest.find('\t')));
            return true;
        }

        // An unknown verb is not an error: a newer addon may send one, and
        // swallowing it is better than echoing it into chat.
        return true;
    }
}
