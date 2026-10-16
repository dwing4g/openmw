-- Curated unique opponents, using record IDs from the three official ESMs.
-- Friendly/story copies only acquire music if they actually fight the player.
local types = require('openmw.types')

-- Earlier groups win when multiple unique opponents are engaged together.
local encounters = {
    { profile = 'boss_mohg', records = { 'dagoth_ur_1', 'dagoth_ur_2' } },
    { profile = 'boss_radagon', records = { 'almalexia_warrior', 'almalexia', 'vivec_god', 'imperfect' } },
    { profile = 'boss_godfrey', records = {
        'bm_hircine_huntaspect', 'bm_hircine_straspect', 'bm_hircine_spdaspect',
        'tharsten heart-fang2', 'tharsten heart-fang', 'thauraver',
    } },
    { profile = 'boss_godskin', records = {
        'dagoth gares', 'dagoth uthol', 'dagoth odros', 'dagoth vemyn',
        'dagoth endus', 'dagoth tureynul', 'dagoth gilvoth', 'dagoth araynys',
    } },
    { profile = 'boss_fortissax', records = {
        'lich_relvel', 'lich_barilzar', 'dahrk mezalf', 'mastrius', 'raxle berne',
        'dhaunayne aundae', 'volrina quarra', 'draugr_aesliip',
    } },
    { profile = 'boss_rennala', records = {
        'divayth fyr', 'gothren', 'trebonius artorius', 'golena sadri_a', 'golena sadri',
    } },
    { profile = 'boss_black_knives', records = { 'dandras vules', 'king hlaalu helseth', 'carnius magius' } },
    { profile = 'boss_fallen_knights', records = {
        'umbra', 'gaenor_b', 'bm_frost_giant', 'bolvyn venim', 'sjoring hard-heart',
        'orvas dren druglord', 'karrod', 'salas valor', 'ulfgar the unending', 'falx carius2',
        'golden saint_staada', 'dremora_special_fyr', 'dremora_ttmg', 'dremora_lord_khash_uni',
        'goblin_warchief1', 'goblin_warchief2', 'armion', 'yarnar',
    } },
}

local profiles, rules = {}, {}
for priority, encounter in ipairs(encounters) do
    profiles[#profiles + 1] = encounter.profile
    for _, id in ipairs(encounter.records) do
        rules[id] = { profile = encounter.profile, priority = priority }
    end
end

local function targetsActor(fight, id)
    if not fight then return false end
    for _, target in ipairs(fight.targets) do
        if target.id == id then return true end
    end
    return false
end

local function select(player, actors, fightingActors, available)
    local cell = player.cell
    if not cell then return nil end
    local chamber = not cell.isExterior and string.lower(cell.id or '') == "akulakhan's chamber"
    local selected, priority = nil, math.huge
    for _, actor in ipairs(actors) do
        if actor:isValid() and actor.enabled and actor.cell and cell:isInSameSpace(actor)
            and not types.Actor.isDead(actor) then
            local id = string.lower(actor.recordId)
            -- The final Dagoth is distinct from the Facility Cavern encounter.
            -- His chamber theme continues through dialogue and combat pauses,
            -- but stops on death, disablement or leaving the chamber.
            if chamber and id == 'dagoth_ur_2' and available.boss_mohg then
                return 'boss_mohg'
            end
            local rule = rules[id]
            if rule and available[rule.profile] and rule.priority < priority
                and types.Actor.isInActorsProcessingRange(actor)
                and (targetsActor(fightingActors[actor.id], player.id)
                    or targetsActor(fightingActors[player.id], actor.id)) then
                selected, priority = rule.profile, rule.priority
            end
        end
    end
    return selected
end

return { profiles = profiles, encounters = encounters, rules = rules, select = select }
