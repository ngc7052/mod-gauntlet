-- Panel.lua - GauntletUI's main window, chooser, settings panel and minimap
-- button. Formerly GauntletUI.lua; split so Protocol.lua can own the wire
-- format while this file only ever reads from it.
--
-- Design notes:
--   * dark flat panels rather than the ornate dialog frame, so the window sits
--     quietly over the game instead of dominating it
--   * the affix list is one compact row per affix; the full description lives
--     in the tooltip, keeping a sixteen-affix run readable
--   * hover feedback is an additive glow plus a border lift, the same idiom
--     Blizzard uses for action buttons
--   * settings live in their own panel, so they can never collide with the list

local offers, affixes = {}, {}
local carriedBySlot = {}   -- slot -> resolved affix record; for OFFER's swap-name lookup
local carriedByMech = {}   -- mechanic id -> the same record; for "II to III" on a rank-up
local mode, pendingOffer = nil, false
local run = { seed = "?", tier = 0, state = "alive" }

GauntletUIDB = GauntletUIDB or {}
local defaults = { minimapAngle = 210, showMinimap = true, autoOpen = true, suppressChat = true }
local function DB(k)
    if GauntletUIDB[k] == nil then GauntletUIDB[k] = defaults[k] end
    return GauntletUIDB[k]
end

local function Strip(s)
    if not s then return "" end
    return (s:gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", ""))
end

-- The palette, and every panel in the addon takes it from here.
--
-- The backdrop alpha is the whole of "transparent": 0.94 was very nearly opaque
-- and the panels read as a second UI sitting on top of the game rather than as
-- part of it. 0.62 lets the world through without letting the text fight it --
-- the title and the row names are drawn at full alpha regardless, because a
-- readable panel over a bright zone is worth more than a uniformly faint one.
local BG        = { 0.04, 0.04, 0.06, 0.62 }
local BORDER    = { 0.30, 0.32, 0.40, 0.85 }
local ROW_BG    = { 0.10, 0.10, 0.13, 0.35 }
local ROW_ALT   = { 0.14, 0.14, 0.18, 0.35 }   -- every other row, for tracking across a wide line
local HEAD_BG   = { 0.08, 0.09, 0.13, 0.55 }

-- ===================================================== fallback-only ====
-- Chat-scraped affixes have no mechanic id, so severity and icon can only be
-- guessed from the description text. Kept for when the protocol is not
-- active (see Protocol.lua) - this is the fallback, not dead code.
local SEVERITY = {
    trivial  = { 0.62, 0.62, 0.62, "Trivial"  },
    minor    = { 0.35, 0.80, 0.35, "Minor"    },
    moderate = { 0.95, 0.85, 0.25, "Moderate" },
    major    = { 1.00, 0.55, 0.10, "Major"    },
    severe   = { 1.00, 0.25, 0.20, "Severe"   },
    dire     = { 0.80, 0.20, 0.95, "Dire"     },
}
local function Sev(desc)
    return SEVERITY[((desc or ""):match("%[(%a+)%]") or ""):lower()]
        or { 0.7, 0.7, 0.7, "" }
end

local function IconFor(desc)
    desc = (desc or ""):lower()
    if desc:find("take") and desc:find("more damage") then
        return "Interface\\Icons\\Ability_Warrior_ShieldBash"
    elseif desc:find("less damage") then
        return "Interface\\Icons\\Ability_Warrior_Disarm"
    elseif desc:find("healing") then
        return "Interface\\Icons\\Spell_Shadow_LifeDrain"
    elseif desc:find("experience") then
        return "Interface\\Icons\\INV_Misc_Book_09"
    end
    return "Interface\\Icons\\INV_Misc_QuestionMark"
end

-- Description minus the "[Severity] " prefix, which the colour already conveys.
local function Body(desc)
    return (desc or ""):gsub("^%[%a+%]%s*", "")
end

-- ===================================================== protocol-only ====
-- Names/descriptions/icons for protocol-sourced affixes come from Data.lua,
-- keyed by mechanic id - never from parsing text.
local function DataUsable()
    return GauntletData ~= nil and GauntletData.version == GauntletProtocol.PROTOCOL_VERSION
end

local function MechInfo(id)
    id = tonumber(id)
    if DataUsable() then
        local m = GauntletData.mechanics and GauntletData.mechanics[id]
        if m then return m end
    end
    -- Data.lua missing, stale (version mismatch treated the same as missing,
    -- since neither can be trusted) or the id is not in it.
    return { name = "#" .. tostring(id), family = nil,
              icon = "Interface\\Icons\\INV_Misc_QuestionMark", desc = "" }
end

-- TODO(design): family colours - not specified anywhere; picked to keep a
-- similar spread of hue/intensity to the old [Severity] palette they replace.
local FAMILY_COLOR = {
    [0] = { 0.70, 0.55, 0.95, "Spawn"     },
    [1] = { 1.00, 0.55, 0.10, "Enemy"     },
    [2] = { 0.95, 0.85, 0.25, "Tempo"     },
    [3] = { 1.00, 0.25, 0.20, "Attrition" },
    [4] = { 0.40, 0.70, 1.00, "Rules"     },
    [5] = { 0.35, 0.80, 0.35, "Bargain"   },
    [6] = { 0.90, 0.40, 0.75, "Class"     },
}
local function FamilyColor(family)
    return FAMILY_COLOR[family] or { 0.7, 0.7, 0.7, "" }
end

-- TODO(design): kind-badge colours and labels - likewise not specified.
local KIND_COLOR = {
    new     = { 0.35, 0.80, 0.35, "NEW"     },
    rankup  = { 0.40, 0.70, 1.00, "RANK UP" },
    swap    = { 1.00, 0.55, 0.10, "SWAP"    },
    bargain = { 0.90, 0.40, 0.75, "BARGAIN" },
}

-- Gauntlet::Boon (src/Gauntlet.h), in enum order. The panel shows the upside
-- on the row itself rather than only inside a tooltip: an affix is a trade, and
-- a list that shows only the cost is a list of bad news.
--
-- The five past BonusRegen have no magnitude of their own -- the mechanic's own
-- sentence says what they do -- so they are named without a number.
local BOON_SHORT = {
    [0] = nil,          -- None
    [1] = "%d%% dmg",
    [2] = "%d%% heal",
    [3] = "%d%% speed",
    [4] = "%d%% xp",
    [5] = "%d%% gold",
    [6] = "%d%% hp",
    [7] = "%d%% regen",
    [8] = "avoidance",
    [9] = "cooldown",
    [10] = "ability",
    [11] = "pet dmg",
    [12] = "second life",
}

local function BoonText(boon, mag)
    local f = BOON_SHORT[boon or 0]
    if not f then return nil end
    if f:find("%%d") then
        if not mag or mag == 0 then return nil end
        return f:format(mag)
    end
    return f
end

local RANK_PIP = { [1] = "I", [2] = "II", [3] = "III", [4] = "IV" }
local function RankText(rank) return RANK_PIP[rank] or "" end

-- Colour/icon for either a fallback record { name, desc } or a protocol
-- record { name, desc, icon, family, ... } - the two shapes intentionally
-- share the "name"/"desc" fields so the render code below barely forks.
local function RowColor(a) return a.family ~= nil and FamilyColor(a.family) or Sev(a.desc) end
local function RowIcon(a) return a.icon or IconFor(a.desc) end

-- ============================================================== helpers ====
local function Panel(name, w, h, title)
    local f = CreateFrame("Frame", name, UIParent)
    f:SetWidth(w); f:SetHeight(h)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 60)
    f:SetBackdrop({
        bgFile   = "Interface\\Buttons\\WHITE8X8",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        edgeSize = 14, insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    f:SetBackdropColor(unpack(BG))
    f:SetBackdropBorderColor(unpack(BORDER))

    -- A one-pixel rule under the title. Cheap, and it is what stops a
    -- translucent panel reading as a floating pile of text: the eye needs one
    -- horizontal to hang the header off when the background is not solid.
    f.rule = f:CreateTexture(nil, "ARTWORK")
    f.rule:SetTexture("Interface\\Buttons\\WHITE8X8")
    f.rule:SetVertexColor(unpack(BORDER))
    f.rule:SetHeight(1)
    f.rule:SetPoint("TOPLEFT", f, "TOPLEFT", 10, -28)
    f.rule:SetPoint("TOPRIGHT", f, "TOPRIGHT", -10, -28)

    f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", f.StartMoving)
    f:SetScript("OnDragStop", f.StopMovingOrSizing)
    f:SetClampedToScreen(true)
    f:SetFrameStrata("DIALOG")
    f:Hide()

    f.title = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    f.title:SetPoint("TOPLEFT", f, "TOPLEFT", 14, -12)
    f.title:SetText(title)

    local x = CreateFrame("Button", nil, f, "UIPanelCloseButton")
    x:SetWidth(26); x:SetHeight(26)
    x:SetPoint("TOPRIGHT", f, "TOPRIGHT", -4, -4)

    tinsert(UISpecialFrames, name)   -- Escape closes it
    return f
end

-- Additive glow, the standard Blizzard hover idiom.
local function AddGlow(btn)
    local g = btn:CreateTexture(nil, "OVERLAY")
    g:SetTexture("Interface\\Buttons\\ButtonHilight-Square")
    g:SetBlendMode("ADD")
    g:SetAllPoints(btn)
    g:SetAlpha(0.55)
    g:Hide()
    btn.glow = g
    return g
end

-- ================================================================= main ====
--
-- The carried list, rewritten in Phase 10 for four things it could not do:
-- show more than sixteen affixes, show what the whole set adds up to, say what
-- each affix pays as well as what it costs, and let the world through behind
-- it.
--
-- The sixteen was not a display choice, it was MAX_CARRIED leaking into the
-- renderer: sixteen frames were built at load and a seventeenth affix would
-- simply not have been drawn. `.gauntlet debug give-class 3 all` attaches more
-- than sixteen on purpose, so the cap had to go from here whatever the carry
-- cap does.

local main = Panel("GauntletMain", 452, 320, "The Gauntlet")

main.head = main:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
main.head:SetPoint("TOPLEFT", main, "TOPLEFT", 14, -36)
main.head:SetJustifyH("LEFT")

local gear = CreateFrame("Button", nil, main)
gear:SetWidth(20); gear:SetHeight(20)
gear:SetPoint("TOPRIGHT", main, "TOPRIGHT", -32, -8)
gear:SetNormalTexture("Interface\\Buttons\\UI-OptionsButton")
AddGlow(gear)
gear:SetScript("OnEnter", function(self)
    self.glow:Show()
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:SetText("Settings")
    GameTooltip:Show()
end)
gear:SetScript("OnLeave", function(self) self.glow:Hide(); GameTooltip:Hide() end)

-- ---------------------------------------------------------------- totals ----
--
-- What the whole set adds up to. Every number is the server's, from the TOTALS
-- frame: six of the seven are aggregate products *after* the caps have clamped
-- them, so a run whose damage taken would be x2.4 shows the x2.0 it actually
-- takes. Adding the boons up in Lua would print the first number while the
-- player took the second.
local totals = nil

-- label, key, and which direction is bad news. `worse` decides the colour, not
-- the arithmetic: 120% damage taken is bad and 120% experience is good, and a
-- panel that paints both the same colour is not summarising anything.
-- Short labels. "Damage taken" and "Enemy speed" wrapped to two lines inside a
-- 58-pixel cell and overflowed the strip; the full names live in the tooltip.
local TOTAL_ROWS = {
    { "Taken",   "taken", true  },
    { "Dealt",   "done",  false },
    { "Healing", "heal",  false },
    { "Max HP",  "maxhp", false },
    { "Speed",   "speed", true  },
    { "XP",      "xp",    false },
    { "Gold",    "gold",  false },
}

local TOTAL_LONG = {
    taken = "Damage taken", done = "Damage dealt", heal  = "Healing received",
    maxhp = "Maximum health", speed = "Enemy move speed", xp = "Experience",
    gold  = "Gold from loot",
}

local totalsBox = CreateFrame("Frame", nil, main)
totalsBox:SetPoint("TOPLEFT", main, "TOPLEFT", 12, -54)
totalsBox:SetPoint("TOPRIGHT", main, "TOPRIGHT", -12, -54)
totalsBox:SetHeight(34)
totalsBox:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8" })
totalsBox:SetBackdropColor(unpack(HEAD_BG))

totalsBox.cells = {}
for i = 1, #TOTAL_ROWS do
    local c = totalsBox:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    c:SetJustifyH("CENTER")
    c:SetWidth(60)
    c:SetPoint("TOPLEFT", totalsBox, "TOPLEFT", 3 + (i - 1) * 61, -4)

    local v = totalsBox:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    v:SetJustifyH("CENTER")
    v:SetWidth(60)
    v:SetPoint("TOPLEFT", c, "BOTTOMLEFT", 0, -2)

    c:SetText(TOTAL_ROWS[i][1])
    -- The label is hidden with its value when there is nothing to summarise, so
    -- the "nothing yet" line does not print on top of a row of headings.
    totalsBox.cells[i] = { value = v, label = c }
end

totalsBox:EnableMouse(true)
totalsBox:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_BOTTOM")
    GameTooltip:SetText("What your affixes add up to", 1, 0.82, 0)
    GameTooltip:AddLine("Every number is the server's, after the caps have clamped it -- "
                        .. "so this is what you actually get, not what the cards add to.",
                        0.8, 0.8, 0.8, true)
    GameTooltip:AddLine(" ")
    for _, spec in ipairs(TOTAL_ROWS) do
        local v = totals and totals[spec[2]] or 1
        local shown = ("%d%%"):format(math.floor(v * 100 + 0.5))
        if math.abs(v - 1) < 0.005 then shown = "unchanged" end
        GameTooltip:AddDoubleLine(TOTAL_LONG[spec[2]], shown, 0.8, 0.8, 0.8, 1, 1, 1)
    end
    GameTooltip:AddLine(" ")
    GameTooltip:AddLine("Some boons only pay under a condition -- Lone Wolf's experience "
                        .. "needs you to be alone -- and are counted here as the card "
                        .. "promises them.", 0.6, 0.6, 0.6, true)
    GameTooltip:Show()
end)
totalsBox:SetScript("OnLeave", function() GameTooltip:Hide() end)

totalsBox.empty = totalsBox:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
totalsBox.empty:SetPoint("LEFT", totalsBox, "LEFT", 8, 0)
totalsBox.empty:SetText("Nothing you carry changes a number yet.")

local function RefreshTotals()
    local any = false
    if totals then
        for i, spec in ipairs(TOTAL_ROWS) do
            local v = totals[spec[2]] or 1
            if math.abs(v - 1) < 0.005 then
                -- Unchanged. Drawn as a dash rather than "100%": seven
                -- identical hundreds is a wall of noise with the one number
                -- that moved hidden in it.
                totalsBox.cells[i].value:SetText("|cff505050-|r")
            else
                any = true
                local worse = spec[3] and v > 1 or (not spec[3]) and v < 1
                local colour = worse and "|cffff6040" or "|cff40e060"
                totalsBox.cells[i].value:SetText(("%s%d%%|r"):format(colour, math.floor(v * 100 + 0.5)))
            end
        end
    end

    -- Labels and values go together. Showing the "nothing yet" line over a row
    -- of headings is what the first version did, and it read as a rendering
    -- fault rather than as a message.
    for _, cell in ipairs(totalsBox.cells) do
        if any then cell.value:Show(); cell.label:Show()
        else        cell.value:Hide(); cell.label:Hide() end
    end
    if any then totalsBox.empty:Hide() else totalsBox.empty:Show() end
end

-- ------------------------------------------------------------------ list ----

-- What the run's timed affixes do to each other's timing, from the PACE frame.
-- The stretch belongs to the whole carried set, not to any one affix, so no
-- affix can state it and this is where it is held.
--
-- Declared here, above the row loop, and not beside RefreshMain where it is
-- also read: the row tooltips are closures built inside that loop, and a local
-- declared after them would not be their upvalue -- they would capture a global
-- of the same name and read nil for the life of the session.
local pace = nil

-- One line per affix, and the description only on hover.
--
-- The first version put the effect text on a second line in the row. It read
-- badly and it was wrong twice over: a sentence written for a tooltip does not
-- truncate into half a row -- "While you are in a group your maximum health is
-- halv..." -- and halving the rows on screen to show text nobody asked for is a
-- poor trade when the panel's job is to show a set at a glance.
local ROW_H     = 24
local VISIBLE   = 12         -- rows built; the list scrolls through however many there are
local scrollAt  = 0          -- index of the first row drawn

local list = CreateFrame("Frame", nil, main)
list:SetPoint("TOPLEFT", main, "TOPLEFT", 12, -92)
list:SetPoint("BOTTOMRIGHT", main, "BOTTOMRIGHT", -12, 28)
list:EnableMouseWheel(true)

local rows = {}
for i = 1, VISIBLE do
    local r = CreateFrame("Button", nil, list)
    r:SetHeight(ROW_H)
    r:SetPoint("TOPLEFT", list, "TOPLEFT", 0, -(i - 1) * ROW_H)
    r:SetPoint("TOPRIGHT", list, "TOPRIGHT", 0, -(i - 1) * ROW_H)
    r:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8" })

    r.icon = r:CreateTexture(nil, "ARTWORK")
    r.icon:SetWidth(22); r.icon:SetHeight(22)
    r.icon:SetPoint("LEFT", r, "LEFT", 5, 0)
    r.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)

    r.name = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    r.name:SetPoint("LEFT", r.icon, "RIGHT", 8, 0)
    r.name:SetJustifyH("LEFT")

    -- Family on the far right, what it pays just inside it, so the line reads
    -- "what it is | what it costs you | what it pays".
    r.tag = r:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    r.tag:SetPoint("RIGHT", r, "RIGHT", -8, 0)
    r.tag:SetJustifyH("RIGHT")
    r.tag:SetWidth(62)

    r.pays = r:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    r.pays:SetPoint("RIGHT", r.tag, "LEFT", -8, 0)
    r.pays:SetJustifyH("RIGHT")
    r.pays:SetWidth(96)

    r.name:SetPoint("RIGHT", r.pays, "LEFT", -6, 0)

    AddGlow(r)
    r:SetScript("OnEnter", function(self)
        self.glow:Show()
        local d = self.data
        if not d then return end

        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(d.name .. (d.rank and ("  " .. (RANK_PIP[d.rank] or "")) or ""), 1, 0.82, 0)

        local s = RowColor(d)
        GameTooltip:AddLine(s[4] .. " family", s[1], s[2], s[3])
        GameTooltip:AddLine(" ")
        GameTooltip:AddLine("What it does", 1, 1, 1)
        GameTooltip:AddLine(Body(d.desc), 0.85, 0.85, 0.85, true)

        local boon = BoonText(d.boon, d.boonMag)
        if boon then
            GameTooltip:AddLine(" ")
            GameTooltip:AddLine("What it pays", 1, 1, 1)
            GameTooltip:AddLine(boon, 0.4, 0.9, 0.4, true)
        end

        if pace and pace.timed > 1 and pace.mult > 1.0 then
            GameTooltip:AddLine(" ")
            GameTooltip:AddLine(("You carry %d affixes that act on a timer, so this one waits x%.2f longer than the line above says."):
                                format(pace.timed, pace.mult), 0.6, 0.6, 0.6, true)
        end
        GameTooltip:Show()
    end)
    r:SetScript("OnLeave", function(self) self.glow:Hide(); GameTooltip:Hide() end)
    r:Hide()
    rows[i] = r
end

main.empty = main:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
main.empty:SetPoint("TOPLEFT", list, "TOPLEFT", 4, -6)
main.empty:SetText("No affixes yet. The first arrives at level 1.")
main.empty:Hide()

main.foot = main:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
main.foot:SetPoint("BOTTOMLEFT", main, "BOTTOMLEFT", 14, 10)

main.count = main:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
main.count:SetPoint("BOTTOMRIGHT", main, "BOTTOMRIGHT", -14, 10)
main.count:SetJustifyH("RIGHT")

local function RefreshMain()
    local live = (run.state == "alive")
    main.head:SetText(("Tier |cffffd100%d|r   %s   |cff808080seed %s|r"):format(
        run.tier, live and "|cff40e040alive|r" or "|cffff4040retired|r", run.seed))

    RefreshTotals()

    -- Grouped by family, then by name. The arrival order is the tier each was
    -- taken at, which meant a run's list shuffled its own families together and
    -- gave no sense of what the set actually was.
    table.sort(affixes, function(x, y)
        if (x.family or 99) ~= (y.family or 99) then return (x.family or 99) < (y.family or 99) end
        return (x.name or "") < (y.name or "")
    end)

    local n = #affixes

    -- Clamp the scroll to what there is. Losing an affix to a swap while
    -- scrolled to the bottom would otherwise leave the list looking empty.
    local maxAt = n - VISIBLE
    if maxAt < 0 then maxAt = 0 end
    if scrollAt > maxAt then scrollAt = maxAt end
    if scrollAt < 0 then scrollAt = 0 end

    for i = 1, VISIBLE do
        local a = affixes[scrollAt + i]
        local r = rows[i]
        if a then
            local s = RowColor(a)
            r.data = a
            r:SetBackdropColor(unpack(((scrollAt + i) % 2 == 0) and ROW_ALT or ROW_BG))
            r.icon:SetTexture(RowIcon(a))
            r.name:SetText(("%s|cff707070%s|r"):format(
                a.name, a.rank and ("  " .. (RANK_PIP[a.rank] or "")) or ""))
            r.name:SetTextColor(s[1], s[2], s[3])
            r.tag:SetText(("|cff606060%s|r"):format(s[4]))

            local boon = BoonText(a.boon, a.boonMag)
            r.pays:SetText(boon and ("|cff40e060+" .. boon .. "|r") or "")
            r:Show()
        else
            r.data = nil
            r:Hide()
        end
    end

    if n == 0 then main.empty:Show() else main.empty:Hide() end

    if n > VISIBLE then
        main.count:SetText(("|cff808080%d-%d of %d   (scroll)|r"):format(
            scrollAt + 1, math.min(scrollAt + VISIBLE, n), n))
    elseif n > 0 then
        main.count:SetText(("|cff808080%d carried|r"):format(n))
    else
        main.count:SetText("")
    end

    if pendingOffer then
        main.foot:SetText("|cffffd100A choice is waiting - leave combat.|r")
    elseif pace and pace.timed > 1 and pace.mult > 1.0 then
        main.foot:SetText(("|cff808080%d affixes act on a timer, so each waits |r|cffffd100x%.2f|r|cff808080 longer|r")
                          :format(pace.timed, pace.mult))
    else
        main.foot:SetText("/gauntlet top for the furthest runs")
    end

    main:SetHeight(122 + math.max(math.min(n, VISIBLE), 1) * ROW_H)
end

list:SetScript("OnMouseWheel", function(_, delta)
    scrollAt = scrollAt - delta
    RefreshMain()
end)

-- ============================================================= settings ====
local cfg = Panel("GauntletSettings", 250, 130, "Settings")
local function Check(parent, label, key, y, onToggle)
    local c = CreateFrame("CheckButton", "GauntletCfg" .. key, parent, "UICheckButtonTemplate")
    c:SetWidth(22); c:SetHeight(22)
    c:SetPoint("TOPLEFT", parent, "TOPLEFT", 14, y)
    getglobal(c:GetName() .. "Text"):SetText(label)
    c:SetScript("OnClick", function(self)
        GauntletUIDB[key] = self:GetChecked() and true or false
        PlaySound("igMainMenuOptionCheckBoxOn")
        if onToggle then onToggle(GauntletUIDB[key]) end
    end)
    return c
end

local minimapBtn
local cbMinimap = Check(cfg, "Show minimap button", "showMinimap", -34, function(v)
    if minimapBtn then if v then minimapBtn:Show() else minimapBtn:Hide() end end
end)
local cbAuto  = Check(cfg, "Open chooser automatically", "autoOpen", -60)
local cbQuiet = Check(cfg, "Hide Gauntlet chat text", "suppressChat", -86)

gear:SetScript("OnClick", function()
    if cfg:IsShown() then cfg:Hide() return end
    cbMinimap:SetChecked(DB("showMinimap"))
    cbAuto:SetChecked(DB("autoOpen"))
    cbQuiet:SetChecked(DB("suppressChat"))
    cfg:Show()
end)

-- ============================================================== chooser ====
local chooser = Panel("GauntletChooser", 430, 240, "Choose Your Affix")
chooser.sub = chooser:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
chooser.sub:SetPoint("TOPLEFT", chooser, "TOPLEFT", 14, -30)
chooser.sub:SetText("Permanent, and it stacks with what you already carry.")

local cards = {}
for i = 1, 3 do
    local b = CreateFrame("Button", nil, chooser)
    b:SetWidth(402); b:SetHeight(48)
    b:SetPoint("TOPLEFT", chooser, "TOPLEFT", 14, -50 - (i - 1) * 54)
    b:SetBackdrop({
        bgFile = "Interface\\Buttons\\WHITE8X8",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    b:SetBackdropColor(0.09, 0.09, 0.11, 0.9)

    b.icon = b:CreateTexture(nil, "ARTWORK")
    b.icon:SetWidth(34); b.icon:SetHeight(34)
    b.icon:SetPoint("LEFT", b, "LEFT", 7, 0)
    b.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)

    b.name = b:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    b.name:SetPoint("TOPLEFT", b.icon, "TOPRIGHT", 9, -4)
    b.name:SetJustifyH("LEFT")

    b.desc = b:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    b.desc:SetPoint("TOPLEFT", b.name, "BOTTOMLEFT", 0, -3)
    b.desc:SetWidth(330); b.desc:SetJustifyH("LEFT")

    -- Kind badge (new/rankup/swap/bargain); only shown for protocol-sourced offers.
    b.badge = b:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    b.badge:SetPoint("TOPRIGHT", b, "TOPRIGHT", -8, -6)
    b.badge:Hide()

    AddGlow(b)
    b.index = i
    b:SetScript("OnEnter", function(self)
        self.glow:Show()
        local s = self.sev or { 1, 0.82, 0 }
        self:SetBackdropBorderColor(s[1], s[2], s[3], 1)
    end)
    b:SetScript("OnLeave", function(self)
        self.glow:Hide()
        self:SetBackdropBorderColor(unpack(BORDER))
    end)
    b:SetScript("OnClick", function(self)
        PlaySound("igMainMenuOptionCheckBoxOn")
        if GauntletProtocol.mode == "protocol" then
            GauntletProtocol.SendPick(self.index)
        else
            SendChatMessage(".gauntlet pick " .. self.index, "SAY")
        end
        chooser:Hide(); offers = {}; pendingOffer = false
    end)
    b:Hide()
    cards[i] = b
end

local function ShowChooser()
    if InCombatLockdown() then pendingOffer = true; RefreshMain() return end
    local n = 0
    for i = 1, 3 do
        local o = offers[i]
        if o then
            local s = RowColor(o)
            cards[i].sev = s
            cards[i].icon:SetTexture(RowIcon(o))
            cards[i].name:SetText(o.rank and o.rank > 1
                                  and (o.name .. "  |cff808080" .. (RANK_PIP[o.rank] or "") .. "|r")
                                  or o.name)
            cards[i].name:SetTextColor(s[1], s[2], s[3])
            local descText = Body(o.desc)
            if o.kind == "swap" and o.swapName then
                descText = descText .. "  |cffff8040(swaps out " .. o.swapName .. ")|r"
            end

            -- What you already carry of this one, so a RANK UP says what it is
            -- raising rather than only that it raises something. The
            -- description above is already written at the offered rank, so
            -- between them the card answers "from what, to what".
            local held = carriedByMech[o.id]
            if o.kind == "rankup" and held and held.rank and o.rank then
                descText = descText .. "  |cff66b0ff(" .. (RANK_PIP[held.rank] or held.rank)
                         .. " to " .. (RANK_PIP[o.rank] or o.rank) .. ")|r"
            end

            -- What it pays, on its own line and in its own colour.
            --
            -- The sentence above already ends with the boon when the mechanic
            -- writes one into its own Describe, but most read as "... In
            -- exchange, you gain 45% more experience." buried at the end of
            -- four lines of curse. A player choosing between three offers is
            -- comparing exactly two things, and the second one should not have
            -- to be found.
            local boon = BoonText(o.boon, o.boonMag)
            if boon then
                descText = descText .. "\n|cff40e060Pays: +" .. boon .. "|r"
            end

            cards[i].desc:SetText(descText)
            -- tonumber and not `or 0`: GetStringHeight is documented to return
            -- a number, but a FontString that has not been laid out yet can
            -- answer with something that is not one, and `or 0` only catches
            -- nil. The arithmetic below is what breaks, several lines away
            -- from the cause.
            cards[i].descH = tonumber(cards[i].desc:GetStringHeight()) or 0
            if o.kind then
                local k = KIND_COLOR[o.kind] or { 0.7, 0.7, 0.7, o.kind:upper() }
                cards[i].badge:SetText(k[4])
                cards[i].badge:SetTextColor(k[1], k[2], k[3])
                cards[i].badge:Show()
            else
                cards[i].badge:Hide()
            end
            cards[i]:SetBackdropBorderColor(unpack(BORDER))
            cards[i]:Show(); n = n + 1
        else
            cards[i]:Hide()
        end
    end
    if n == 0 then return end

    -- Cards are sized to their text rather than to a fixed 48 pixels.
    --
    -- They were fixed-height while every description was the registry's
    -- one-line blurb. The server now sends the mechanic's real sentence at the
    -- offered rank, which runs to four wrapped lines for the longer ones, and
    -- a fixed card clipped it and overlapped the row beneath.
    --
    -- NAME_H is the name line plus its padding above and below the block; the
    -- 48 floor keeps a one-line offer looking the way it always has.
    local NAME_H, GAP = 24, 6
    local top = 50
    for i = 1, 3 do
        local c = cards[i]
        if c:IsShown() then
            local h = math.max(48, NAME_H + (tonumber(c.descH) or 0) + 10)
            c:SetHeight(h)
            c:ClearAllPoints()
            c:SetPoint("TOPLEFT", chooser, "TOPLEFT", 14, -top)
            top = top + h + GAP
        end
    end

    chooser.title:SetText("Tier " .. run.tier .. " - Choose Your Affix")
    chooser:SetHeight(top + 12)
    chooser:Show()
    PlaySound("igQuestListOpen")
    pendingOffer = false
end

-- ========================================================= chat fallback ====
local function OnSystem(raw)
    -- The protocol is authoritative once active; ignore chat-scraped state so
    -- the two sources cannot race or clobber each other. Chat suppression
    -- below still applies regardless of mode.
    if GauntletProtocol.mode == "protocol" then return end

    local msg = Strip(raw)

    local tier = msg:match("Tier (%d+) reached")
    if tier then offers, mode, run.tier = {}, "offer", tonumber(tier) return end

    local seed, t, state = msg:match("seed (%d+)%s*|%s*tier (%d+)%s*|%s*(%a+)")
    if seed then
        run.seed, run.tier, run.state = seed, tonumber(t), state:lower()
        affixes, mode = {}, "status"
        if main:IsShown() then RefreshMain() end
        return
    end

    local rseed, rtier = msg:match("Run seed (%d+) %- tier (%d+)")
    if rseed then run.seed, run.tier = rseed, tonumber(rtier) return end

    local idx, name, desc = msg:match("^%s*(%d+)%.%s+(.-)%s+%-%s+(.+)$")
    if idx then
        idx = tonumber(idx)
        if mode == "offer" and idx <= 3 then
            offers[idx] = { name = name, desc = desc }
            if DB("autoOpen") then ShowChooser() else pendingOffer = true; RefreshMain() end
        elseif mode == "status" then
            affixes[idx] = { name = name, desc = desc }
            if main:IsShown() then RefreshMain() end
        end
        return
    end

    if msg:find("You bear") then
        chooser:Hide(); offers, pendingOffer = {}, false

        -- The scraped carried list is only ever rebuilt from a .gauntlet status,
        -- so without asking for a fresh one the affix just picked does not show
        -- up until the panel is opened again. Only worth the round trip while
        -- the window is actually up.
        if main:IsShown() then
            affixes, mode = {}, "status"
            SendChatMessage(".gauntlet status", "SAY")
        else
            mode = nil
        end
        RefreshMain()
    end
end

local function IsGauntletLine(msg)
    if not msg then return false end
    local s = Strip(msg)
    return s:find("%[Gauntlet%]") ~= nil or (mode ~= nil and s:match("^%s*%d+%.%s") ~= nil)
end

local origHandler = ChatFrame_MessageEventHandler
ChatFrame_MessageEventHandler = function(self, event, ...)
    if event == "CHAT_MSG_SYSTEM" then
        local msg = select(1, ...)
        if IsGauntletLine(msg) then
            OnSystem(msg)
            if DB("suppressChat") then return end
        end
    end
    return origHandler(self, event, ...)
end

-- ============================================================== protocol ====
-- Everything below reads GNT messages via GauntletProtocol.On and turns them
-- into the same { name, desc, icon, family, ... } shape the renderers above
-- already understand.

GauntletProtocol.On("RUN", function(seed, tier, state, class)
    run.seed = seed
    run.tier = tonumber(tier) or run.tier
    run.state = (state or run.state):lower()
    run.class = class
    if main:IsShown() then RefreshMain() end
end)

local pendingCarried, pendingCarriedBySlot, pendingCarriedByMech = {}, {}, {}
GauntletProtocol.On("AFFIX", function(slot, id, rank, cond, boon, boonMag)
    local info = MechInfo(id)
    local rec = {
        name = info.name, desc = info.desc, icon = info.icon, family = info.family,
        rank = tonumber(rank), cond = tonumber(cond), boon = tonumber(boon),
        boonMag = tonumber(boonMag), slot = tonumber(slot), id = tonumber(id),
    }
    tinsert(pendingCarried, rec)
    pendingCarriedBySlot[rec.slot] = rec
    pendingCarriedByMech[rec.id]   = rec
end)

-- The same for a carried affix, keyed by slot. Reinforcements III really draws
-- an enemy after twenty seconds and then every ten; the registry blurb said
-- "longer than 30 seconds ... every 15 seconds", which is the rank I wording,
-- and that is what the tooltip showed a player carrying rank III.
GauntletProtocol.On("ADESC", function(slot, text)
    local a = pendingCarriedBySlot[tonumber(slot)]
    if not a then return end

    if a.descFromServer then
        a.desc = a.desc .. " " .. (text or "")
    else
        a.desc = text or ""
        a.descFromServer = true
    end
end)

GauntletProtocol.On("AFFIX_END", function()
    -- AFFIX_END marks a complete, authoritative snapshot; replace rather than
    -- merge, since the server may resend this after login, a pick or a swap.
    affixes, carriedBySlot, carriedByMech = pendingCarried, pendingCarriedBySlot, pendingCarriedByMech
    pendingCarried, pendingCarriedBySlot, pendingCarriedByMech = {}, {}, {}
    -- Unconditional: RefreshMain only writes text and textures, so running it
    -- while the window is hidden costs nothing, and gating it on IsShown() was
    -- how a freshly picked affix failed to appear until the panel was reopened.
    RefreshMain()
end)

local pendingOffers = {}
GauntletProtocol.On("OFFER", function(i, id, rank, cond, boon, boonMag, kind, swapSlot)
    local info = MechInfo(id)
    local swapName
    if kind == "swap" then
        local carried = carriedBySlot[tonumber(swapSlot)]
        swapName = carried and carried.name or ("#" .. tostring(swapSlot))
    end
    pendingOffers[tonumber(i)] = {
        name = info.name, desc = info.desc, icon = info.icon, family = info.family,
        rank = tonumber(rank), cond = tonumber(cond), boon = tonumber(boon),
        boonMag = tonumber(boonMag), kind = kind, swapName = swapName,
    }
end)

-- The mechanic's own sentence at the rank being offered, which the server
-- sends as one or more ODESC frames right after the OFFER it belongs to.
--
-- It replaces Data.lua's static blurb rather than adding to it: the blurb is
-- one line per registry row and says the same thing at every rank, so a RANK
-- UP offer used to read exactly like the NEW offer beside it. Long sentences
-- arrive in pieces and are joined here in arrival order.
--
-- A server too old to send ODESC simply never calls this, and the blurb set in
-- the OFFER handler stands.
GauntletProtocol.On("ODESC", function(i, text)
    local o = pendingOffers[tonumber(i)]
    if not o then return end

    -- Joined with exactly one space. The server strips the space it split on,
    -- because a space at the end of an addon message does not survive the trip
    -- to CHAT_MSG_ADDON and "in dungeons.In exchange" is what came back.
    if o.descFromServer then
        o.desc = o.desc .. " " .. (text or "")
    else
        o.desc = text or ""
        o.descFromServer = true
    end
end)

GauntletProtocol.On("OFFER_END", function()
    offers = pendingOffers
    pendingOffers = {}
    if DB("autoOpen") then ShowChooser() else pendingOffer = true; RefreshMain() end
end)

-- ========================================================== leaderboard ====
--
-- The server has sent conducts on the TOP frame since Phase 0 and nothing has
-- ever drawn them: the handler printed one chat line per row and dropped the
-- conduct list into it, which is exactly what GauntletCommands.cpp's own
-- comment says not to do -- "a conduct list is too long to read in a chat
-- frame and the plan gives it to the addon's leaderboard tab". This is that
-- tab. The chat lines the server sends are untouched, because fallback mode
-- has nothing else.
--
-- Conducts are the class curses a run carried when it ended. They are the
-- run's epitaph and the reason the leaderboard is more than a number, so they
-- get the whole width of a tooltip rather than a truncated tail.

local TOP_ROWS   = 10
local TOP_ROW_H  = 22
local topRows    = {}
local topData    = {}
local topWaiting = 0        -- seconds since /gauntlet top asked, 0 when not asking

local topPanel = Panel("GauntletTop", 420, 62 + TOP_ROWS * TOP_ROW_H, "Furthest Runs")

topPanel.head = topPanel:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
topPanel.head:SetPoint("TOPLEFT", topPanel, "TOPLEFT", 14, -30)
topPanel.head:SetJustifyH("LEFT")
topPanel.head:SetText("Hover a run to see the curses it carried.")

for i = 1, TOP_ROWS do
    local r = CreateFrame("Button", nil, topPanel)
    r:SetWidth(392); r:SetHeight(TOP_ROW_H)
    r:SetPoint("TOPLEFT", topPanel, "TOPLEFT", 14, -46 - (i - 1) * TOP_ROW_H)
    r:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8" })
    r:SetBackdropColor(unpack(ROW_BG))

    r.rank = r:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    r.rank:SetPoint("LEFT", r, "LEFT", 6, 0)
    r.rank:SetWidth(24); r.rank:SetJustifyH("RIGHT")

    r.name = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    r.name:SetPoint("LEFT", r.rank, "RIGHT", 8, 0)
    r.name:SetWidth(110); r.name:SetJustifyH("LEFT")

    r.tier = r:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    r.tier:SetPoint("LEFT", r.name, "RIGHT", 4, 0)
    r.tier:SetWidth(90); r.tier:SetJustifyH("LEFT")

    r.cause = r:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    r.cause:SetPoint("LEFT", r.tier, "RIGHT", 4, 0)
    r.cause:SetPoint("RIGHT", r, "RIGHT", -6, 0)
    r.cause:SetJustifyH("LEFT")

    AddGlow(r)
    r:SetScript("OnEnter", function(self)
        self.glow:Show()
        local d = self.data
        if not d then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(d.name, 1, 0.82, 0)
        GameTooltip:AddLine(("Tier %s at level %s"):format(d.tier, d.level), 1, 1, 1)
        GameTooltip:AddLine(d.cause, 0.8, 0.8, 0.8, true)
        if d.conducts and d.conducts ~= "" then
            GameTooltip:AddLine(" ")
            GameTooltip:AddLine("Conducts", 1, 0.82, 0)
            GameTooltip:AddLine(d.conducts, 0.6, 0.85, 0.6, true)
        else
            GameTooltip:AddLine(" ")
            GameTooltip:AddLine("No class curses carried.", 0.5, 0.5, 0.5)
        end
        GameTooltip:Show()
    end)
    r:SetScript("OnLeave", function(self) self.glow:Hide(); GameTooltip:Hide() end)
    r:Hide()
    topRows[i] = r
end

topPanel.empty = topPanel:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
topPanel.empty:SetPoint("TOPLEFT", topPanel, "TOPLEFT", 16, -52)
topPanel.empty:Hide()

local function RefreshTop()
    local n = 0
    for i = 1, TOP_ROWS do
        local d = topData[i]
        if d then
            topRows[i].data = d
            topRows[i].rank:SetText("#" .. d.rank)
            topRows[i].name:SetText(d.name)
            topRows[i].tier:SetText(("tier |cffffd100%s|r"):format(d.tier))
            -- Marked when the run carried conducts, because that is the whole
            -- reason to hover a row and nothing else on the line hints at it.
            local mark = (d.conducts and d.conducts ~= "") and "|cff60c060*|r " or ""
            topRows[i].cause:SetText(mark .. d.cause)
            topRows[i]:Show(); n = i
        else
            topRows[i].data = nil
            topRows[i]:Hide()
        end
    end

    if n == 0 then
        topPanel.empty:SetText(topWaiting > 0 and "Asking the server..."
                                              or "No completed runs yet.")
        topPanel.empty:Show()
    else
        topPanel.empty:Hide()
    end
    topPanel:SetHeight(62 + math.max(n, 1) * TOP_ROW_H)
end

-- The server sends one TOP frame per row and no end marker, so rank 1 is the
-- start of a batch. That is enough: the query is ORDER BY tier DESC LIMIT 10
-- and always begins at 1, and a second /gauntlet top simply replaces the list.
GauntletProtocol.On("TOP", function(rank, name, tier, level, cause, conducts)
    local i = tonumber(rank)
    if not i or i < 1 or i > TOP_ROWS then return end
    if i == 1 then topData = {} end

    topData[i] = {
        rank     = tostring(rank),
        name     = tostring(name or "?"),
        tier     = tostring(tier or "?"),
        level    = tostring(level or "?"),
        cause    = tostring(cause or ""),
        conducts = conducts and tostring(conducts) or "",
    }

    topWaiting = 0
    RefreshTop()
    topPanel:Show()
end)

-- Nothing came back within a few seconds of asking, which on a fresh realm is
-- the ordinary answer rather than a fault: gauntlet_leaderboard is empty until
-- somebody finishes a run. Said plainly instead of leaving "Asking the
-- server..." on screen forever.
topPanel:SetScript("OnUpdate", function(self, elapsed)
    if topWaiting <= 0 then return end
    topWaiting = topWaiting + elapsed
    if topWaiting < 4 then return end
    topWaiting = 0
    RefreshTop()
end)

local function AskForTop()
    topData = {}
    topWaiting = 0.001   -- non-zero starts the timer; see OnUpdate
    RefreshTop()
    topPanel:Show()
    SendChatMessage(".gauntlet top", "SAY")
end

-- What the whole carried set adds up to. Server-computed on purpose: six of
-- the seven are aggregate products after the caps have clamped them, and an
-- addon adding up the boons it can see would show a player the number before
-- the clamp while they lived with the number after it.
GauntletProtocol.On("TOTALS", function(taken, done, heal, maxhp, speed, xp, gold)
    totals = {
        taken = (tonumber(taken) or 100) / 100,
        done  = (tonumber(done)  or 100) / 100,
        heal  = (tonumber(heal)  or 100) / 100,
        maxhp = (tonumber(maxhp) or 100) / 100,
        speed = (tonumber(speed) or 100) / 100,
        xp    = (tonumber(xp)    or 100) / 100,
        gold  = (tonumber(gold)  or 100) / 100,
    }
    RefreshMain()
end)

GauntletProtocol.On("PACE", function(timed, budgetPct, minSpacingSecs)
    pace = {
        timed = tonumber(timed) or 0,
        mult  = (tonumber(budgetPct) or 100) / 100,
        space = tonumber(minSpacingSecs) or 0,
    }
    if main:IsShown() then RefreshMain() end
end)

GauntletProtocol.OnModeChange(function(newMode)
    if newMode == "protocol" then
        -- Drop any state a chat scrape may have accumulated during "pending"
        -- so a late HELLO cannot leave stale fallback data mixed in.
        offers, mode, pendingOffer = {}, nil, false
        chooser:Hide()
    end
    if main:IsShown() then RefreshMain() end
end)

-- ====================================================== minimap button ====
minimapBtn = CreateFrame("Button", "GauntletMinimapButton", Minimap)
minimapBtn:SetWidth(31); minimapBtn:SetHeight(31)
minimapBtn:SetFrameStrata("MEDIUM")
minimapBtn:SetMovable(true)

local mmIcon = minimapBtn:CreateTexture(nil, "BACKGROUND")
mmIcon:SetWidth(19); mmIcon:SetHeight(19)
mmIcon:SetPoint("CENTER", minimapBtn, "CENTER", 0, 1)
mmIcon:SetTexture("Interface\\Icons\\Ability_Warrior_ShieldBash")
mmIcon:SetTexCoord(0.1, 0.9, 0.1, 0.9)

local mmRing = minimapBtn:CreateTexture(nil, "OVERLAY")
mmRing:SetWidth(52); mmRing:SetHeight(52)
mmRing:SetPoint("TOPLEFT", minimapBtn, "TOPLEFT", 0, 0)
mmRing:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")

local mmGlow = minimapBtn:CreateTexture(nil, "OVERLAY")
mmGlow:SetTexture("Interface\\Buttons\\ButtonHilight-Square")
mmGlow:SetBlendMode("ADD")
mmGlow:SetWidth(24); mmGlow:SetHeight(24)
mmGlow:SetPoint("CENTER", minimapBtn, "CENTER", 0, 1)
mmGlow:Hide()

local function PlaceMinimap()
    local a = math.rad(DB("minimapAngle"))
    minimapBtn:SetPoint("CENTER", Minimap, "CENTER", 80 * math.cos(a), 80 * math.sin(a))
end

minimapBtn:RegisterForDrag("LeftButton")
minimapBtn:SetScript("OnDragStart", function(self) self.dragging = true end)
minimapBtn:SetScript("OnDragStop", function(self) self.dragging = false end)
minimapBtn:SetScript("OnUpdate", function(self)
    if not self.dragging then return end
    local mx, my = Minimap:GetCenter()
    local px, py = GetCursorPosition()
    local scale = Minimap:GetEffectiveScale()
    GauntletUIDB.minimapAngle = math.deg(math.atan2(py / scale - my, px / scale - mx))
    PlaceMinimap()
end)
minimapBtn:SetScript("OnEnter", function(self)
    mmGlow:Show()
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:SetText("The Gauntlet", 1, 0.82, 0)
    GameTooltip:AddLine(("Tier %d  -  %s"):format(run.tier, run.state), 1, 1, 1)
    if pendingOffer then GameTooltip:AddLine("A choice is waiting.", 1, 0.82, 0) end
    GameTooltip:AddLine(GauntletProtocol.mode == "protocol" and "Protocol: connected"
                         or "Protocol: chat fallback", 0.6, 0.6, 0.6)
    GameTooltip:AddLine("Click to open, drag to move.", 0.6, 0.6, 0.6)
    GameTooltip:Show()
end)
minimapBtn:SetScript("OnLeave", function() mmGlow:Hide(); GameTooltip:Hide() end)
minimapBtn:SetScript("OnClick", function()
    if main:IsShown() then main:Hide() return end
    if GauntletProtocol.mode == "protocol" then
        if run.tier == 0 and not next(affixes) then GauntletProtocol.SendSync() end
    else
        affixes, mode = {}, "status"
        SendChatMessage(".gauntlet status", "SAY")
    end
    RefreshMain(); main:Show()
end)

-- =============================================================== events ====
local ev = CreateFrame("Frame")
ev:RegisterEvent("PLAYER_LOGIN")
ev:RegisterEvent("PLAYER_REGEN_ENABLED")
ev:SetScript("OnEvent", function(_, event)
    if event == "PLAYER_LOGIN" then
        PlaceMinimap()
        if not DB("showMinimap") then minimapBtn:Hide() end
    elseif event == "PLAYER_REGEN_ENABLED" and pendingOffer and next(offers) then
        ShowChooser()
    end
end)

SLASH_GAUNTLET1 = "/gauntlet"
SlashCmdList["GAUNTLET"] = function(cmd)
    cmd = (cmd or ""):lower()
    if cmd == "top" then AskForTop() return end
    if cmd == "pick" and next(offers) then ShowChooser() return end
    if cmd == "config" then gear:GetScript("OnClick")() return end
    if main:IsShown() then main:Hide() return end
    if GauntletProtocol.mode == "protocol" then
        -- TODO(design): staleness heuristic - "never received a snapshot" is
        -- the only case the spec gives a name to, so that is what triggers a
        -- re-SYNC; anything richer needs a signal Phase 0 does not have yet.
        if run.tier == 0 and not next(affixes) then
            GauntletProtocol.SendSync()
        end
    else
        affixes, mode = {}, "status"
        SendChatMessage(".gauntlet status", "SAY")
    end
    RefreshMain(); main:Show()
end
