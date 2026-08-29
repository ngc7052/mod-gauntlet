-- Protocol.lua - the GNT addon channel
--
-- Transport: the server whispers the player from themself using LANG_ADDON
-- (ChatHandler::BuildChatPacket, see AddonChannelCommandHandler::Send at
-- $CORE/src/server/game/Chat/Chat.cpp:1104); the client sees this only via
-- CHAT_MSG_ADDON, never as a visible whisper. Payload is "<type>\t<fields...>"
-- with the "GNT" prefix carried in CHAT_MSG_ADDON's own prefix argument, not
-- embedded in the message text.
--
-- This file only receives, parses, version-gates and decides fallback. It
-- knows nothing about frames or rendering - that is Panel.lua's job.

GauntletProtocol = GauntletProtocol or {}
local P = GauntletProtocol

local ADDON_PREFIX = "GNT"
P.PROTOCOL_VERSION = 7   -- must match Gauntlet::GeneratorVersion (src/Gauntlet.h) and Data.lua's version field

-- mode: "pending" (waiting to find out), "protocol" (GNT confirmed live),
-- "fallback" (chat-scraping; either no HELLO arrived in time, or the
-- version did not match).
P.mode = "pending"
P.loginElapsed = 0

local HELLO_TIMEOUT = 8   -- seconds; plan says "no HELLO within ~8s" -> fall back silently

local callbacks = {}        -- msgType -> function(field1, field2, ...) ; raw strings, uncoerced
local modeCallbacks = {}
local warnedMismatch = false

function P.On(msgType, fn)
    callbacks[msgType] = fn
end

function P.OnModeChange(fn)
    tinsert(modeCallbacks, fn)
end

local function SetMode(m)
    if P.mode == m then return end
    P.mode = m
    for _, fn in ipairs(modeCallbacks) do fn(m) end
end

-- Declared but not emitted before Phase 1. Registered here as no-ops so the
-- dispatcher never has to special-case an unknown type; Panel.lua (or a new
-- Hud.lua) overwrites any of these with GauntletProtocol.On(...) once there
-- is something to draw. One line each so Phase 1 only has to fill the body.
callbacks["EVT"]    = function(key, secs, label) end   -- scheduler countdown (Hud.lua)
callbacks["CTR"]    = function(key, value, max) end    -- counter readout: Champions/Echo/Carrion/... (Hud.lua)
callbacks["STAT"]   = function(key, value) end         -- wound %, Frenzy stacks, Unspent bank (Hud.lua)
callbacks["COND"]   = function(slot, active) end       -- carried-row condition light (Panel.lua)
callbacks["SUMMON"] = function(key, alive) end         -- stalker/ambusher-alive indicator (Hud.lua)
callbacks["KILLBY"] = function(id, name) end           -- which affix acted last on death (surface TBD)

local function Send(msgType, ...)
    SendAddonMessage(ADDON_PREFIX, table.concat({ msgType, ... }, "\t"), "WHISPER", UnitName("player"))
end

function P.SendPick(i)
    Send("PICK", i)
end

function P.SendSync()
    Send("SYNC")
end

local function HandleHello(rawVer)
    local ver = tonumber(rawVer)
    if ver == P.PROTOCOL_VERSION then
        SetMode("protocol")
        return
    end
    if not warnedMismatch then
        warnedMismatch = true
        DEFAULT_CHAT_FRAME:AddMessage(
            ("Gauntlet: server protocol v%s does not match this addon (v%d); falling back to chat."):
                format(tostring(rawVer), P.PROTOCOL_VERSION), 1, 0.5, 0.2)
    end
    SetMode("fallback")
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")

-- Some later client builds require RegisterAddonMessagePrefix before
-- CHAT_MSG_ADDON will fire for a given prefix; 3.3.5a is not believed to
-- (per the task spec; the previous addon never used the addon channel at
-- all, so this could not be confirmed by reading it - see worker report).
-- Guarded so this is a harmless no-op if the function is absent, and correct
-- if it turns out to be required.
if RegisterAddonMessagePrefix then
    RegisterAddonMessagePrefix(ADDON_PREFIX)
end

local didInit = false
local function OnLoginUpdate(self, elapsed)
    P.loginElapsed = P.loginElapsed + elapsed
    if P.mode ~= "pending" then
        self:SetScript("OnUpdate", nil)
        return
    end
    if P.loginElapsed >= HELLO_TIMEOUT then
        self:SetScript("OnUpdate", nil)
        SetMode("fallback")   -- silent: spec says switch quietly when nothing ever answers
    end
end

frame:SetScript("OnEvent", function(self, event, prefix, message, channel, sender)
    if event == "PLAYER_ENTERING_WORLD" then
        -- Fires on true login and on /reload (unlike PLAYER_LOGIN, which
        -- only fires on true login); guarded so a later zone change/loading
        -- screen during the same session does not re-arm the timer.
        if didInit then return end
        didInit = true
        P.loginElapsed = 0
        self:SetScript("OnUpdate", OnLoginUpdate)
        P.SendSync()
        return
    end

    if event ~= "CHAT_MSG_ADDON" then return end
    if prefix ~= ADDON_PREFIX then return end
    if sender and sender ~= UnitName("player") then return end

    local fields = { strsplit("\t", message) }
    local msgType = table.remove(fields, 1)
    if msgType == "HELLO" then
        HandleHello(fields[1])
        return
    end

    local fn = callbacks[msgType]
    if fn then fn(unpack(fields)) end
end)
