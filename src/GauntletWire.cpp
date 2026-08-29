/*
 * mod-gauntlet - the addon wire's string handling
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletWire.h"

#include <algorithm>
#include <string_view>

namespace Gauntlet
{
    namespace
    {
        // A UTF-8 continuation byte is 10xxxxxx. Backing off them puts a hard
        // cut on a character boundary.
        bool IsContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }
    }

    std::vector<std::string> SplitDescription(std::string const& desc, std::size_t chunk)
    {
        std::vector<std::string> out;
        if (desc.empty() || chunk == 0)
            return out;

        for (std::size_t at = 0; at < desc.size(); )
        {
            std::size_t take = std::min<std::size_t>(chunk, desc.size() - at);
            std::size_t skip = 0;

            if (at + take < desc.size())
            {
                // Prefer a word boundary. rfind searches backwards from the
                // cut, so this is the last space that fits.
                std::size_t const space = desc.rfind(' ', at + take);
                if (space != std::string::npos && space > at)
                {
                    take = space - at;   // up to, not including, the space
                    skip = 1;
                }
                else
                {
                    // No space to cut at: one word longer than a chunk. Cut
                    // hard, but not inside a character -- back off any
                    // continuation bytes first.
                    while (take > 1 && IsContinuation(static_cast<unsigned char>(desc[at + take])))
                        --take;
                }
            }

            // Runs of spaces would otherwise produce an empty chunk, and an
            // empty chunk rejoins as a doubled space.
            if (take == 0)
            {
                ++at;
                continue;
            }

            out.push_back(desc.substr(at, take));
            at += take + skip;
        }

        return out;
    }

    std::size_t Utf8Floor(std::string_view s, std::size_t n)
    {
        if (n >= s.size())
            return s.size();

        while (n > 0 && IsContinuation(static_cast<unsigned char>(s[n])))
            --n;
        return n;
    }

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

    std::string JoinDescription(std::vector<std::string> const& parts)
    {
        std::string out;
        for (std::string const& part : parts)
        {
            if (!out.empty())
                out += ' ';
            out += part;
        }
        return out;
    }
}
