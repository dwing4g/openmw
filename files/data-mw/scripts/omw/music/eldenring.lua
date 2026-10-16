-- Exploration profiles; lower playlist priorities still preserve combat and quests.
-- The exact record catalog is generated from the user's game and stays local.
-- A source-only build uses the region and name rules below.
local haveCells, cells = pcall(require, 'scripts.omw.music.eldenring_cells')
if not haveCells then cells = {} end

local regions = {
    -- Marshes, open grassland and dry highlands have distinct exploration moods.
    ['bitter coast region'] = 'liurnia',
    ['ascadian isles region'] = 'limgrave',
    ['grazelands region'] = 'limgrave',
    ['west gash region'] = 'altus_plateau',
    -- Exposed ash wastes stay distinct from Red Mountain's blighted core.
    ['ashlands region'] = 'mountaintops',
    -- The base game's Molag Amur and Sheogorad IDs differ from their display names.
    ['molag mar region'] = 'volcano_manor',
    ['molag amur region'] = 'volcano_manor',
    ["azura's coast region"] = 'liurnia',
    ['sheogorad'] = 'liurnia',
    ['sheogorad region'] = 'liurnia',
    ['red mountain region'] = 'caelid',
    ['mournhold region'] = 'leyndell',
    -- Separate snowy lowlands from glaciated mountains and the southern forest.
    ['felsaad coast region'] = 'snowfield',
    ['moesring mountains region'] = 'mountaintops',
    ['isinfier plains region'] = 'snowfield',
    ['hirstaang forest region'] = 'limgrave',
    -- Brodir's cursed standing stones retain a more mysterious atmosphere.
    ['brodir grove region'] = 'haligtree',
    ['thirsk region'] = 'snowfield',
}

local profiles = {
    'limgrave', 'altus_plateau', 'volcano_manor', 'liurnia', 'caelid',
    'mountaintops', 'snowfield', 'haligtree', 'roundtable_hold', 'leyndell',
    'divine_tower', 'caves', 'tunnels', 'catacombs', 'underground_rivers',
    'eternal_city', 'farum_azula', 'stormveil', 'academy', 'shunning_grounds',
}

local function select(cell)
    if not cell then return nil end
    local region = regions[string.lower(cell.region or '')]
    if cell.isExterior then return region or 'limgrave' end

    -- Use record identity rather than displayName, which can be translated by .cel files.
    local name = string.lower(cell.id or cell.name or '')
    if cells[name] then return cells[name] end
    local function has(...)
        for _, word in ipairs({...}) do
            if name:find(word, 1, true) then return true end
        end
        return false
    end
    -- Added/modded cells still have coverage; special uses precede generic buildings.
    if has('sixth house', 'dagoth ur', 'kogoruhn') then return 'caelid' end
    if has('tomb', 'crypt', 'barrow', 'catacomb', 'sepulcher') then return 'catacombs' end
    if has('sewer', 'canalworks', 'underworks', 'prison', 'jail', 'dungeon') then return 'shunning_grounds' end
    if has('mine', 'quarry') then return 'tunnels' end
    if has('grotto', 'shipwreck', 'flooded') then return 'underground_rivers' end
    if has('dwemer', 'dwarven', 'sotha sil', 'clockwork') then return 'eternal_city' end
    if has('daedric', 'daedra') then return 'farum_azula' end
    if has('ice cave', 'glacier', 'frozen cavern') then return 'snowfield' end
    if has('cave', 'cavern', 'grot', 'lair') then return 'caves' end
    if has('temple', 'shrine', 'chapel', 'sanctuary', 'high fane', 'imperial cult') then return 'divine_tower' end
    if cell.hasSky then return region or 'limgrave' end
    if has('palace', 'royal court') then return 'leyndell' end
    if has('mages', 'telvanni', 'wizard', 'tel fyr') then return 'academy' end
    if has('fort ', 'castle', 'stronghold', 'barracks') then return 'stormveil' end
    return 'roundtable_hold'
end

return { profiles = profiles, regions = regions, cells = cells, select = select }
