"""Local Elden Ring recordings used by the OpenMW music profiles.

IDs are the mod author's documented non-combat WEM IDs:
https://www.nexusmods.com/eldenring/mods/6194?tab=description
The importer verifies every recording against the installed cs_smain.bnk.
No recordings or game assets are distributed with this catalog.
"""

TRACKS = (
    ("limgrave", "Limgrave", 104016490, 485545195),
    ("altus_plateau", "Altus Plateau", 456978507, 437381930),
    ("volcano_manor", "Volcano Manor", 306092369, None),
    ("liurnia", "Liurnia of the Lakes", 246898297, 226381116),
    ("caelid", "Caelid", 627524125, 897726803),
    ("mountaintops", "Mountaintops of the Giants", 187667416, 345327097),
    ("snowfield", "Consecrated Snowfield", 553455681, 534524371),
    ("haligtree", "Miquella's Haligtree", 750464800, None),
    ("roundtable_hold", "Roundtable Hold", 995763001, None),
    ("leyndell", "Leyndell, Royal Capital", 180042125, None),
    ("divine_tower", "Divine Tower", 638250853, None),
    ("caves", "Caves", 435288208, None),
    ("tunnels", "Tunnels", 932889770, None),
    ("catacombs", "Catacombs", 111510624, None),
    ("underground_rivers", "Great Underground Rivers", 1041048659, None),
    ("eternal_city", "Eternal City", 636849333, None),
    ("farum_azula", "Crumbling Farum Azula", 98995798, None),
    ("stormveil", "Stormveil Castle", 739288735, None),
    ("academy", "Academy of Raya Lucaria", 781027474, None),
    ("shunning_grounds", "Subterranean Shunning-Grounds", 993639805, None),
)

LEGACY_REGIONS = {"limgrave", "altus_plateau", "volcano_manor", "liurnia", "caelid"}
REGIONS = {entry[0] for entry in TRACKS}

# Sound IDs from the original Music ID List for Elden Ring, linked by the
# SoulsModding Music List. The importer additionally verifies every ID against
# streamed MusicTrack records in the user's installed cs_smain.bnk.
BOSS_SOURCE_URL = (
    "https://docs.google.com/spreadsheets/d/1lCY5ehc4eBBtDQYsJa5D0zVFqxa73FJj3aFzW6axwMI/edit?gid=0"
)
BOSS_TRACKS = (
    ("boss_mohg", "Mohg, Lord of Blood", 163095055),
    ("boss_godskin", "Godskin Apostles", 939791379),
    ("boss_rennala", "Rennala, Queen of the Full Moon (phase 2)", 478974414),
    ("boss_godfrey", "Godfrey, First Elden Lord (Hoarah Loux, phase 2)", 793246364),
    ("boss_radagon", "The Final Battle (Radagon)", 290780887),
    ("boss_fallen_knights", "Fallen Knights", 986842364),
    ("boss_black_knives", "Black Knives", 1049530399),
    ("boss_fortissax", "Lichdragon Fortissax", 829114909),
)
BOSS_PROFILES = {entry[0] for entry in BOSS_TRACKS}
PROFILES = REGIONS | BOSS_PROFILES
