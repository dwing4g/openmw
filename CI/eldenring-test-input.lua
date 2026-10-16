-- Validate the actual builtin controls scripts with small OpenMW API stubs.
-- Run from the OpenMW source directory:
--   build-tools-local/controls-lua-5.4.8/bin/lua CI/eldenring-test-input.lua
-- An optional first argument supplies the OpenMW source root from any directory.
-- The local interpreter is official Lua 5.4.8; these checks do not establish
-- LuaJIT compatibility, full engine execution, controller hardware or gameplay.
-- No Lua interpreter is required by normal CMake or Windows builds.

local scriptPath = debug.getinfo(1, 'S').source:sub(2):gsub('\\', '/')
local scriptDirectory = scriptPath:match('^(.*)/[^/]+$') or '.'
local root = arg[1] or (scriptDirectory .. '/..')
local actions, bindings, preferences, held, axes, triggers, settingsGroup
local controlSwitches, uiMode, uiTransitions
local elden = true
local self = { controls = {}, ATTACK_TYPE = { Any = 1, NoAttack = 0 } }
local function names() return setmetatable({}, { __index = function(_, key) return key end }) end
local vector = {}
vector.__index = vector
function vector:length2() return self.x * self.x + self.y * self.y end
function vector:normalize()
    local length = math.sqrt(self:length2())
    if length == 0 then return setmetatable({ x = 0, y = 0 }, vector), 0 end
    return setmetatable({ x = self.x / length, y = self.y / length }, vector), length
end
function vector.__mul(v, scale) return setmetatable({ x = v.x * scale, y = v.y * scale }, vector) end
local util = { vector2 = function(x, y) return setmetatable({ x = x, y = y }, vector) end }
local storage = {}
function storage.playerSection(name)
    return {
        get = function(_, key) if name == 'SettingsOMWControls' then return preferences[key] end end,
        set = function(_, key, value) if name == 'SettingsOMWControls' then preferences[key] = value end end,
        subscribe = function() end,
        asTable = function() return {} end,
    }
end
local input = { ACTION = names(), CONTROLLER_AXIS = names(), CONTROLLER_BUTTON = names(), ACTION_TYPE = names() }
local function evaluate(key)
    local value = actions[key] and actions[key].defaultValue or false
    for _, binding in ipairs(bindings[key] or {}) do
        local dependencies = {}
        for _, dep in ipairs(binding.dependencies) do dependencies[#dependencies + 1] = evaluate(dep) end
        value = binding.callback(1 / 60, value, table.unpack(dependencies))
    end
    return value
end
function input._isEldenCombatEnabled() return elden end
function input.registerAction(info) actions[info.key] = info end
function input.bindAction(key, callback, dependencies)
    bindings[key] = bindings[key] or {}
    table.insert(bindings[key], { callback = callback, dependencies = dependencies })
end
function input.registerTrigger() end
function input.registerTriggerHandler(key, callback) triggers[key] = callback end
function input.registerActionHandler() end
function input.activateTrigger(key) if triggers[key] then triggers[key]() end end
function input.isActionPressed(action) return held[action] or false end
function input.getAxisValue(axis) return axes[axis] or 0 end
function input.getBooleanActionValue(key) return evaluate(key) end
function input.getRangeActionValue(key) return evaluate(key) end
local types = {
    Player = {
        CONTROL_SWITCH = names(),
        getControlSwitch = function(_, key) return controlSwitches[key] ~= false end,
        isWerewolf = function() return false end,
        isCharGenFinished = function() return true end,
    },
    Actor = { STANCE = { Weapon = 'weapon', Spell = 'spell' }, getStance = function() return 'weapon' end },
    NPC = {
        isOnGround = function() return true end,
        isSwimming = function() return false end,
        getWalkSpeed = function() return 2 end,
        getRunSpeed = function() return 4 end,
    },
}
local interfaces = {
    Settings = {
        registerPage = function() end,
        registerGroup = function(group)
            settingsGroup = group
            for _, setting in ipairs(group.settings) do preferences[setting.key] = setting.default end
        end,
        registerRenderer = function() end,
    },
    UI = {
        MODE = names(),
        getMode = function() return uiMode end,
        setMode = function(mode)
            uiMode = mode
            uiTransitions = uiTransitions + 1
        end,
        removeMode = function(mode)
            assert(mode == uiMode, 'attempted to remove a different UI mode')
            uiMode = nil
            uiTransitions = uiTransitions + 1
        end,
    },
}
local modules = {
    ['openmw.core'] = { l10n = function() return function(key) return key end end, isWorldPaused = function() return false end },
    ['openmw.debug'] = { isGodMode = function() return true end },
    ['openmw.input'] = input,
    ['openmw.self'] = self,
    ['openmw.storage'] = storage,
    ['openmw.ui'] = {},
    ['openmw.async'] = { callback = function(_, fn) return fn end },
    ['openmw.util'] = util,
    ['openmw.types'] = types,
    ['openmw.interfaces'] = interfaces,
    ['scripts.omw.input.button'] = function() end,
}
for name, module in pairs(modules) do package.preload[name] = function() return module end end

local function script(name) return root .. '/files/data/scripts/omw/input/' .. name .. '.lua' end
for _, name in ipairs({ 'settings', 'actionbindings', 'playercontrols', 'smoothmovement', 'gamepadcontrols' }) do
    assert(loadfile(script(name)), 'invalid Lua syntax: ' .. name)
end

local player, actionBindings
local function reset(enabled)
    elden = enabled
    actions, bindings, preferences, held, axes, triggers = {}, {}, {}, {}, {}, {}
    controlSwitches, uiMode, uiTransitions = {}, nil, 0
    self.controls = { sneak = false }
    -- Match builtin.omwscripts ordering: menu settings, then the player scripts.
    dofile(script('settings'))
    player = dofile(script('playercontrols'))
    actionBindings = dofile(script('actionbindings'))
    dofile(script('smoothmovement'))
end
local function frame() player.engineHandlers.onFrame(1 / 60) end
local checks = 0
local function check(actual, expected, reason)
    if actual ~= expected then error(reason .. ': got ' .. tostring(actual) .. ', expected ' .. tostring(expected)) end
    checks = checks + 1
end

reset(true)
check(preferences.alwaysRun, true, 'Elden default runs normally')
check(preferences.toggleSneak, true, 'Elden default uses crouch toggle')
held.MoveForward = true
frame()
check(self.controls.run, true, 'W runs by default')
held.Run = true -- The native ER binding is Left Alt.
frame()
check(self.controls.run, false, 'Alt + W walks')
held.Run = false
frame()
check(self.controls.run, true, 'releasing Alt returns to running')

held.MoveForward = false
axes.MoveForwardBackward = -0.25
frame()
check(self.controls.run, false, 'light left-stick tilt walks')
check(self.controls.movement, 0.5, 'light stick keeps proportional walk movement')
axes.MoveForwardBackward = -1
frame()
check(self.controls.run, true, 'full left-stick tilt runs')
check(self.controls.movement, 1, 'full stick keeps full movement')
preferences.smoothControllerMovement = false
axes.MoveForwardBackward = -0.25
frame()
check(self.controls.run, false, 'light stick walks with smoothing disabled')
axes.MoveForwardBackward = -1
frame()
check(self.controls.run, true, 'full stick runs with smoothing disabled')

axes.TriggerRight = 0.9
held.Use = false -- Native Elden routing sends RT to HeavyAttack.
frame()
check(self.controls.use, self.ATTACK_TYPE.NoAttack, 'RT heavy does not also start native light attack')
held.Use = true -- The native ER binding is RB / unmodified left click.
frame()
check(self.controls.use, self.ATTACK_TYPE.Any, 'RB / left click starts native light use')
held.Use = false -- Shift / grip chords suppress the native light channel.
held.HeavyAttack = true
frame()
check(self.controls.use, self.ATTACK_TYPE.NoAttack, 'Shift + left click does not also start native light attack')
held.HeavyAttack = false
held.Block = true
frame()
check(self.controls.use, self.ATTACK_TYPE.NoAttack, 'LB / right click guards without a light attack')

input.activateTrigger('ToggleSneak')
frame()
check(self.controls.sneak, true, 'X / L3 toggles crouch on')
input.activateTrigger('ToggleSneak')
frame()
check(self.controls.sneak, false, 'X / L3 toggles crouch off')
input.activateTrigger('Jump')
frame()
check(self.controls.jump, true, 'F / A jump trigger reaches physics controls')
frame()
check(self.controls.jump, false, 'jump pulse clears on next frame')

reset(false)
check(preferences.alwaysRun, false, 'classic default walking preserved')
check(preferences.toggleSneak, false, 'classic hold sneak preserved')
held.MoveForward = true
frame()
check(self.controls.run, false, 'classic W walks by default')
held.Run = true
frame()
check(self.controls.run, true, 'classic run modifier remains active')
axes.TriggerRight = 0.9
held.Use = false
frame()
check(self.controls.use, self.ATTACK_TYPE.Any, 'classic RT native Use fallback retained')

-- Exercise the same action dispatch used by native keyboard/controller bindings,
-- including the real actionbindings bridge and builtin Inventory handler. Native
-- GUI key priority and SDL repeat suppression are outside this Lua harness.
local function inventoryPress()
    actionBindings.engineHandlers.onInputAction(input.ACTION.Inventory)
end
for _, enabled in ipairs({ true, false }) do
    reset(enabled)
    inventoryPress()
    check(uiMode, interfaces.UI.MODE.Interface, 'one inventory action opens the player inventory')
    check(uiTransitions, 1, 'opening inventory changes the UI once')
    inventoryPress()
    check(uiMode, nil, 'the next inventory action closes the player inventory')
    check(uiTransitions, 2, 'closing inventory changes the UI once')

    uiMode = interfaces.UI.MODE.Container
    inventoryPress()
    check(uiMode, nil, 'one inventory action closes a container inventory')
    check(uiTransitions, 3, 'closing a container does not also open the player inventory')
    inventoryPress()
    check(uiMode, interfaces.UI.MODE.Interface, 'next press after container close opens player inventory')
    check(uiTransitions, 4, 'post-container inventory opening changes the UI once')

    for _, mode in ipairs({ 'Settings', 'Journal', 'Dialogue', 'Barter', 'MainMenu' }) do
        uiMode = interfaces.UI.MODE[mode]
        inventoryPress()
        check(uiMode, interfaces.UI.MODE[mode], 'inventory action leaves ' .. mode .. ' unchanged')
        check(uiTransitions, 4, 'inventory action does not change unrelated UI modes')
    end

    controlSwitches.Controls = false
    for _, mode in ipairs({ 'Interface', 'Container' }) do
        uiMode = interfaces.UI.MODE[mode]
        inventoryPress()
        check(uiMode, interfaces.UI.MODE[mode], 'disabled controls prevent closing ' .. mode)
    end
    uiMode = nil
    inventoryPress()
    check(uiMode, nil, 'disabled controls prevent opening inventory')
    check(uiTransitions, 4, 'disabled controls perform no UI transitions')
    controlSwitches.Controls = true

    player.interface.overrideUiControls(true)
    inventoryPress()
    check(uiMode, nil, 'script UI override prevents builtin inventory opening')
    uiMode = interfaces.UI.MODE.Container
    inventoryPress()
    check(uiMode, interfaces.UI.MODE.Container, 'script UI override prevents builtin container closing')
    check(uiTransitions, 4, 'script UI override performs no builtin UI transitions')
    player.interface.overrideUiControls(false)
    inventoryPress()
    check(uiMode, nil, 'inventory action resumes after UI override is released')
    check(uiTransitions, 5, 'restored inventory action closes the container once')
end

print('Lua control validation passed: five parsed scripts, ' .. checks .. ' runtime assertions')
