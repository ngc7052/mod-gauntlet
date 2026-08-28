-- GauntletUI - client interface for mod-gauntlet
-- Reads the server's Gauntlet messages and presents affix choices as buttons.

local ADDON = "GauntletUI"
local offers, currentTier = {}, 0

-- ---------------------------------------------------------------- frame ----
local f = CreateFrame("Frame", "GauntletFrame", UIParent)
f:SetWidth(440); f:SetHeight(210)
f:SetPoint("CENTER", UIParent, "CENTER", 0, 120)
f:SetBackdrop({
    bgFile   = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
f:SetScript("OnDragStart", f.StartMoving)
f:SetScript("OnDragStop", f.StopMovingOrSizing)
f:Hide()

local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", f, "TOP", 0, -18)
title:SetText("The Gauntlet")

local subtitle = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
subtitle:SetPoint("TOP", title, "BOTTOM", 0, -4)
subtitle:SetText("Choose your affix - this cannot be undone.")

-- ------------------------------------------------------------- buttons ----
local buttons = {}
for i = 1, 3 do
    local b = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
    b:SetWidth(400); b:SetHeight(42)
    b:SetPoint("TOP", f, "TOP", 0, -56 - (i - 1) * 48)

    local name = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    name:SetPoint("TOPLEFT", b, "TOPLEFT", 10, -5)
    name:SetJustifyH("LEFT")

    local desc = b:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    desc:SetPoint("TOPLEFT", name, "BOTTOMLEFT", 0, -2)
    desc:SetWidth(380); desc:SetJustifyH("LEFT")

    b.nameText, b.descText, b.index = name, desc, i
    b:SetScript("OnClick", function(self)
        SendChatMessage(".gauntlet pick " .. self.index, "SAY")
        f:Hide()
        wipe(offers)
    end)
    b:Hide()
    buttons[i] = b
end

local function ShowOffers()
    local n = 0
    for i = 1, 3 do
        local o = offers[i]
        if o then
            buttons[i].nameText:SetText("|cffffd100" .. i .. ". " .. o.name .. "|r")
            buttons[i].descText:SetText(o.desc or "")
            buttons[i]:Show()
            n = n + 1
        else
            buttons[i]:Hide()
        end
    end
    if n > 0 then
        title:SetText("The Gauntlet - Tier " .. currentTier)
        f:SetHeight(76 + n * 48)
        f:Show()
    end
end

-- --------------------------------------------------------------- parse ----
-- The server announces offers as system messages; we consume those rather
-- than requiring a separate addon channel.
local function OnSystemMessage(msg)
    if not msg then return end

    local tier = msg:match("Tier (%d+) reached")
    if tier then
        wipe(offers)
        currentTier = tonumber(tier)
        return
    end

    -- "  1. <name> - <description>"
    local idx, name, desc = msg:match("^%s*(%d+)%.%s+(.-)%s+%-%s+(.+)$")
    if idx and currentTier > 0 then
        idx = tonumber(idx)
        if idx >= 1 and idx <= 3 then
            offers[idx] = { name = name, desc = desc }
            ShowOffers()
        end
        return
    end

    if msg:match("You bear") then
        f:Hide()
        wipe(offers)
    end
end

-- --------------------------------------------------------------- events ---
local ev = CreateFrame("Frame")
ev:RegisterEvent("CHAT_MSG_SYSTEM")
ev:RegisterEvent("PLAYER_LOGIN")
ev:SetScript("OnEvent", function(_, event, arg1)
    if event == "CHAT_MSG_SYSTEM" then
        OnSystemMessage(arg1)
    elseif event == "PLAYER_LOGIN" then
        DEFAULT_CHAT_FRAME:AddMessage("|cffff2020[Gauntlet]|r UI loaded. /gauntlet for status.")
    end
end)

SLASH_GAUNTLET1 = "/gauntlet"
SlashCmdList["GAUNTLET"] = function(cmd)
    if cmd == "show" and next(offers) then
        ShowOffers()
    else
        SendChatMessage(".gauntlet status", "SAY")
    end
end
