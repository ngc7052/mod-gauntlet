-- GauntletUI - client interface for mod-gauntlet
--
--  * minimap button, draggable around the ring, position saved
--  * main window: run status, every affix carried, settings
--  * affix chooser: three icons with descriptions, shown only out of combat
--
-- The server communicates over system chat; those lines are consumed here and
-- hidden from the chat frame.

local ADDON = "GauntletUI"

GauntletUIDB = GauntletUIDB or {}
local defaults = { minimapAngle = 210, showMinimap = true, autoOpen = true, suppressChat = true }

local offers, affixes = {}, {}
local mode, pendingOffer = nil, false
local run = { seed = "?", tier = 0, state = "alive" }

local function DB(k)
    if GauntletUIDB[k] == nil then GauntletUIDB[k] = defaults[k] end
    return GauntletUIDB[k]
end

local function Strip(s)
    if not s then return "" end
    return (s:gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", ""))
end

-- Affixes arrive as text, so the icon and colour are inferred from the wording.
local function IconFor(desc)
    desc = (desc or ""):lower()
    if desc:find("more damage") and desc:find("take") then
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

local SEVERITY_COLOUR = {
    trivial  = { 0.6, 0.6, 0.6 },
    minor    = { 0.4, 0.8, 0.4 },
    moderate = { 0.9, 0.9, 0.3 },
    major    = { 1.0, 0.6, 0.1 },
    severe   = { 1.0, 0.3, 0.2 },
    dire     = { 0.8, 0.1, 0.9 },
}
local function ColourFor(desc)
    local sev = (desc or ""):match("%[(%a+)%]")
    return SEVERITY_COLOUR[(sev or ""):lower()] or { 0.7, 0.6, 0.2 }
end

-- ======================================================== panel factory ====
local function MakePanel(name, w, h, titleText)
    local f = CreateFrame("Frame", name, UIParent)
    f:SetWidth(w); f:SetHeight(h)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 80)
    f:SetBackdrop({
        bgFile   = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 },
    })
    f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", f.StartMoving)
    f:SetScript("OnDragStop", f.StopMovingOrSizing)
    f:SetFrameStrata("DIALOG")
    f:SetClampedToScreen(true)
    f:Hide()

    f.title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    f.title:SetPoint("TOP", f, "TOP", 0, -16)
    f.title:SetText(titleText)

    local x = CreateFrame("Button", nil, f, "UIPanelCloseButton")
    x:SetPoint("TOPRIGHT", f, "TOPRIGHT", -6, -6)
    return f
end

-- ============================================================== chooser ====
local chooser = MakePanel("GauntletChooser", 560, 260, "Choose Your Affix")
chooser.sub = chooser:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
chooser.sub:SetPoint("TOP", chooser.title, "BOTTOM", 0, -4)

local choiceBtns = {}
for i = 1, 3 do
    local b = CreateFrame("Button", nil, chooser)
    b:SetWidth(520); b:SetHeight(56)
    b:SetPoint("TOP", chooser, "TOP", 0, -62 - (i - 1) * 60)
    b:SetBackdrop({
        bgFile = "Interface\\Buttons\\WHITE8X8",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    b:SetBackdropColor(0.08, 0.08, 0.08, 0.9)

    b.icon = b:CreateTexture(nil, "ARTWORK")
    b.icon:SetWidth(42); b.icon:SetHeight(42)
    b.icon:SetPoint("LEFT", b, "LEFT", 8, 0)
    b.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)

    b.name = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    b.name:SetPoint("TOPLEFT", b.icon, "TOPRIGHT", 10, -2)
    b.name:SetJustifyH("LEFT")

    b.desc = b:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    b.desc:SetPoint("TOPLEFT", b.name, "BOTTOMLEFT", 0, -3)
    b.desc:SetWidth(440); b.desc:SetJustifyH("LEFT")

    b.index = i
    b:SetScript("OnEnter", function(self) self:SetBackdropBorderColor(1, 0.82, 0, 1) end)
    b:SetScript("OnLeave", function(self)
        local c = self.colour or { 0.7, 0.6, 0.2 }
        self:SetBackdropBorderColor(c[1], c[2], c[3], 1)
    end)
    b:SetScript("OnClick", function(self)
        SendChatMessage(".gauntlet pick " .. self.index, "SAY")
        chooser:Hide(); offers = {}; pendingOffer = false
    end)
    b:Hide()
    choiceBtns[i] = b
end

local function ShowChooser()
    if InCombatLockdown() then pendingOffer = true return end
    local n = 0
    for i = 1, 3 do
        local o = offers[i]
        if o then
            local c = ColourFor(o.desc)
            choiceBtns[i].colour = c
            choiceBtns[i].icon:SetTexture(IconFor(o.desc))
            choiceBtns[i].name:SetText("|cffffd100" .. o.name .. "|r")
            choiceBtns[i].desc:SetText(o.desc or "")
            choiceBtns[i]:SetBackdropBorderColor(c[1], c[2], c[3], 1)
            choiceBtns[i]:Show(); n = n + 1
        else
            choiceBtns[i]:Hide()
        end
    end
    if n == 0 then return end
    chooser.title:SetText("Tier " .. run.tier .. " - Choose Your Affix")
    chooser.sub:SetText("Permanent. It stacks with everything you already carry.")
    chooser:SetHeight(84 + n * 60)
    chooser:Show()
    pendingOffer = false
end

-- ================================================================= main ====
local main = MakePanel("GauntletMain", 520, 400, "The Gauntlet")
main.head = main:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
main.head:SetPoint("TOP", main.title, "BOTTOM", 0, -6)

main.hint = main:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
main.hint:SetPoint("BOTTOM", main, "BOTTOM", 0, 16)

local rows = {}
for i = 1, 16 do
    local r = CreateFrame("Frame", nil, main)
    r:SetWidth(470); r:SetHeight(34)
    r:SetPoint("TOPLEFT", main, "TOPLEFT", 24, -70 - (i - 1) * 36)

    r.icon = r:CreateTexture(nil, "ARTWORK")
    r.icon:SetWidth(28); r.icon:SetHeight(28)
    r.icon:SetPoint("LEFT", r, "LEFT", 0, 0)
    r.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93)

    r.name = r:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    r.name:SetPoint("TOPLEFT", r.icon, "TOPRIGHT", 8, -1)
    r.name:SetJustifyH("LEFT")

    r.desc = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    r.desc:SetPoint("TOPLEFT", r.name, "BOTTOMLEFT", 0, -2)
    r.desc:SetWidth(420); r.desc:SetJustifyH("LEFT")
    r:Hide()
    rows[i] = r
end

-- settings live at the foot of the main window
local cbMinimap = CreateFrame("CheckButton", "GauntletCbMinimap", main, "UICheckButtonTemplate")
cbMinimap:SetWidth(24); cbMinimap:SetHeight(24)
cbMinimap:SetPoint("BOTTOMLEFT", main, "BOTTOMLEFT", 22, 36)
getglobal(cbMinimap:GetName() .. "Text"):SetText("Minimap button")

local cbAuto = CreateFrame("CheckButton", "GauntletCbAuto", main, "UICheckButtonTemplate")
cbAuto:SetWidth(24); cbAuto:SetHeight(24)
cbAuto:SetPoint("LEFT", cbMinimap, "RIGHT", 130, 0)
getglobal(cbAuto:GetName() .. "Text"):SetText("Auto-open chooser")

local minimapBtn  -- forward declaration

local function ShowMain()
    local colour = (run.state == "alive") and "|cff20ff20alive|r" or "|cffff2020RETIRED|r"
    main.head:SetText("Seed |cffffd100" .. run.seed .. "|r    Tier |cffffd100"
                      .. run.tier .. "|r    " .. colour)

    local shown = 0
    for i = 1, 16 do
        local a = affixes[i]
        if a then
            local c = ColourFor(a.desc)
            rows[i].icon:SetTexture(IconFor(a.desc))
            rows[i].name:SetText(a.name)
            rows[i].name:SetTextColor(c[1], c[2], c[3])
            rows[i].desc:SetText(a.desc or "")
            rows[i]:Show(); shown = i
        else
            rows[i]:Hide()
        end
    end

    if shown == 0 then
        main.hint:SetText("No affixes yet - the first arrives at level 5.")
    elseif pendingOffer then
        main.hint:SetText("|cffffd100A choice is waiting. Leave combat to pick.|r")
    else
        main.hint:SetText("/gauntlet top for the leaderboard")
    end

    main:SetHeight(math.max(220, 118 + shown * 36))
    cbMinimap:SetChecked(DB("showMinimap"))
    cbAuto:SetChecked(DB("autoOpen"))
    main:Show()
end

cbMinimap:SetScript("OnClick", function(self)
    GauntletUIDB.showMinimap = self:GetChecked() and true or false
    if minimapBtn then
        if GauntletUIDB.showMinimap then minimapBtn:Show() else minimapBtn:Hide() end
    end
end)
cbAuto:SetScript("OnClick", function(self)
    GauntletUIDB.autoOpen = self:GetChecked() and true or false
end)

-- ====================================================== minimap button ====
local function PlaceMinimap(btn)
    local a = math.rad(DB("minimapAngle"))
    btn:SetPoint("CENTER", Minimap, "CENTER", 80 * math.cos(a), 80 * math.sin(a))
end

minimapBtn = CreateFrame("Button", "GauntletMinimapButton", Minimap)
minimapBtn:SetWidth(31); minimapBtn:SetHeight(31)
minimapBtn:SetFrameStrata("MEDIUM")
minimapBtn:SetMovable(true)

local mmIcon = minimapBtn:CreateTexture(nil, "BACKGROUND")
mmIcon:SetWidth(20); mmIcon:SetHeight(20)
mmIcon:SetPoint("CENTER", minimapBtn, "CENTER", 0, 1)
mmIcon:SetTexture("Interface\\Icons\\Ability_Warrior_ShieldBash")
mmIcon:SetTexCoord(0.07, 0.93, 0.07, 0.93)

local mmBorder = minimapBtn:CreateTexture(nil, "OVERLAY")
mmBorder:SetWidth(53); mmBorder:SetHeight(53)
mmBorder:SetPoint("TOPLEFT", minimapBtn, "TOPLEFT", 0, 0)
mmBorder:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")

minimapBtn:RegisterForDrag("LeftButton")
minimapBtn:SetScript("OnDragStart", function(self) self.dragging = true end)
minimapBtn:SetScript("OnDragStop", function(self) self.dragging = false end)
minimapBtn:SetScript("OnUpdate", function(self)
    if not self.dragging then return end
    local mx, my = Minimap:GetCenter()
    local cx, cy = GetCursorPosition()
    local scale = UIParent:GetEffectiveScale()
    cx, cy = cx / scale, cy / scale
    GauntletUIDB.minimapAngle = math.deg(math.atan2(cy - my, cx - mx))
    PlaceMinimap(self)
end)
minimapBtn:SetScript("OnClick", function()
    if main:IsShown() then main:Hide() else
        affixes, mode = {}, "status"
        ShowMain()
        SendChatMessage(".gauntlet status", "SAY")
    end
end)
minimapBtn:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:AddLine("The Gauntlet")
    GameTooltip:AddLine("Tier " .. run.tier .. "  -  " .. run.state, 1, 1, 1)
    if pendingOffer then GameTooltip:AddLine("A choice is waiting.", 1, 0.8, 0) end
    GameTooltip:AddLine("Click to open.", 0.6, 0.6, 0.6)
    GameTooltip:Show()
end)
minimapBtn:SetScript("OnLeave", function() GameTooltip:Hide() end)

-- ================================================================ parse ====
local function OnSystem(raw)
    local msg = Strip(raw)

    local tier = msg:match("Tier (%d+) reached")
    if tier then
        offers, mode, run.tier = {}, "offer", tonumber(tier)
        return
    end

    local seed, t, state = msg:match("seed (%d+)%s*|%s*tier (%d+)%s*|%s*(%a+)")
    if seed then
        run.seed, run.tier, run.state = seed, tonumber(t), state:lower()
        affixes, mode = {}, "status"
        if main:IsShown() then ShowMain() end
        return
    end

    local rseed, rtier = msg:match("Run seed (%d+) %- tier (%d+)")
    if rseed then run.seed, run.tier = rseed, tonumber(rtier) return end

    local idx, name, desc = msg:match("^%s*(%d+)%.%s+(.-)%s+%-%s+(.+)$")
    if idx then
        idx = tonumber(idx)
        if mode == "offer" and idx <= 3 then
            offers[idx] = { name = name, desc = desc }
            if DB("autoOpen") then ShowChooser() else pendingOffer = true end
        elseif mode == "status" then
            affixes[idx] = { name = name, desc = desc }
            if main:IsShown() then ShowMain() end
        end
        return
    end

    if msg:find("You bear") then
        chooser:Hide(); offers, mode, pendingOffer = {}, nil, false
    end
end

local function IsGauntletLine(msg)
    if not msg then return false end
    local s = Strip(msg)
    if s:find("%[Gauntlet%]") then return true end
    if mode and s:match("^%s*%d+%.%s") then return true end
    return false
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

-- =============================================================== events ====
local ev = CreateFrame("Frame")
ev:RegisterEvent("PLAYER_LOGIN")
ev:RegisterEvent("PLAYER_REGEN_ENABLED")
ev:SetScript("OnEvent", function(_, event)
    if event == "PLAYER_LOGIN" then
        PlaceMinimap(minimapBtn)
        if not DB("showMinimap") then minimapBtn:Hide() end
        DEFAULT_CHAT_FRAME:AddMessage("|cffff2020[Gauntlet]|r ready - minimap button or |cffffd100/gauntlet|r")
    elseif event == "PLAYER_REGEN_ENABLED" then
        -- Out of combat: now is the moment to present a waiting choice.
        if pendingOffer and next(offers) then ShowChooser() end
    end
end)

SLASH_GAUNTLET1 = "/gauntlet"
SlashCmdList["GAUNTLET"] = function(cmd)
    cmd = (cmd or ""):lower()
    if cmd == "top" then SendChatMessage(".gauntlet top", "SAY") return end
    if cmd == "pick" and next(offers) then ShowChooser() return end
    if main:IsShown() then main:Hide() return end
    affixes, mode = {}, "status"
    ShowMain()
    SendChatMessage(".gauntlet status", "SAY")
end
