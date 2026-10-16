-- Exercise the native combat Lua handler's Elden hit contract with engine stubs.
-- build-tools-local/controls-lua-5.4.8/bin/lua CI/eldenring-test-combat.lua
local scriptPath = debug.getinfo(1, 'S').source:sub(2):gsub('\\', '/')
local root = arg[1] or ((scriptPath:match('^(.*)/[^/]+$') or '.') .. '/..')
local calls = { armor = 0, difficulty = 0, stagger = 0, hit = 0 }
local handler
local self = {}
local combat = {
    addOnHitHandler = function(callback) handler = callback end,
    applyArmor = function(attack)
        calls.armor = calls.armor + 1
        attack.damage.health = attack.damage.health / 2
    end,
    adjustDamageForDifficulty = function(attack)
        calls.difficulty = calls.difficulty + 1
        attack.damage.health = attack.damage.health * 0.8
    end,
    applyStagger = function() calls.stagger = calls.stagger + 1 end,
}
package.loaded['openmw.interfaces'] = { Combat = combat }
package.loaded['openmw.self'] = self
package.loaded['openmw.storage'] = {}
package.loaded['openmw.core'] = { sound = { playSound3d = function() end } }
package.loaded['openmw.types'] = {
    Actor = { _onHit = function(_, attack)
        calls.hit = calls.hit + 1
        calls.damage = attack.damage.health
    end },
    Player = { objectIsInstance = function() return false end },
}
package.loaded['openmw_aux.util'] = { shallowCopy = function(value)
    local copy = {}
    for key, entry in pairs(value) do copy[key] = entry end
    return copy
end }
package.loaded['scripts.omw.combat.common'] = {
    getDamage = function(attack, stat) return attack.damage[stat] or 0 end,
}
dofile(root .. '/files/data-mw/scripts/omw/combat/local.lua')

local function hit(ignoreStagger, successful)
    handler({ successful = successful, attacker = {}, damage = { health = 20 },
        ignoreStagger = ignoreStagger, muteSound = true })
end

hit(true, true)
assert(calls.stagger == 0, 'controller-resolved hits must not receive a second random stagger')
assert(calls.armor == 1 and calls.difficulty == 1, 'Elden hits retain armor and difficulty adjustments')
assert(calls.hit == 1 and calls.damage == 8, 'Elden hit still reaches the native damage callback')
hit(nil, true)
assert(calls.stagger == 1, 'ordinary hits retain the native stagger path')
assert(calls.hit == 2 and calls.damage == 8, 'ordinary hits retain damage processing')
hit(true, false)
assert(calls.stagger == 1 and calls.armor == 2 and calls.difficulty == 2,
    'evaded hits must not cause stagger or armor and difficulty processing')
assert(calls.hit == 3, 'evaded hits still notify the native callback')
print('Combat Lua contract: 7 assertions passed (mock engine APIs; no gameplay validation)')
