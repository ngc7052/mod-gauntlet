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

local BG      = { 0.06, 0.06, 0.07, 0.94 }
local BORDER  = { 0.20, 0.22, 0.28, 1.00 }
local ROW_BG  = { 0.10, 0.10, 0.12, 0.60 }

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

local RANK_PIP = { [1] = "I", [2] = "II", [3] = "III" }
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
local main = Panel("GauntletMain", 340, 260, "The Gauntlet")

main.head = main:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
main.head:SetPoint("TOPLEFT", main, "TOPLEFT", 14, -30)
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

local ROW_H = 26
local rows = {}
for i = 1, 16 do
    local r = CreateFrame("Button", nil, main)
    r:SetWidth(312); r:SetHeight(ROW_H)
    r:SetPoint("TOPLEFT", main, "TOPLEFT", 14, -48 - (i - 1) * ROW_H)
    r:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8" })
    r:SetBackdropColor(unpack(ROW_BG))

    r.icon = r:CreateTexture(nil, "ARTWORK")
    r.icon:SetWidth(18); r.icon:SetHeight(18)
    r.icon:SetPoint("LEFT", r, "LEFT", 4, 0)
    r.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)

    r.name = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    r.name:SetPoint("LEFT", r.icon, "RIGHT", 7, 0)
    r.name:SetJustifyH("LEFT"); r.name:SetWidth(150)

    -- Rank pip and condition light: only populated for protocol-sourced rows
    -- (see RefreshMain); hidden for chat-scraped ones, which have neither.
    r.rank = r:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    r.rank:SetPoint("LEFT", r.name, "RIGHT", 6, 0)
    r.rank:SetWidth(24); r.rank:SetJustifyH("LEFT")

    r.cond = r:CreateTexture(nil, "OVERLAY")
    r.cond:SetWidth(8); r.cond:SetHeight(8)
    r.cond:SetPoint("LEFT", r.rank, "RIGHT", 6, 0)
    r.cond:SetTexture("Interface\\Buttons\\WHITE8X8")
    r.cond:Hide()

    r.tag = r:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    r.tag:SetPoint("RIGHT", r, "RIGHT", -6, 0)

    AddGlow(r)
    r:SetScript("OnEnter", function(self)
        self.glow:Show()
        if not self.data then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(self.data.name, 1, 0.82, 0)
        GameTooltip:AddLine(Body(self.data.desc), 1, 1, 1, true)
        local s = RowColor(self.data)
        GameTooltip:AddLine(s[4], s[1], s[2], s[3])
        GameTooltip:Show()
    end)
    r:SetScript("OnLeave", function(self) self.glow:Hide(); GameTooltip:Hide() end)
    r:Hide()
    rows[i] = r
end

main.empty = main:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
main.empty:SetPoint("TOPLEFT", main, "TOPLEFT", 16, -54)
main.empty:SetText("No affixes yet. The first arrives at level 5.")
main.empty:Hide()

main.foot = main:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
main.foot:SetPoint("BOTTOMLEFT", main, "BOTTOMLEFT", 14, 10)

local function RefreshMain()
    local live = (run.state == "alive")
    main.head:SetText(("Tier |cffffd100%d|r   %s   |cff808080seed %s|r"):format(
        run.tier, live and "|cff40e040alive|r" or "|cffff4040retired|r", run.seed))

    local n = 0
    for i = 1, 16 do
        local a = affixes[i]
        if a then
            local s = RowColor(a)
            rows[i].data = a
            rows[i].icon:SetTexture(RowIcon(a))
            rows[i].name:SetText(a.name)
            rows[i].name:SetTextColor(s[1], s[2], s[3])
            rows[i].tag:SetText(s[4])
            if a.rank then
                rows[i].rank:SetText(RankText(a.rank))
                rows[i].rank:Show()
                rows[i].cond:Show()
                rows[i].cond:SetVertexColor(0.4, 0.4, 0.4, 0.55)   -- neutral: COND is a Phase 1 no-op today
            else
                rows[i].rank:SetText("")
                rows[i].cond:Hide()
            end
            rows[i]:Show(); n = i
        else
            rows[i].data = nil
            rows[i]:Hide()
        end
    end

    if n == 0 then main.empty:Show() else main.empty:Hide() end
    main.foot:SetText(pendingOffer and "|cffffd100A choice is waiting - leave combat.|r"
                                    or "/gauntlet top for the leaderboard")
    main:SetHeight(math.max(120, 76 + math.max(n, 1) * ROW_H))
end

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
            cards[i].name:SetText(o.name)
            cards[i].name:SetTextColor(s[1], s[2], s[3])
            local descText = Body(o.desc)
            if o.kind == "swap" and o.swapName then
                descText = descText .. "  |cffff8040(swaps out " .. o.swapName .. ")|r"
            end
            cards[i].desc:SetText(descText)
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
    chooser.title:SetText("Tier " .. run.tier .. " - Choose Your Affix")
    chooser:SetHeight(62 + n * 54)
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

local pendingCarried, pendingCarriedBySlot = {}, {}
GauntletProtocol.On("AFFIX", function(slot, id, rank, cond, boon, boonMag)
    local info = MechInfo(id)
    local rec = {
        name = info.name, desc = info.desc, icon = info.icon, family = info.family,
        rank = tonumber(rank), cond = tonumber(cond), boon = tonumber(boon),
        boonMag = tonumber(boonMag), slot = tonumber(slot),
    }
    tinsert(pendingCarried, rec)
    pendingCarriedBySlot[rec.slot] = rec
end)

GauntletProtocol.On("AFFIX_END", function()
    -- AFFIX_END marks a complete, authoritative snapshot; replace rather than
    -- merge, since the server may resend this after login, a pick or a swap.
    affixes, carriedBySlot = pendingCarried, pendingCarriedBySlot
    pendingCarried, pendingCarriedBySlot = {}, {}
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

    if o.descFromServer then
        o.desc = o.desc .. (text or "")
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

-- No player-facing leaderboard tab exists yet (out of this file's rendering
-- scope per spec); print it to chat so ".gauntlet top" still shows something
-- once the protocol is active, same as it always has via plain chat text.
GauntletProtocol.On("TOP", function(rank, name, tier, level, cause, conducts)
    DEFAULT_CHAT_FRAME:AddMessage(("Gauntlet Top: #%s %s - tier %s (level %s) - %s [%s]"):
        format(tostring(rank), tostring(name), tostring(tier), tostring(level),
               tostring(cause), tostring(conducts)), 1, 0.82, 0)
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
    if cmd == "top" then SendChatMessage(".gauntlet top", "SAY") return end
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
