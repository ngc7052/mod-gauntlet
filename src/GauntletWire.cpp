/*
 * mod-gauntlet - the addon wire's string handling
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "GauntletWire.h"

#include <algorithm>

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
