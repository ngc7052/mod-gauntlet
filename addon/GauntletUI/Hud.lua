-- Hud.lua - the live readout: counters, stats, countdowns and the stalker light.
--
-- Plan section 4 names this file and Phase 0 did not write it. Protocol.lua has
-- carried EVT, CTR, STAT and SUMMON as registered no-ops since then -- each one
-- commented "(Hud.lua)" -- so from Phase 1 onward the server has been sending
-- Frenzy's stacks, Deep Wounds' wound, Champions' fight counter, Ambush's
-- countdown and the stalker indicator, and the addon has thrown every one of
-- them away. Nothing rendered them. This is what renders them.
--
-- Why it has to exist at all, rather than the mechanics printing to chat: no
-- mechanic in this module applies an aura, because there are no client patches
-- and no new spell visuals, so none of them carries a native buff icon. The
-- counters and stats ARE the buff frame. Design section 5's fourth rule --
-- "visible when active; a scalar you cannot see acting is a scalar you cannot
-- learn from" -- is this file's whole justification.
--
-- Shape: one small movable window, one row per live reading, newest-changed
-- last. A row appears when the server first mentions its key and disappears
-- when the value goes to zero and the key is one that means nothing at zero.
-- The whole thing hides itself when there is nothing to say.

local BG     = { 0.06, 0.06, 0.07, 0.88 }
local BORDER = { 0.20, 0.22, 0.28, 1.00 }
local ROW_H  = 18
local WIDTH  = 190

GauntletUIDB = GauntletUIDB or {}
local function DB(k, dflt)
    if GauntletUIDB[k] == nil then GauntletUIDB[k] = dflt end
    return GauntletUIDB[k]
end

-- ============================================================ vocabulary ====
--
-- The wire carries a mechanic *key* ("frenzy", "deep_wounds") and a number.
-- What a player needs is a word and a unit, and the registry's own name is the
-- word -- Data.lua is generated from the same table the server reads, so these
-- cannot drift from what the panel and the offer line call the same affix.
--
-- KEY_BY_MECHANIC maps a registry key back to its id so the name and icon can
-- be looked up; built once from GauntletData rather than written out, for the
-- same reason.
local idByKey = {}
local function BuildIndex()
    if not GauntletData or not GauntletData.mechanics then return end
    for id, m in pairs(GauntletData.mechanics) do
        if m.key then idByKey[m.key] = id end
    end
end

-- Data.lua carries the name and the icon, but not what a reading *means*: a
-- counter with a ceiling, a percentage and a flag all arrive as a number and
-- read completely differently. That is what this table is for, one entry per
-- key the server can send, as { label, unit, hideAtZero }.
--
--   unit "n/m"  a counter with a ceiling: "Champions 6/8"
--   unit "%"    a percentage: "Deep Wounds 18%"
--   unit ""     a bare number: "Nimble 30"
--   unit "flag" present or not: "Grudge" shown only while it is 1
local READINGS = {
    champions      = { "Champions",      "n/m",  true  },
    echo           = { "Echo",           "n/m",  true  },
    carrion        = { "Carrion",        "n/m",  true  },
    frenzy         = { "Frenzy",         "n/m",  true  },
    overextended   = { "Surrounded",     "n/m",  true  },
    reinforcements = { "Reinforcements", "n/m",  true  },
    deep_wounds    = { "Deep Wounds",    "%",    true  },
    hubris         = { "Hubris",         "%",    false },
    nimble         = { "Nimble",         "%",    true  },
    grudge         = { "Grudge",         "flag", true  },
    lone_wolf      = { "Grouped: half health", "flag", true },
    vindication    = { "Vindication",    "%",    true  },
}

local function Reading(key)
    local r = READINGS[key]
    if r then return r[1], r[2], r[3] end
    -- An unknown key is still worth showing rather than dropping: a mechanic
    -- added later should appear the day it starts sending, not the day this
    -- table is updated.
    return key, "", true
end

local function IconFor(key)
    local id = idByKey[key]
    local m  = id and GauntletData and GauntletData.mechanics and GauntletData.mechanics[id]
    return m and m.icon or "Interface\\Icons\\INV_Misc_QuestionMark"
end

-- ================================================================ frame ====
local hud = CreateFrame("Frame", "GauntletHud", UIParent)
hud:SetWidth(WIDTH); hud:SetHeight(40)
hud:SetBackdrop({
    bgFile   = "Interface\\Buttons\\WHITE8X8",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    edgeSize = 14, insets = { left = 4, right = 4, top = 4, bottom = 4 },
})
hud:SetBackdropColor(unpack(BG))
hud:SetBackdropBorderColor(unpack(BORDER))
hud:SetMovable(true); hud:EnableMouse(true); hud:RegisterForDrag("LeftButton")
hud:SetScript("OnDragStart", hud.StartMoving)
hud:SetScript("OnDragStop", function(self)
    self:StopMovingOrSizing()
    local p, _, rp, x, y = self:GetPoint()
    GauntletUIDB.hud = { p, rp, x, y }
end)
hud:SetClampedToScreen(true)
hud:SetFrameStrata("MEDIUM")
hud:Hide()

local saved = DB("hud", nil)
if saved and saved[1] then
    hud:SetPoint(saved[1], UIParent, saved[2], saved[3], saved[4])
else
    hud:SetPoint("CENTER", UIParent, "CENTER", 260, -80)
end

hud.title = hud:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
hud.title:SetPoint("TOPLEFT", hud, "TOPLEFT", 10, -7)
hud.title:SetText("|cffff2020Gauntlet|r")

-- The countdown, which is the one reading that is a bar rather than a number:
-- an event with three seconds left is a thing to move away from, and a number
-- ticking down is much harder to read at a glance than a bar draining.
local bar = CreateFrame("StatusBar", nil, hud)
bar:SetWidth(WIDTH - 20); bar:SetHeight(12)
bar:SetPoint("TOPLEFT", hud, "TOPLEFT", 10, -22)
bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
bar:SetStatusBarColor(0.75, 0.15, 0.15)
bar:SetMinMaxValues(0, 1)
bar:SetValue(0)
bar.bg = bar:CreateTexture(nil, "BACKGROUND")
bar.bg:SetAllPoints(bar)
bar.bg:SetTexture("Interface\\Buttons\\WHITE8X8")
bar.bg:SetVertexColor(0, 0, 0, 0.5)
bar.text = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
bar.text:SetPoint("CENTER", bar, "CENTER", 0, 0)
bar:Hide()

local rows = {}
local function Row(i)
    if rows[i] then return rows[i] end

    local r = CreateFrame("Frame", nil, hud)
    r:SetWidth(WIDTH - 20); r:SetHeight(ROW_H)

    r.icon = r:CreateTexture(nil, "ARTWORK")
    r.icon:SetWidth(14); r.icon:SetHeight(14)
    r.icon:SetPoint("LEFT", r, "LEFT", 0, 0)
    r.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)

    r.label = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    r.label:SetPoint("LEFT", r.icon, "RIGHT", 5, 0)
    r.label:SetJustifyH("LEFT")

    r.value = r:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    r.value:SetPoint("RIGHT", r, "RIGHT", 0, 0)
    r.value:SetJustifyH("RIGHT")

    rows[i] = r
    return r
end

-- ================================================================ state ====
local readings = {}    -- key -> { value, max, order }
local order    = 0

-- Every countdown currently running, keyed by mechanic, and the one the bar is
-- drawing. A single slot was wrong: four timed affixes can easily have
-- countdowns overlapping -- the Shade's thirty seconds is long enough to have
-- Falter's two, Ambush's four and Carrion's four land inside it -- and a new
-- EVT replacing the old one meant the Shade's countdown vanished the moment
-- anything else warned, and never came back even though it was still running.
--
-- The bar draws whichever is *soonest*, because that is the one the player has
-- to act on first, and falls back to the next when it clears.
local events   = {}    -- key -> { label, endsAt, total }
local stalkers = {}    -- key -> true while one is alive

local function Soonest()
    local best, bestKey = nil, nil
    for key, e in pairs(events) do
        if not best or e.endsAt < best.endsAt then best, bestKey = e, key end
    end
    return best, bestKey
end

local function ValueText(key, v, m)
    local _, unit = Reading(key)
    if unit == "n/m" then
        return tostring(v) .. (m and m > 0 and ("/" .. m) or "")
    elseif unit == "%" then
        return tostring(v) .. "%"
    elseif unit == "flag" then
        return "active"
    end
    return tostring(v)
end

local function Layout()
    -- Newest reading last, so a row does not jump around under the cursor as
    -- other values change: the order a key was first seen in is stable for the
    -- session.
    local live = {}
    for key, r in pairs(readings) do
        local _, _, hideAtZero = Reading(key)
        if r.value ~= 0 or not hideAtZero then
            table.insert(live, { key = key, r = r })
        end
    end
    table.sort(live, function(a, b) return a.r.order < b.r.order end)

    local top = -22
    local soon = Soonest()
    if soon then
        bar:Show()
        top = top - 16
    else
        bar:Hide()
    end

    for i, e in ipairs(live) do
        local row = Row(i)
        row:ClearAllPoints()
        row:SetPoint("TOPLEFT", hud, "TOPLEFT", 10, top - (i - 1) * ROW_H)
        row.icon:SetTexture(IconFor(e.key))
        local label = Reading(e.key)
        row.label:SetText(label)
        row.value:SetText(ValueText(e.key, e.r.value, e.r.max))
        row:Show()
    end
    for i = #live + 1, #rows do rows[i]:Hide() end

    local anyStalker = (next(stalkers) ~= nil)

    local n = #live
    if n == 0 and not soon and not anyStalker then
        hud:Hide()
        return
    end

    -- The border turns red while something uninvited is alive and hunting,
    -- which is the SUMMON message's whole content: a light, not a row.
    if anyStalker then
        hud:SetBackdropBorderColor(0.85, 0.15, 0.15, 1)
    else
        hud:SetBackdropBorderColor(unpack(BORDER))
    end

    hud:SetHeight(26 + (soon and 16 or 0) + math.max(n, 1) * ROW_H)
    hud:Show()
end

-- ============================================================== updates ====
hud:SetScript("OnUpdate", function(self, elapsed)
    local soon, key = Soonest()
    if not soon then return end

    local left = soon.endsAt - GetTime()
    if left <= 0 then
        -- A countdown that reaches zero without the server saying so has
        -- either landed or been held; either way the bar has nothing true
        -- left to draw, and the next one takes over.
        events[key] = nil
        Layout()
        return
    end

    bar:SetValue(soon.total > 0 and (left / soon.total) or 0)
    bar.text:SetText(("%s  %.0f"):format(soon.label or "", left + 0.5))
end)

-- =============================================================== wiring ====
GauntletProtocol.On("CTR", function(key, value, max)
    local v, m = tonumber(value) or 0, tonumber(max) or 0
    local r = readings[key]
    if not r then
        order = order + 1
        r = { order = order }
        readings[key] = r
    end
    r.value, r.max = v, m
    Layout()
end)

GauntletProtocol.On("STAT", function(key, value)
    local v = tonumber(value) or 0
    local r = readings[key]
    if not r then
        order = order + 1
        r = { order = order }
        readings[key] = r
    end
    r.value, r.max = v, 0
    Layout()
end)

GauntletProtocol.On("EVT", function(key, secs, label)
    local s = tonumber(secs) or 0
    if s <= 0 then
        -- secs == 0 means "it landed, or it was called off". Either way that
        -- one countdown is over -- and only that one: anything else still
        -- running keeps its place and the bar moves to whichever is next.
        if events[key] then
            events[key] = nil
            Layout()
        end
        return
    end

    events[key] = { label = label or key, endsAt = GetTime() + s, total = s }
    Layout()
end)

GauntletProtocol.On("SUMMON", function(key, alive)
    if alive == "1" or alive == 1 then
        stalkers[key] = true
    else
        stalkers[key] = nil
    end
    Layout()
end)

GauntletProtocol.On("KILLBY", function(id, name)
    -- The session is ending, so this is the one reading that goes to chat: a
    -- frame the player is about to stop looking at is the wrong place for the
    -- single most important sentence the module ever says.
    DEFAULT_CHAT_FRAME:AddMessage(
        ("|cffff2020[Gauntlet]|r The last affix to act on you was |cffffff00%s|r."):format(tostring(name)),
        1, 0.3, 0.3)
end)

local ev = CreateFrame("Frame")
ev:RegisterEvent("PLAYER_ENTERING_WORLD")
ev:SetScript("OnEvent", function()
    BuildIndex()
    -- Nothing is drawn until the server says something. A character carrying
    -- no affix that reports anything never sees this window at all.
    Layout()
end)

SLASH_GAUNTLETHUD1 = "/gauntlethud"
SlashCmdList["GAUNTLETHUD"] = function(cmd)
    cmd = (cmd or ""):lower()
    if cmd == "reset" then
        GauntletUIDB.hud = nil
        hud:ClearAllPoints()
        hud:SetPoint("CENTER", UIParent, "CENTER", 260, -80)
        DEFAULT_CHAT_FRAME:AddMessage("Gauntlet: HUD position reset.", 1, 0.8, 0.2)
        return
    end

    -- A deliberate show, for a player who wants to check it is alive before
    -- anything has been sent. It goes away again on the next Layout().
    hud:Show()
    DEFAULT_CHAT_FRAME:AddMessage(
        "Gauntlet: HUD shown. Drag to move, /gauntlethud reset to recentre. It hides itself "
        .. "when nothing is being reported.", 1, 0.8, 0.2)
end
