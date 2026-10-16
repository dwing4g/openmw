-- Exercise the shipped music controller and playlist helpers with OpenMW API stubs.
-- Run: build-tools-local/controls-lua-5.4.8/bin/lua CI/eldenring-test-music.lua
-- This covers playlist behavior, not decoding or listening to the imported audio.
-- The optional generated cell table is never read; all interior fixtures below
-- are authored for these tests, so a clean source checkout needs no game data.
local scriptPath = debug.getinfo(1, 'S').source:sub(2):gsub('\\', '/')
local root = arg[1] or ((scriptPath:match('^(.*)/[^/]+$') or '.') .. '/..')
unpack = unpack or table.unpack

local enabled, soundEnabled, combatEnabled, aiEnabled, dead, scriptPlaying
local files, sections, currentTrack, history, controller
local self = { id = 'player', cell = nil }
local nearby = { actors = {} }
local function section(name)
    sections[name] = sections[name] or {}
    return {
        get = function(_, key)
            if name == 'SettingsOMWMusic' and key == 'CombatMusicEnabled' then return combatEnabled end
            return sections[name][key]
        end,
        set = function(_, key, value) sections[name][key] = value end,
        asTable = function() return sections[name] end,
        setLifeTime = function() end,
    }
end
local ambient = {
    _isEldenRingMusicEnabled = function() return enabled end,
    isMusicPlaying = function() return currentTrack ~= nil end,
    streamMusic = function(path)
        -- Match SoundManager's MWScript priority guard: built-in playlists cannot
        -- interrupt an explicit MWScript StreamMusic command.
        if scriptPlaying then return end
        assert(files[path], 'attempted to play missing file: ' .. tostring(path))
        assert(not path:match('%.json$'), 'attempted to play a manifest')
        currentTrack = path
        history[#history + 1] = path
    end,
    stopMusic = function() if not scriptPlaying then currentTrack = nil end end,
}
local modules = {
    ['openmw.ambient'] = ambient,
    ['openmw.core'] = { sound = { isEnabled = function() return soundEnabled end } },
    ['openmw.debug'] = { isAIEnabled = function() return aiEnabled end },
    ['openmw.self'] = self,
    ['openmw.nearby'] = nearby,
    ['openmw.storage'] = { playerSection = section, LIFE_TIME = { GameSession = 1 } },
    ['openmw.types'] = { Actor = {
        isDead = function(actor) return actor == self and dead or actor.dead == true end,
        isInActorsProcessingRange = function(actor) return actor.inRange ~= false end,
    } },
    ['openmw.vfs'] = {
        pathsWithPrefix = function(prefix)
            local matches = {}
            for path in pairs(files) do
                if path:sub(1, #prefix) == prefix then matches[#matches + 1] = path end
            end
            table.sort(matches)
            local index = 0
            return function() index = index + 1; return matches[index] end
        end,
    },
}
for name, module in pairs(modules) do package.preload[name] = function() return module end end
package.preload['scripts.omw.music.helpers'] = function()
    return dofile(root .. '/files/data-mw/scripts/omw/music/helpers.lua')
end
for _, name in ipairs({ 'eldenring', 'eldenring_bosses' }) do
    package.preload['scripts.omw.music.' .. name] = function()
        return dofile(root .. '/files/data-mw/scripts/omw/music/' .. name .. '.lua')
    end
end
local cellsModule = 'scripts.omw.music.eldenring_cells'
package.loaded[cellsModule] = nil
package.preload[cellsModule] = function() error('optional generated cell table is absent') end
local eldenMusic = require('scripts.omw.music.eldenring')
local eldenBosses = require('scripts.omw.music.eldenring_bosses')

local explore = 'music/explore/morrowind.mp3'
local battle = 'music/battle/morrowind.mp3'
local regions = {
    { 'bitter coast region', 'liurnia' },
    { 'grazelands region', 'limgrave' },
    { 'west gash region', 'altus_plateau' },
    { 'ashlands region', 'mountaintops' },
    { 'molag mar region', 'volcano_manor' }, -- Actual Morrowind.esm record ID for Molag Amur.
    { "azura's coast region", 'liurnia' },
    { 'red mountain region', 'caelid' },
    { 'ascadian isles region', 'limgrave' },
    { 'sheogorad', 'liurnia' }, -- Actual Morrowind.esm ID lacks "Region".
    { 'mournhold region', 'leyndell' },
    { 'felsaad coast region', 'snowfield' },
    { 'moesring mountains region', 'mountaintops' },
    { 'isinfier plains region', 'snowfield' },
    { 'hirstaang forest region', 'limgrave' },
    { 'brodir grove region', 'haligtree' },
    { 'thirsk region', 'snowfield' },
}
local function track(region) return 'music/eldenring/' .. region .. '/explore.ogg' end
local function bossTrack(profile) return 'music/eldenring/' .. profile .. '/battle.ogg' end
local function cell(id, exterior, region)
    return {
        id = id,
        isExterior = exterior == true,
        region = region,
        isInSameSpace = function(this, actor)
            assert(actor and actor.id and actor.cell, 'Cell:isInSameSpace requires a GameObject, not its cell')
            return this.isExterior and actor.cell.isExterior
                or this.isExterior == actor.cell.isExterior and this.id:lower() == actor.cell.id:lower()
        end,
    }
end
local function actor(id, recordId, actorCell)
    return {
        id = id,
        recordId = recordId,
        cell = actorCell,
        enabled = true,
        isValid = function(this) return this.valid ~= false end,
    }
end
local function reset(imported, savedOrders)
    enabled, soundEnabled, combatEnabled, aiEnabled = true, true, true, true
    dead, scriptPlaying = false, false
    currentTrack, history = nil, {}
    sections = { OMWMusicPlaylistsTrackOrder = savedOrders or {} }
    files = { [explore] = true, [battle] = true, ['music/special/mw_death.mp3'] = true }
    for _, id in ipairs(eldenMusic.profiles) do
        if imported == nil or imported[id] then files[track(id)] = true end
        files['music/eldenring/' .. id .. '/manifest.json'] = true
        files['music/eldenring/' .. id .. '/undecoded.wem'] = true
    end
    for _, id in ipairs(eldenBosses.profiles) do
        if imported == nil or imported[id] then files[bossTrack(id)] = true end
        files['music/eldenring/' .. id .. '/manifest.json'] = true
        files['music/eldenring/' .. id .. '/undecoded.wem'] = true
    end
    nearby.actors = {}
    self.cell = { isExterior = true, region = 'ascadian isles region' }
    package.loaded['scripts.omw.music.helpers'] = nil
    controller = dofile(root .. '/files/data-mw/scripts/omw/music/music.lua')
    controller.engineHandlers.onFrame(1 / 60) -- Built-in first-frame initialization delay.
end
local function frame(dt) controller.engineHandlers.onFrame(dt or 1 / 60) end
local function combat(active)
    controller.eventHandlers.OMWMusicCombatTargetsChanged({ actor = actor('enemy', 'ordinary_enemy', self.cell),
        targets = active and { self } or {} })
end
local function combatTargets(combatActor, targets)
    controller.eventHandlers.OMWMusicCombatTargetsChanged({ actor = combatActor, targets = targets })
end
local checks = 0
local function check(actual, expected, reason)
    assert(actual == expected, reason .. ': got ' .. tostring(actual) .. ', expected ' .. tostring(expected))
    checks = checks + 1
end

check(next(eldenMusic.cells), nil, 'source checkout loads without a generated cell table')
check(eldenMusic.select({ id = 'Test Unnamed Interior' }), 'roundtable_hold',
    'missing cell table retains sheltered fallback')
check(eldenMusic.select({ id = 'Test Cave' }), 'caves',
    'missing cell table retains interior name heuristics')
reset()
frame()
check(currentTrack, track('limgrave'), 'controller plays regional music without the cell table')
self.cell = { id = 'Test Cave', isExterior = false }
frame()
check(currentTrack, track('caves'), 'controller plays interior music without the cell table')

-- A small synthetic lookup checks locally generated table integration, including
-- opaque IDs, case folding, and lookup precedence over name heuristics.
local fixtureCells = {
    ['test opaque hall'] = 'caves',
    ['test misleading temple'] = 'tunnels',
    ['test mixedcase identity'] = 'academy',
}
package.loaded[cellsModule] = nil
package.preload[cellsModule] = function() return fixtureCells end
package.loaded['scripts.omw.music.eldenring'] = nil
eldenMusic = require('scripts.omw.music.eldenring')
check(eldenMusic.cells, fixtureCells, 'available local cell table is loaded')

reset()
check(#history, 0, 'initialization does not immediately start music')
for _, region in ipairs(regions) do
    local countBefore = #history
    local trackBefore = currentTrack
    self.cell.region = region[1]
    frame()
    check(currentTrack, track(region[2]), 'requested mapping for ' .. region[1])
    if trackBefore == track(region[2]) then
        check(#history, countBefore, 'crossing into ' .. region[1] .. ' keeps the shared track playing')
    end
end
local previousCount = #history
frame()
check(#history, previousCount, 'same region does not restart playing music')
frame(0)
check(#history, previousCount, 'paused frame does not restart regional music')
currentTrack = nil
frame()
check(currentTrack, track('snowfield'), 'regional track repeats after it finishes')
self.cell.region = 'BITTER COAST REGION'
frame()
check(currentTrack, track('liurnia'), 'region IDs are case insensitive')
self.cell.region = 'molag amur region'
frame()
check(currentTrack, track('volcano_manor'), 'Molag Amur alias also works for mods')
self.cell.isExterior = false
frame()
check(currentTrack, track('roundtable_hold'), 'unnamed interiors use sheltered ambience')
self.cell = { isExterior = true, region = 'ascadian isles region' }
frame()
check(currentTrack, track('limgrave'), 'Ascadian Isles have pastoral music')
self.cell.region = nil
frame()
check(currentTrack, track('limgrave'), 'exterior with no region has default Elden Ring ambience')
self.cell.region = 'new modded region'
frame()
check(currentTrack, track('limgrave'), 'unknown modded exterior has Elden Ring ambience')
self.cell = nil
frame()
check(currentTrack, explore, 'no player cell is safe')

local interiors = {
    { 'Test Opaque Hall', 'caves' },
    { 'Test Misleading Temple', 'tunnels' },
    { 'TEST MIXEDCASE IDENTITY', 'academy' },
    { 'Test Egg Mine', 'tunnels' },
    { 'Test Ancestral Tomb', 'catacombs' },
    { 'Test Grotto', 'underground_rivers' },
    { 'Test Shipwreck, Cabin', 'underground_rivers' },
    { 'Test Dwemer Hall', 'eternal_city' },
    { 'Test Clockwork Room', 'eternal_city' },
    { 'Test Daedric Shrine', 'farum_azula' },
    { 'Test Sixth House Cavern', 'caelid' },
    { 'Test Temple', 'divine_tower' },
    { 'Test Guild of Mages', 'academy' },
    { 'Test Town Office', 'roundtable_hold' },
    { 'Fort Test, General Quarters', 'stormveil' },
    { 'Fort Test, Prison', 'shunning_grounds' },
    { 'Test Royal Palace', 'leyndell' },
    { 'Test Temple Courtyard', 'divine_tower', true },
    { 'Test Sewer Temple', 'shunning_grounds' },
    { 'Test Frozen Cavern', 'snowfield' },
    { 'Test Barrow', 'catacombs' },
    { 'A Mod: New Dwemer Ruins', 'eternal_city' },
    { 'A Mod: New Sixth House Shrine', 'caelid' },
    { 'A Mod: Frozen Cavern', 'snowfield' },
    { 'A Mod: New Tavern', 'roundtable_hold' },
    { 'A Mod: New Temple', 'divine_tower' },
}
reset()
for _, entry in ipairs(interiors) do
    self.cell = { id = entry[1], name = 'Translated display name', hasSky = entry[3], isExterior = false }
    frame()
    check(currentTrack, track(entry[2]), 'interior profile for ' .. entry[1])
end
local count, validProfiles = 0, {}
for _, id in ipairs(eldenMusic.profiles) do validProfiles[id] = true end
for name, profile in pairs(eldenMusic.cells) do
    check(validProfiles[profile], true, 'synthetic cell references an imported profile: ' .. name)
    count = count + 1
end
check(count, 3, 'only the hand-authored synthetic cell fixtures are loaded')
self.cell = { id = 'A Mod: New Courtyard', hasSky = true, isExterior = false, region = 'mournhold region' }
frame()
check(currentTrack, track('leyndell'), 'new quasi-exteriors use their region')

reset()
enabled = false
frame()
check(currentTrack, explore, 'disabled launcher option keeps Morrowind music')
enabled = true
frame()
check(currentTrack, track('limgrave'), 'enabling switches to the current regional music')
enabled = false
frame()
check(currentTrack, explore, 'disabling resumes normal exploration')

reset({})
frame()
check(currentTrack, explore, 'missing assets fall back despite metadata and raw WEM files')
reset({ limgrave = true })
frame()
check(currentTrack, track('limgrave'), 'partial installation uses available regional audio')
self.cell.region = 'west gash region'
frame()
check(currentTrack, explore, 'partial installation falls back for missing region')
self.cell = { id = 'Test Opaque Hall', isExterior = false }
frame()
check(currentTrack, explore, 'partial installation falls back for missing cave music')

reset()
frame()
combat(true)
frame()
check(currentTrack, battle, 'combat interrupts regional exploration')
previousCount = #history
self.cell.region = 'red mountain region'
frame()
check(currentTrack, battle, 'region boundary leaves battle music playing')
check(#history, previousCount, 'region boundary does not restart battle music')
self.cell = { id = 'Test Opaque Hall', isExterior = false }
frame()
check(currentTrack, battle, 'entering a cave leaves battle music playing')
combat(false)
frame()
check(currentTrack, track('caves'), 'combat ends into the new cave profile')
combat(true)
combatEnabled = false
frame()
check(currentTrack, track('caves'), 'existing combat music preference is respected')
combatEnabled, aiEnabled = true, false
frame()
check(currentTrack, track('caves'), 'AI-disabled combat does not force battle music')

reset()
frame()
scriptPlaying, currentTrack = true, 'music/quest/scripted.mp3'
self.cell.region = 'west gash region'
frame()
check(currentTrack, 'music/quest/scripted.mp3', 'regional switch respects engine scripted music guard')
scriptPlaying, currentTrack = false, nil
frame()
check(currentTrack, track('altus_plateau'), 'regional music resumes after scripted track ends')
controller.eventHandlers.Died()
dead = true
self.cell.region = 'red mountain region'
frame()
check(currentTrack, 'music/special/mw_death.mp3', 'death music retains precedence')

-- Old saves have only Morrowind playlist order; imported tracks need no new save.
reset(nil, { explore = { 1 }, battle = { 1 } })
frame(0)
check(currentTrack, track('limgrave'), 'existing save playlist state accepts new regional playlists')
reset(nil, { ['eldenring/limgrave'] = { 99 }, explore = { 1 } })
frame()
check(currentTrack, track('limgrave'), 'changed imported track count repairs stale saved order')
reset()
soundEnabled = false
frame()
check(#history, 0, 'disabled sound does not start regional music')

-- Dagoth Ur's final chamber has an encounter theme before combat starts. His
-- first incarnation in the Facility Cavern still follows ordinary combat rules.
-- Without a generated entry, the chamber uses the sheltered interior fallback.
reset()
self.cell = cell("Akulakhan's Chamber")
local dagoth = actor('dagoth-final', 'dagoth_ur_2', self.cell)
dagoth.inRange = false -- The Heart chamber encounter spans the whole cell.
nearby.actors = { dagoth }
frame()
check(currentTrack, bossTrack('boss_mohg'), 'living Dagoth Ur activates his chamber theme without combat')
previousCount = #history
frame()
check(#history, previousCount, 'chamber theme is not restarted every frame')
dagoth.dead = true
frame()
check(currentTrack, track('roundtable_hold'), 'Dagoth Ur death immediately restores chamber ambience')
dagoth.dead = false
frame()
check(currentTrack, bossTrack('boss_mohg'), 'living chamber encounter can reactivate after a save state change')
dagoth.valid = false
frame()
check(currentTrack, track('roundtable_hold'), 'invalid Dagoth Ur reference cannot keep the encounter active')
dagoth.valid = true
dagoth.enabled = false
frame()
check(currentTrack, track('roundtable_hold'), 'disabled Dagoth Ur reference cannot keep the encounter active')
dagoth.enabled = true
nearby.actors = {}
frame()
check(currentTrack, track('roundtable_hold'), 'removed Dagoth Ur reference cannot keep the encounter active')
nearby.actors = { dagoth }
self.cell = cell('Dagoth Ur, Facility Cavern')
frame()
check(currentTrack, track('caelid'), 'leaving the Heart chamber clears its encounter music')
dagoth.cell = self.cell
frame()
check(currentTrack, track('caelid'), 'final Dagoth Ur outside the Heart chamber does not force a theme while idle')
self.cell = cell("AKULAKHAN'S CHAMBER")
dagoth.cell = cell("akulakhan's chamber")
dagoth.recordId = 'DAGOTH_UR_2'
frame()
check(currentTrack, bossTrack('boss_mohg'), 'encounter record and cell IDs are case insensitive')
dagoth.cell = cell('A different interior')
frame()
check(currentTrack, track('roundtable_hold'), 'a boss in another interior cannot activate the chamber encounter')

-- Unique-enemy themes respond to actual combat with the player, not mere
-- proximity or an unrelated fight between NPCs.
reset()
self.cell = cell('Exterior', true, 'grazelands region')
local umbra = actor('umbra-ref', 'umbra', cell('Adjacent exterior', true, 'grazelands region'))
nearby.actors = { umbra }
frame()
check(currentTrack, track('limgrave'), 'idle Umbra keeps regional exploration music')
combatTargets(umbra, { self })
frame()
check(currentTrack, bossTrack('boss_fallen_knights'), 'Umbra combat replaces ordinary battle music')
previousCount = #history
frame()
check(#history, previousCount, 'continuous boss combat does not restart its theme')
combatTargets(umbra, {})
frame()
check(currentTrack, track('limgrave'), 'ending Umbra combat immediately restores exploration')
combatTargets(umbra, { actor('unrelated-npc', 'ordinary_npc', self.cell) })
frame()
check(currentTrack, battle, 'Umbra dueling another NPC cannot select his boss theme')
combatTargets(umbra, {})
combatTargets(self, { umbra })
frame()
check(currentTrack, bossTrack('boss_fallen_knights'), 'player targeting a boss also establishes the encounter')
combatTargets(self, {})
frame()
check(currentTrack, track('limgrave'), 'ending player-side combat clears the encounter')
combatTargets(umbra, { self })
umbra.inRange = false
frame()
check(currentTrack, battle, 'boss outside actors processing range does not keep special music')
umbra.inRange = true
umbra.dead = true
frame()
check(currentTrack, battle, 'dead unique enemy is ignored even before its combat event clears')
umbra.dead = false
umbra.valid = false
frame()
check(currentTrack, battle, 'invalid unique enemy is ignored even before its combat event clears')
umbra.valid = true
umbra.enabled = false
frame()
check(currentTrack, battle, 'disabled unique enemy cannot keep special music')
umbra.enabled = true
umbra.cell = cell('Unrelated interior')
frame()
check(currentTrack, battle, 'unique enemy in another space cannot keep special music')
umbra.cell = self.cell
nearby.actors = {}
frame()
check(currentTrack, battle, 'unloaded unique enemy cannot keep its special music active')
nearby.actors = { umbra }
umbra.recordId = 'modded_npc_with_umbra_name'
umbra.name = 'Umbra'
frame()
check(currentTrack, battle, 'boss matching uses stable record ID instead of display name')

-- Missing boss files fall back through the existing priority system. Metadata
-- and undecoded WEMs in a profile directory never count as playable music.
reset({ roundtable_hold = true })
self.cell = cell("Akulakhan's Chamber")
dagoth = actor('dagoth-final', 'dagoth_ur_2', self.cell)
nearby.actors = { dagoth }
frame()
check(currentTrack, track('roundtable_hold'), 'old installation without boss music preserves chamber ambience')
combatTargets(dagoth, { self })
frame()
check(currentTrack, battle, 'missing chamber boss recording falls back to battle while fighting')
reset({ liurnia = true, boss_mohg = true })
self.cell = cell('Exterior', true, 'bitter coast region')
umbra = actor('umbra-ref', 'umbra', self.cell)
nearby.actors = { umbra }
combatTargets(umbra, { self })
frame()
check(currentTrack, battle, 'partial boss installation does not substitute a different boss theme')
combatTargets(umbra, {})
frame()
check(currentTrack, track('liurnia'), 'partial installation resumes its available regional music')

reset()
self.cell = cell('Exterior', true, 'bitter coast region')
umbra = actor('umbra-ref', 'umbra', self.cell)
nearby.actors = { umbra }
combatTargets(umbra, { self })
frame()
check(currentTrack, bossTrack('boss_fallen_knights'), 'saved combat target replay activates a unique enemy theme')
enabled = false
frame()
check(currentTrack, battle, 'Elden Ring music option disables unique enemy themes')
enabled, combatEnabled = true, false
frame()
check(currentTrack, track('liurnia'), 'Combat Music option disables unique enemy themes')
combatEnabled, aiEnabled = true, false
frame()
check(currentTrack, track('liurnia'), 'disabled AI disables unique enemy themes')
aiEnabled = true
frame()
check(currentTrack, bossTrack('boss_fallen_knights'), 'reenabling AI resumes a still-active boss encounter')
scriptPlaying, currentTrack = true, 'music/quest/scripted.mp3'
combatTargets(umbra, {})
frame()
combatTargets(umbra, { self })
frame()
check(currentTrack, 'music/quest/scripted.mp3', 'unique enemy theme respects explicit scripted music')
scriptPlaying, currentTrack = false, nil
frame()
check(currentTrack, bossTrack('boss_fallen_knights'), 'boss theme resumes when scripted music ends')
controller.eventHandlers.Died()
dead = true
frame()
check(currentTrack, 'music/special/mw_death.mp3', 'death music retains precedence over boss music')
currentTrack = nil
frame()
check(currentTrack ~= nil and currentTrack:find('/boss_', 1, true) ~= nil, false,
    'boss music cannot restart when the dead player death cue finishes')

reset()
self.cell = cell("Akulakhan's Chamber")
dagoth = actor('dagoth-final', 'dagoth_ur_2', self.cell)
nearby.actors = { dagoth }
combatEnabled = false
frame()
check(currentTrack, track('roundtable_hold'), 'Combat Music option also gates the noncombat chamber encounter')
combatEnabled, aiEnabled = true, false
frame()
check(currentTrack, track('roundtable_hold'), 'disabled AI also gates the noncombat chamber encounter')
aiEnabled, enabled = true, false
frame()
check(currentTrack, explore, 'Elden Ring music option gates the noncombat chamber encounter')
enabled = true
frame()
check(currentTrack, bossTrack('boss_mohg'), 'reenabled chamber encounter does not require a new combat event')

-- A save made before these playlists existed (or with stale order indices) must
-- be able to load directly into an ongoing boss battle.
reset(nil, { explore = { 1 }, battle = { 1 }, ['eldenring/boss_mohg'] = { 99 } })
self.cell = cell("Akulakhan's Chamber")
dagoth = actor('dagoth-final', 'dagoth_ur_2', self.cell)
nearby.actors = { dagoth }
combatTargets(dagoth, { self })
frame()
check(currentTrack, bossTrack('boss_mohg'), 'new and stale saved playlist orders load into an active encounter')

local bossCases = {
    { 'dagoth_ur_1', 'boss_mohg' },
    { 'almalexia_warrior', 'boss_radagon' },
    { 'vivec_god', 'boss_radagon' },
    { 'bm_hircine_huntaspect', 'boss_godfrey' },
    { 'bm_hircine_spdaspect', 'boss_godfrey' },
    { 'bm_hircine_straspect', 'boss_godfrey' },
    { 'dagoth vemyn', 'boss_godskin' },
    { 'dagoth gares', 'boss_godskin' },
    { 'lich_relvel', 'boss_fortissax' },
    { 'draugr_aesliip', 'boss_fortissax' },
    { 'gothren', 'boss_rennala' },
    { 'divayth fyr', 'boss_rennala' },
    { 'trebonius artorius', 'boss_rennala' },
    { 'dandras vules', 'boss_black_knives' },
    { 'umbra', 'boss_fallen_knights' },
    { 'gaenor_b', 'boss_fallen_knights' },
    { 'bm_frost_giant', 'boss_fallen_knights' },
}
check(#eldenBosses.profiles, 8, 'curated encounters use all eight imported boss profiles')
local availableBosses = {}
for _, profile in ipairs(eldenBosses.profiles) do availableBosses[profile] = true end
local uniqueRecords = 0
for recordId, rule in pairs(eldenBosses.rules) do
    check(availableBosses[rule.profile], true, 'unique enemy references an imported boss profile: ' .. recordId)
    uniqueRecords = uniqueRecords + 1
end
check(uniqueRecords, 54, 'all curated official unique-enemy record IDs are covered')
self.cell = cell('Exterior', true, 'grazelands region')
for _, entry in ipairs(bossCases) do
    local enemy = actor('unique-ref', entry[1], self.cell)
    local fights = { [enemy.id] = { actor = enemy, targets = { self } } }
    check(eldenBosses.select(self, { enemy }, {}, availableBosses), nil,
        'idle unique enemy does not select boss music: ' .. entry[1])
    check(eldenBosses.select(self, { enemy }, fights, availableBosses), entry[2],
        'unique enemy selects its intended score: ' .. entry[1])
    check(eldenBosses.select(self, { enemy }, fights, {}), nil,
        'unique enemy with unavailable music falls back: ' .. entry[1])
end
local highPriority = actor('dagoth-ref', 'dagoth_ur_1', self.cell)
local lowPriority = actor('gothren-ref', 'gothren', self.cell)
local simultaneousFights = {
    [highPriority.id] = { actor = highPriority, targets = { lowPriority, self } },
    [lowPriority.id] = { actor = lowPriority, targets = { self } },
}
check(eldenBosses.select(self, { highPriority, lowPriority }, simultaneousFights, availableBosses), 'boss_mohg',
    'simultaneous encounters pick a stable priority and recognize nonfirst combat targets')
check(eldenBosses.select(self, { lowPriority, highPriority }, simultaneousFights, availableBosses), 'boss_mohg',
    'actor enumeration order cannot change simultaneous encounter priority')
availableBosses.boss_mohg = nil
check(eldenBosses.select(self, { highPriority, lowPriority }, simultaneousFights, availableBosses), 'boss_rennala',
    'missing highest priority theme allows another active unique enemy theme')
self.cell = nil
check(eldenBosses.select(self, { highPriority, lowPriority }, simultaneousFights, availableBosses), nil,
    'no player cell safely clears a boss encounter')

print('Regional and boss music validation passed: ' .. checks .. ' runtime assertions')
