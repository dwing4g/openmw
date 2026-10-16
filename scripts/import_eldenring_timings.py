#!/usr/bin/env python3
"""Import locally generated Elden Ring action data into the OpenMW combat adapter.

Run ELDEN-RING-Combat-Rewrite/tools/extract.py against your own unpacked game
installation first, then pass its src/sim/extracted.rs to this script. Generated
files contain data from the game; keep the output local. This importer does not
download assets or execute Rust/Python expressions from its input.

The adapter uses canonical Front dodges/reactions and the Stand jump. Jump
attacks combine the airborne hit window/cost with their landing damage metadata;
touchdown switches to the authored full or short landing animation and recovery.
Missing airborne or landing damage data preserves the existing jump fallback.
It imports clip names, weapon stance categories, timing,
cancel windows, invincibility, per-hit motion values/costs and sampled cumulative
root motion. OpenMW applies horizontal displacement through its own physics;
vertical jumps continue to use OpenMW physics. Skeletal playback additionally
requires the locally baked player_anims.bin. Native contact geometry remains in
use. Stance and hit-stop scaling retain the public
simulation's estimates. Run --self-test for parser and conversion fixtures.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import re
import sys
import unittest


class ImportError(ValueError):
    """The generated source is missing or cannot be represented faithfully."""


WEAPON_CLASSES = {
    "Dagger": "Dagger", "Longsword": "StraightSword", "Claymore": "Greatsword",
    "Greatsword": "ColossalSword", "Rapier": "ThrustingSword", "Uchigatana": "Katana",
    "Club": "Hammer", "Battle Axe": "Axe", "Short Spear": "Spear", "Halberd": "Halberd",
    "Heavy Thrusting Sword": "HeavyThrustingSword", "Curved Sword": "CurvedSword",
    "Curved Greatsword": "CurvedGreatsword", "Twinblade": "Twinblade", "Great Hammer": "GreatHammer",
    "Flail": "Flail", "Greataxe": "Greataxe", "Great Spear": "GreatSpear", "Reaper": "Reaper",
    "Whip": "Whip", "Fist": "Fist", "Claw": "Claw", "Colossal Weapon": "ColossalWeapon", "Torch": "Torch",
    "Shield": "Shield",
}
ATTACK_ACTIONS = {
    **{f"Light{i}": f"Light{i}" for i in range(1, 7)},
    **{f"Heavy{i}{suffix}": f"Heavy{i}{suffix}" for i in (1, 2) for suffix in ("", "Charge")},
    **{f"{prefix}Light{i}": f"{prefix}Light{i}" for prefix in ("Left", "Paired") for i in range(1, 7)},
    **{name: name for name in ("RunLight", "RunHeavy", "RollAttack", "CrouchAttack", "BackstepAttack",
                               "GuardCounter", "PairedRun", "PairedRoll", "PairedBackstep")},
    "JumpLightLand": "JumpLight", "JumpHeavyLand": "JumpHeavy", "PairedJumpLand": "PairedJump",
    "JumpLightLandShort": "JumpLight", "JumpHeavyLandShort": "JumpHeavy", "PairedJumpLandShort": "PairedJump",
}
TIMING_FIELDS = {
    "total": "mDuration", "input_from": "mInputFrom", "input_dodge_from": "mDodgeInputFrom",
    "cancel_light": "mCancelLight", "cancel_heavy": "mCancelHeavy", "cancel_dodge": "mCancelDodge",
    "cancel_jump": "mCancelJump", "cancel_guard": "mCancelGuard", "cancel_move": "mCancelMove",
    "cancel_left": "mCancelLeft",
}
MAX_HITS = 32  # Rust's per-hit masks and EldenCombat::MaximumHits both have 32 slots.
TOKEN = re.compile(r'\s*(?:("(?:[^"\\]|\\.)*")|(-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)'
                   r'|([A-Za-z_][A-Za-z_0-9]*)|([{}\[\](),:&]))')


class RustLiteral:
    """A restricted literal grammar for the extractor's structs, arrays and tuples."""

    def __init__(self, text: str):
        self.tokens: list[str] = []
        at = 0
        while at < len(text):
            match = TOKEN.match(text, at)
            if not match:
                if not text[at:].strip():
                    break
                raise ImportError(f"invalid literal near {text[at:at + 35]!r}")
            self.tokens.append(next(part for part in match.groups() if part is not None))
            at = match.end()
        self.at = 0

    def peek(self) -> str:
        return self.tokens[self.at] if self.at < len(self.tokens) else ""

    def take(self, expected: str | None = None) -> str:
        token = self.peek()
        if not token or (expected is not None and token != expected):
            raise ImportError(f"expected {expected or 'literal'}, got {token or 'end of input'}")
        self.at += 1
        return token

    def sequence(self, closing: str) -> list:
        values = []
        while self.peek() != closing:
            values.append(self.value())
            if self.peek() != closing:
                self.take(",")
        self.take(closing)
        return values

    def value(self):
        token = self.take()
        if token == "&":
            self.take("[")
            return self.sequence("]")
        if token == "[":
            return self.sequence("]")
        if token == "(":
            return tuple(self.sequence(")"))
        if token == "None":
            return None
        if token == "Some":
            self.take("(")
            result = self.value()
            self.take(")")
            return result
        if token == "NEVER":
            return 9999.0
        if token in ("true", "false"):
            return token == "true"
        if token.startswith('"'):
            try:
                return json.loads(token)
            except ValueError as error:
                raise ImportError(f"invalid string: {error}") from error
        if token[0].isdigit() or token[0] in "-.":
            number = float(token)
            if not math.isfinite(number) or abs(number) > 3.4028234663852886e38:
                raise ImportError("number is outside the finite C++ float range")
            return number
        if token not in ("ActionDef", "AirAttackDef", "Hit", "WeaponInfo"):
            raise ImportError(f"unsupported literal {token!r}")
        self.take("{")
        fields = {"_type": token}
        while self.peek() != "}":
            key = self.take()
            if not re.fullmatch(r"[A-Za-z_]\w*", key) or key in fields:
                raise ImportError(f"invalid or duplicate field {key!r}")
            self.take(":")
            fields[key] = self.value()
            if self.peek() != "}":
                self.take(",")
        self.take("}")
        return fields

    def parse(self):
        result = self.value()
        if self.at != len(self.tokens):
            raise ImportError(f"trailing token {self.peek()!r}")
        return result


def split_top_level(text: str) -> list[str]:
    parts, start, stack = [], 0, []
    for index, char in enumerate(text):
        if char in "([":
            stack.append(char)
        elif char in ")]":
            if not stack or (stack.pop(), char) not in (("(", ")"), ("[", "]")):
                raise ImportError(f"unbalanced action key {text!r}")
        elif char == "," and not stack:
            parts.append(text[start:index].strip())
            start = index + 1
    if stack:
        raise ImportError(f"unbalanced action key {text!r}")
    return parts + [text[start:].strip()]


def number(fields: dict, key: str) -> float:
    value = fields.get(key)
    if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value) or value < 0:
        raise ImportError(f"{fields.get('name', fields.get('_type', 'data'))}: {key} must be a finite nonnegative number")
    return float(value)


def numeric_tuple(value, size: int, name: str) -> tuple[float, ...]:
    if not isinstance(value, (list, tuple)) or len(value) != size:
        raise ImportError(f"{name} must contain {size} numbers")
    if any(isinstance(item, bool) or not isinstance(item, (int, float)) or not math.isfinite(item) for item in value):
        raise ImportError(f"{name} contains invalid numbers")
    return tuple(float(item) for item in value)


def attack_action(kind: str) -> str | None:
    kind = kind.removeprefix("AttackKind::")
    if kind in ATTACK_ACTIONS:
        return ATTACK_ACTIONS[kind]
    # Accept the indexed form used by some revisions of the recreation, too.
    match = re.fullmatch(r"(Light|LeftLight|PairedLight)\((\d+)\)", kind)
    if match:
        return ATTACK_ACTIONS.get(f"{match[1]}{match[2]}")
    match = re.fullmatch(r"Heavy\(([12]),\s*(true|false)\)", kind)
    if match:
        return f"Heavy{match[1]}" + ("Charge" if match[2] == "true" else "")
    return None


def base_action(key: str) -> tuple[str, str | None] | None:
    key = re.sub(r"(?:ActionId|Load|Dir|JumpKind|HurtLevel)::", "", key)
    if key in ("Backstep", "GuardHit", "GuardBreak"):
        return key, None
    match = re.fullmatch(r"(Roll|CrouchRoll)\((Light|Medium|Heavy),\s*Front\)", key)
    if match:
        return match[1], match[2]
    match = re.fullmatch(r"Hurt\((Small|Middle|Large|Knockdown),\s*Front\)", key)
    if match:
        return ("Knockdown" if match[1] == "Knockdown" else "Hurt" + match[1]), None
    if key == "Jump(Stand)":
        return "Jump", None
    return None


@dataclass
class Definition:
    action: str
    weapon: str | None
    two_hand: bool | None
    load: str | None
    values: dict[str, float]
    hits: list[tuple[float, ...]]
    root_motion: list[tuple[float, ...]]
    animation: str
    stance: tuple[int, int] | None = None
    landing: Definition | None = None
    landing_short: Definition | None = None


def animation_name(fields: dict) -> str:
    source = fields.get("source")
    match = re.fullmatch(r"a(\d{2,3})_(\d{6})", source) if isinstance(source, str) else None
    if match is None:
        raise ImportError("source must be a baked animation identifier, for example a000_027000")
    return f"a{int(match[1]):03d}_{match[2]}"


def convert(action: str, fields: dict, weapon: str | None = None,
            two_hand: bool | None = None, load: str | None = None) -> Definition:
    if not isinstance(fields, dict) or fields.get("_type") != "ActionDef":
        raise ImportError("expected ActionDef literal")
    values = {destination: number(fields, source) for source, destination in TIMING_FIELDS.items()}
    if values["mDuration"] <= 0:
        raise ImportError(f"{action}: duration must be positive")
    iframe = numeric_tuple(fields.get("iframes"), 2, "iframes")
    if not 0 <= iframe[0] <= iframe[1] <= values["mDuration"]:
        raise ImportError(f"{action}: invalid invincibility window")
    values["mInvincibleFrom"], values["mInvincibleTo"] = iframe
    charge = fields.get("charge")
    if charge is not None:
        charge = numeric_tuple(charge, 2, "charge")
        if not 0 <= charge[0] <= charge[1] <= values["mDuration"]:
            raise ImportError(f"{action}: invalid charge window")
        values["mChargeFrom"], values["mChargeTo"] = charge
    values["mStartCost"] = {"Roll": 12., "CrouchRoll": 12., "Backstep": 8., "Jump": 10.}.get(action, 0.)
    values["mStrength"] = (1. if action.endswith("Charge") else 0.75 if action in ("Heavy1", "Heavy2")
                           else 0.9 if action == "GuardCounter" else 0.8 if action in ("RunHeavy", "JumpHeavy") else 0.5)
    motion = fields.get("motion")
    if not isinstance(motion, list) or not motion:
        raise ImportError(f"{action}: missing root-motion samples")
    samples = [numeric_tuple(sample, 3, "motion sample") for sample in motion]
    first, last = samples[0], samples[-1]
    values["mDistance"] = math.hypot(last[0] - first[0], last[2] - first[2])
    raw_hits = fields.get("hits")
    if not isinstance(raw_hits, list):
        raise ImportError(f"{action}: hits must be an array")
    if len(raw_hits) > MAX_HITS:
        raise ImportError(f"{action}: {len(raw_hits)} hits exceed the adapter capacity of {MAX_HITS}; no hits were discarded")
    hits = []
    for hit in raw_hits:
        if not isinstance(hit, dict) or hit.get("_type") != "Hit":
            raise ImportError(f"{action}: expected Hit literal")
        begin, end = number(hit, "from"), number(hit, "to")
        if not begin < end <= values["mDuration"]:
            raise ImportError(f"{action}: hit window lies outside the animation")
        guard = number(hit, "guard_damage")
        hits.append((begin, end, number(hit, "mv"), number(hit, "stamina"), guard * 5., guard, number(hit, "stop") * 0.5))
    return Definition(action, weapon, two_hand, load, values, hits, samples, animation_name(fields))


def merge_air_attack(landing: Definition, airborne: dict, landing_short: Definition | None = None) -> Definition:
    """Keep the source's separate air and landing clocks, sharing the hit's damage data."""
    begin, end = number(airborne, "from"), number(airborne, "to")
    if begin >= end:
        raise ImportError("airborne hit window must have from < to")
    cost = number(airborne, "stamina")
    hit = landing.hits[0]
    values = dict(landing.values)
    values["mDuration"] = max(values["mDuration"], end + 1.)
    # An airborne attack starts its own clock when pressed. Never let the
    # inherited landing cancel release that action before its hit can occur.
    for field in TIMING_FIELDS.values():
        if field.startswith("mCancel"):
            values[field] = max(values[field], end)
    values["mInputFrom"] = min(values["mInputFrom"], begin)
    values["mDodgeInputFrom"] = min(values["mDodgeInputFrom"], begin)
    values["mInvincibleFrom"] = values["mInvincibleTo"] = 0.
    values["mChargeFrom"] = values["mChargeTo"] = 0.
    values["mStartCost"] = values["mDistance"] = 0.
    # The controller preserves this hit's payment/connection across touchdown.
    # Landing root motion starts with the landing phase, never with the air clock.
    hits = [(begin, end, hit[2], cost, hit[4], hit[5], hit[6])]
    return Definition(landing.action, landing.weapon, landing.two_hand, landing.load, values, hits, [],
                      animation_name(airborne), landing.stance, landing, landing_short)


def parse_source(source: str, diagnostics: list[str] | None = None) -> list[Definition]:
    match = re.search(r"\bpub\s+const\s+WEAPONS\b[^=]*=\s*&\[(.*?)\];", source, re.S)
    if not match:
        raise ImportError("missing generated WEAPONS table; run the recreation's tools/extract.py first")
    weapons, stances = [], []
    for literal in re.findall(r"WeaponInfo\s*\{[^{}]*\}", match[1]):
        info = RustLiteral(literal).parse()
        name = info.get("name")
        if not isinstance(name, str) or (name not in WEAPON_CLASSES and name != "Shield"):
            raise ImportError(f"unsupported weapon name {name!r}")
        weapons.append(WEAPON_CLASSES.get(name))
        stance = numeric_tuple(info.get("stance"), 2, "weapon stance categories")
        if any(category < 0 or category > 255 or int(category) != category for category in stance):
            raise ImportError("weapon stance categories must be integers between 0 and 255")
        stances.append(tuple(int(category) for category in stance))
    if not weapons:
        raise ImportError("generated WEAPONS table contains no weapon records")
    definitions, keys = [], set()
    air_attacks = {}
    short_landings = {}
    diagnostics = diagnostics if diagnostics is not None else []
    for line_number, line in enumerate(source.splitlines(), 1):
        if "=>" not in line or ("ActionDef" not in line and "AirAttackDef" not in line):
            continue
        match = re.fullmatch(r"\s*(.*?)\s*=>\s*((?:ActionDef|AirAttackDef)\s*\{.*\})\s*,\s*", line)
        if not match:
            raise ImportError(f"line {line_number}: expected a one-line generated action match arm")
        key, literal = match.groups()
        try:
            fields = RustLiteral(literal).parse()
            if fields["_type"] == "AirAttackDef":
                if key.isdigit():
                    index, grip, action = int(key), False, "PairedJump"
                else:
                    if not key.startswith("(") or not key.endswith(")"):
                        raise ImportError("expected airborne (weapon, two_hand, heavy) or paired weapon index")
                    parts = split_top_level(key[1:-1])
                    if (len(parts) != 3 or not parts[0].isdigit() or parts[1] not in ("true", "false")
                            or parts[2] not in ("true", "false")):
                        raise ImportError("expected airborne (weapon index, two_hand boolean, heavy boolean)")
                    index, grip = int(parts[0]), parts[1] == "true"
                    action = "JumpHeavy" if parts[2] == "true" else "JumpLight"
                if index >= len(weapons):
                    raise ImportError(f"weapon index {index} is outside WEAPONS table")
                if number(fields, "from") >= number(fields, "to"):
                    raise ImportError("airborne hit window must have from < to")
                number(fields, "stamina")
                number(fields, "radius")
                animation_name(fields)
                signature = (action, weapons[index], grip, None)
                if signature in air_attacks:
                    raise ImportError(f"duplicate airborne action {signature}")
                air_attacks[signature] = fields
                continue
            if key.startswith("("):
                if not key.endswith(")"):
                    raise ImportError("invalid attack key")
                parts = split_top_level(key[1:-1])
                if len(parts) != 3 or not parts[0].isdigit() or parts[1] not in ("true", "false"):
                    raise ImportError("expected (weapon index, two_hand boolean, attack kind)")
                index = int(parts[0])
                if index >= len(weapons):
                    raise ImportError(f"weapon index {index} is outside WEAPONS table")
                action = attack_action(parts[2])
                if action is None or weapons[index] is None:
                    continue
                definition = convert(action, fields, weapons[index], parts[1] == "true")
                definition.stance = stances[index]
                if parts[2].removeprefix("AttackKind::").endswith("LandShort"):
                    signature = (definition.action, definition.weapon, definition.two_hand, definition.load)
                    if signature in short_landings:
                        raise ImportError(f"duplicate short landing action {signature}")
                    short_landings[signature] = definition
                    continue
            else:
                mapped = base_action(key)
                if mapped is None:
                    continue
                definition = convert(mapped[0], fields, load=mapped[1])
            signature = (definition.action, definition.weapon, definition.two_hand, definition.load)
            if signature in keys:
                raise ImportError(f"duplicate mapped action {signature}")
            keys.add(signature)
            definitions.append(definition)
        except ImportError as error:
            raise ImportError(f"line {line_number}: {error}") from error
    merged = []
    jump_actions = {"JumpLight", "JumpHeavy", "PairedJump"}
    for definition in definitions:
        if definition.action in jump_actions:
            signature = (definition.action, definition.weapon, definition.two_hand, definition.load)
            airborne = air_attacks.get(signature)
            if airborne is None or not definition.hits:
                missing = "airborne window" if airborne is None else "landing damage metadata"
                diagnostics.append(f"Preserving jump fallback for {definition.weapon} {definition.action} "
                                   f"({'two-handed' if definition.two_hand else 'one-handed'}): missing {missing}.")
                continue
            definition = merge_air_attack(definition, airborne, short_landings.get(signature))
        merged.append(definition)
    for signature in air_attacks.keys() - keys:
        action, weapon, grip, _load = signature
        if weapon is not None:
            diagnostics.append(f"Preserving jump fallback for {weapon} {action} "
                               f"({'two-handed' if grip else 'one-handed'}): missing landing definition.")
    definitions = merged
    if not definitions or not any(item.weapon for item in definitions) or not any(item.weapon is None for item in definitions):
        raise ImportError("source must contain supported base and attack ActionDef entries")
    return definitions


def cpp_float(number: float) -> str:
    result = format(number, ".9g")
    if "." not in result and "e" not in result:
        result += ".0"
    return result + "f"


def condition(definition: Definition) -> str:
    result = [f"action == Action::{definition.action}"]
    if definition.weapon:
        side = "mLeftWeaponClass" if definition.action.startswith("LeftLight") else "mWeaponClass"
        result += [f"config.{side} == WeaponClass::{definition.weapon}",
                   "twoHanded" if definition.two_hand else "!twoHanded"]
    if definition.load:
        result.append(f"config.mLoad == Load::{definition.load}")
    return " && ".join(result)


def generate_header(definitions: list[Definition]) -> str:
    lines = ["// Generated from a local extracted.rs. Keep this game-derived data local.",
             "// Timings: 30-fps animation frames. Displacements: metres.",
             "#ifndef OPENMW_COMPONENTS_ELDENCOMBAT_IMPORTED_TIMINGS_H",
             "#define OPENMW_COMPONENTS_ELDENCOMBAT_IMPORTED_TIMINGS_H", "",
             '#include "combat.hpp"', "#include <algorithm>", "#include <cmath>", "#include <memory>", "",
             "#if defined(_MSC_VER)",
             "#define OPENMW_ELDENCOMBAT_IMPORTED_NOINLINE __declspec(noinline)",
             "#elif defined(__GNUC__) || defined(__clang__)",
             "#define OPENMW_ELDENCOMBAT_IMPORTED_NOINLINE __attribute__((noinline))",
             "#else", "#define OPENMW_ELDENCOMBAT_IMPORTED_NOINLINE", "#endif", "",
             "namespace EldenCombat", "{", "    namespace ImportedTimingsDetail", "    {",
             "        // Keep each action's construction frame separate on compilers with small default stacks."]
    def append_definition(definition: Definition, name: str):
        for suffix, landing in (("Landing", definition.landing), ("LandingShort", definition.landing_short)):
            if landing is not None:
                append_definition(landing, name + suffix)
        parameter = "const Config& config" if definition.weapon else "const Config&"
        lines.extend(["        OPENMW_ELDENCOMBAT_IMPORTED_NOINLINE",
                      f"        inline std::optional<ActionDef> {name}({parameter})",
                      "        {", "            ActionDef result;"])
        lines.append(f"            result.mAnimation = {json.dumps(definition.animation)};")
        for suffix, landing in (("Landing", definition.landing), ("LandingShort", definition.landing_short)):
            if landing is not None:
                lines.append(f"            result.m{suffix} = std::make_shared<ActionDef>(*{name}{suffix}(config));")
        for key, value in definition.values.items():
            lines.append(f"            result.{key} = {cpp_float(value)};")
        for hit_index, hit in enumerate(definition.hits):
            lines.append(f"            result.mHits[{hit_index}] = {{ " + ", ".join(map(cpp_float, hit)) + " };")
        lines.append(f"            result.mHitCount = {len(definition.hits)};")
        if definition.root_motion:
            lines.append("            static constexpr std::array<float, 3> rootMotion[] = {")
            for sample in definition.root_motion:
                lines.append("                { " + ", ".join(map(cpp_float, sample)) + " },")
            lines.extend(["            };",
                          f"            result.mRootMotion.assign(rootMotion, rootMotion + {len(definition.root_motion)});"])
        if definition.weapon:
            lines.extend(["            const float speed = std::isfinite(config.mSpeedMultiplier)",
                          "                ? std::clamp(config.mSpeedMultiplier, 0.1f, 5.0f) : 1.0f;"])
            for key in list(TIMING_FIELDS.values()) + ["mChargeFrom", "mChargeTo", "mInvincibleFrom", "mInvincibleTo"]:
                lines.append(f"            result.{key} /= speed;")
            lines.extend(["            for (std::size_t index = 0; index < result.mHitCount; ++index)", "            {",
                          "                result.mHits[index].mFrom /= speed;", "                result.mHits[index].mTo /= speed;",
                          "            }", "            result.mRootMotionSpeed = speed;",
                          "            result.mAnimationSpeed = speed;"])
        lines.extend(["            return result;", "        }", ""])

    for index, definition in enumerate(definitions):
        append_definition(definition, f"definition{index}")
    lines += ["    }", "",
              "    inline std::optional<ActionDef> importedActionDefinition(Action action, const Config& config, bool twoHanded)",
              "    {"]
    for index, definition in enumerate(definitions):
        lines += [f"        if ({condition(definition)})",
                  f"            return ImportedTimingsDetail::definition{index}(config);"]
    lines += ["        return std::nullopt;", "    }", "",
              "    inline bool importedHasAction(Action action, const Config& config, bool twoHanded)", "    {",
              "        return importedActionDefinition(action, config, twoHanded).has_value();", "    }", "",
              "    inline bool importedHasWeaponMoveset(WeaponClass weapon, bool twoHanded)", "    {"]
    movesets = sorted({(item.weapon, item.two_hand) for item in definitions if item.weapon})
    checks = [f"(weapon == WeaponClass::{weapon} && {'twoHanded' if grip else '!twoHanded'})"
              for weapon, grip in movesets]
    lines.append("        return " + "\n            || ".join(checks) + ";")
    lines += ["    }", "", "    inline unsigned int importedStanceCategory(WeaponClass weapon, bool twoHanded)",
              "    {", "        switch (weapon)", "        {"]
    stances = {item.weapon: item.stance for item in definitions if item.weapon and item.stance is not None}
    for weapon, stance in sorted(stances.items()):
        lines.append(f"            case WeaponClass::{weapon}: return twoHanded ? {stance[1]}u : {stance[0]}u;")
    lines += ["            default: return 0;", "        }", "    }", "}", "",
              "#undef OPENMW_ELDENCOMBAT_IMPORTED_NOINLINE", "", "#endif", ""]
    return "\n".join(lines)


def fixture_literal(hits: int = 0) -> str:
    hit = ("Hit { from: 11.0, to: 13.0, mv: 1.2, guard_damage: 1.5, stamina: 12.0, stop: 0.08, radius: 0.1, "
           "blade: &[[0.1, 1.0, 0.3, 0.2, 1.4, 0.8], [-0.1, 0.9, 0.4, 0.15, 1.2, 0.7]] }")
    timings = ", ".join(f"{key}: {'60.0' if key == 'total' else 'NEVER' if key == 'cancel_left' else '20.0'}"
                        for key in TIMING_FIELDS)
    return ('ActionDef { name: "Fixture", source: "a00_000000", ' + timings
            + ', iframes: (0.0, 13.0), jump_frames: false, stamina: 12.0, hits: &['
            + ", ".join([hit] * hits) + '], charge: None, no_turn: &[], turn: &[], motion: &[[0.0, 0.0, 0.0], [0.0, 0.1, 3.65]] }')


def fixture_air_literal(begin: float = 5., end: float = 9., cost: float = 7.) -> str:
    return (f'AirAttackDef {{ from: {begin}, to: {end}, stamina: {cost}, source: "a02_031030", radius: 0.25, '
            'blade: &[[0.1, 1.0, 0.3, 0.2, 1.4, 0.8], [-0.1, 0.9, 0.4, 0.15, 1.2, 0.7]] }')


class FixtureTests(unittest.TestCase):
    def source(self, attack: str | None = None) -> str:
        return ('pub const WEAPONS: &[WeaponInfo] = &[WeaponInfo { name: "Longsword", category: 2, attack: 100.0, weight: 3.0, stance: [10, 11] }];\n'
                + f"Roll(Load::Medium, Dir::Front) => {fixture_literal()},\n"
                + f"(0, false, Light1) => {attack or fixture_literal(2)},\n")

    def source_with_air_attacks(self) -> str:
        return (self.source()
                + f"(0, false, JumpLightLand) => {fixture_literal(1)},\n"
                + f"(0, true, JumpLightLand) => {fixture_literal(1)},\n"
                + f"(0, false, JumpHeavyLand) => {fixture_literal(1).replace('total: 60.0', 'total: 20.0')},\n"
                + f"(0, false, PairedJumpLand) => {fixture_literal(2)},\n"
                + 'pub fn air_attack(weapon: usize, two_hand: bool, heavy: bool) -> Option<AirAttackDef> {\n'
                + '    Some(match (weapon, two_hand, heavy) {\n'
                + f"        (0, false, false) => {fixture_air_literal()},\n"
                + f"        (0, true, false) => {fixture_air_literal(6., 10., 8.)},\n"
                + f"        (0, false, true) => {fixture_air_literal(8., 25., 17.)},\n"
                + '        _ => return None,\n    })\n}\n'
                + 'pub fn air_paired(weapon: usize) -> Option<AirAttackDef> {\n'
                + f"    Some(match weapon {{\n        0 => {fixture_air_literal(4., 7., 21.)},\n"
                + '        _ => return None,\n    })\n}\n')

    def source_with_short_landings(self) -> str:
        short = (fixture_literal().replace('total: 60.0', 'total: 18.0')
                 .replace('cancel_move: 20.0', 'cancel_move: 8.0'))
        # Put short landings before their full counterparts to cover source order.
        return ("".join(f"(0, false, {action}LandShort) => "
                        + short.replace('a00_000000', clip) + ",\n"
                        for action, clip in (("JumpLight", "a02_031071"), ("JumpHeavy", "a02_031271"),
                                             ("PairedJump", "a02_034571")))
                + self.source_with_air_attacks())

    def test_multihit_and_root_motion(self):
        roll, attack = parse_source(self.source())
        self.assertEqual(attack.weapon, "StraightSword")
        self.assertEqual(len(attack.hits), 2)
        self.assertEqual(attack.values["mCancelLeft"], 9999.)
        self.assertAlmostEqual(attack.hits[0][4], 7.5)
        self.assertAlmostEqual(attack.hits[0][6], 0.04)
        self.assertAlmostEqual(roll.values["mDistance"], 3.65)
        self.assertEqual(roll.root_motion, [(0., 0., 0.), (0., 0.1, 3.65)])
        self.assertEqual(roll.animation, "a000_000000")
        self.assertEqual(attack.stance, (10, 11))
        header = generate_header([roll, attack])
        self.assertIn("result.mHits[1]", header)
        self.assertIn("Load::Medium", header)
        self.assertIn("result.mRootMotion", header)
        self.assertIn("result.mRootMotionSpeed = speed", header)
        self.assertIn("importedHasWeaponMoveset", header)
        self.assertIn('result.mAnimation = "a000_000000";', header)
        self.assertIn("result.mAnimationSpeed = speed;", header)
        self.assertIn("WeaponClass::StraightSword: return twoHanded ? 11u : 10u;", header)

    def test_animation_metadata_is_validated(self):
        for source in (self.source().replace('source: "a00_000000"', 'source: "bad"'),
                       self.source_with_air_attacks().replace('source: "a02_031030"', 'source: "bad"'),
                       self.source_with_short_landings().replace('source: "a02_031071"', 'source: "bad"'),
                       self.source().replace("stance: [10, 11]", "stance: [10, -1]"),
                       self.source().replace("stance: [10, 11]", "stance: [10.5, 11]")):
            with self.subTest(source=source[:140]), self.assertRaises(ImportError):
                parse_source(source)

    def test_front_and_indexed_mapping(self):
        self.assertEqual(base_action("Hurt(HurtLevel::Knockdown, Dir::Front)"), ("Knockdown", None))
        self.assertIsNone(base_action("Roll(Load::Light, Dir::Back)"))
        self.assertEqual(attack_action("Light(6)"), "Light6")
        self.assertEqual(attack_action("Heavy(2, true)"), "Heavy2Charge")

    def test_shield_mapped_for_both_grips_and_jump(self):
        source = self.source_with_air_attacks().replace('name: "Longsword"', 'name: "Shield"')
        source += f"(0, true, Light1) => {fixture_literal(1)},\n"
        definitions = parse_source(source)
        attacks = [item for item in definitions if item.action == "Light1"]
        self.assertEqual({item.two_hand for item in attacks}, {False, True})
        self.assertTrue(all(item.weapon == "Shield" and item.hits for item in attacks))
        jumps = [item for item in definitions if item.action == "JumpLight"]
        self.assertEqual({item.two_hand for item in jumps}, {False, True})
        self.assertTrue(all(item.weapon == "Shield" and item.hits for item in jumps))
        header = generate_header(definitions)
        self.assertIn("WeaponClass::Shield", header)

    def test_maximum_hit_capacity(self):
        definitions = parse_source(self.source(fixture_literal(MAX_HITS)))
        self.assertEqual(len(definitions[1].hits), MAX_HITS)
        self.assertIn("result.mHits[31]", generate_header(definitions))

    def test_airborne_light_heavy_and_paired_attacks(self):
        diagnostics = []
        definitions = parse_source(self.source_with_air_attacks(), diagnostics)
        jumps = {(item.action, item.two_hand): item for item in definitions if item.action in
                 ("JumpLight", "JumpHeavy", "PairedJump")}
        self.assertEqual(len(jumps), 4)
        self.assertTrue(all(jump.animation == "a002_031030" for jump in jumps.values()))
        self.assertFalse(diagnostics)
        for key, window_and_cost in ((('JumpLight', False), (5., 9., 7.)),
                                     (('JumpLight', True), (6., 10., 8.)),
                                     (('JumpHeavy', False), (8., 25., 17.)),
                                     (('PairedJump', False), (4., 7., 21.))):
            with self.subTest(key=key):
                definition = jumps[key]
                self.assertEqual(len(definition.hits), 1)
                hit = definition.hits[0]
                self.assertEqual((hit[0], hit[1], hit[3]), window_and_cost)
                self.assertEqual((hit[2], hit[4], hit[5], hit[6]), (1.2, 7.5, 1.5, 0.04))
                self.assertGreaterEqual(definition.values["mCancelMove"], hit[1])
                self.assertGreater(definition.values["mDuration"], hit[1])
                self.assertEqual(definition.values["mInvincibleTo"], 0.)
                self.assertFalse(definition.root_motion)
                self.assertIsNotNone(definition.landing)
                self.assertEqual(definition.landing.animation, "a000_000000")
                self.assertEqual(definition.landing.values["mInvincibleTo"], 13.)
                self.assertEqual(definition.landing.root_motion, [(0., 0., 0.), (0., 0.1, 3.65)])
                self.assertIsNone(definition.landing_short)
        self.assertEqual(jumps['JumpHeavy', False].values['mDuration'], 26.)
        self.assertEqual(jumps['JumpHeavy', False].landing.values['mDuration'], 20.)
        self.assertEqual(len(jumps['PairedJump', False].landing.hits), 2)

    def test_distinct_landing_phases_preserve_clips_and_recovery(self):
        definitions = parse_source(self.source_with_short_landings())
        jumps = {(item.action, item.two_hand): item for item in definitions if item.landing is not None}
        for action, clip in (("JumpLight", "a002_031071"), ("JumpHeavy", "a002_031271"),
                             ("PairedJump", "a002_034571")):
            with self.subTest(action=action):
                jump = jumps[action, False]
                self.assertEqual(jump.animation, "a002_031030")
                self.assertEqual(jump.landing.animation, "a000_000000")
                self.assertEqual(jump.landing_short.animation, clip)
                self.assertEqual(jump.landing_short.values["mDuration"], 18.)
                self.assertEqual(jump.landing_short.values["mCancelMove"], 8.)
                self.assertEqual(jump.landing_short.root_motion, [(0., 0., 0.), (0., 0.1, 3.65)])
                self.assertFalse(jump.landing_short.hits)
        self.assertIsNone(jumps["JumpLight", True].landing_short)
        header = generate_header([jumps["JumpLight", False]])
        for suffix in ("Landing", "LandingShort"):
            self.assertIn(f"result.m{suffix} = std::make_shared<ActionDef>(*definition0{suffix}(config));", header)
            self.assertLess(header.index(f"definition0{suffix}(const Config& config)"),
                            header.index("definition0(const Config& config)"))
        # Each phase owns a clock and must be scaled once, at the same weapon speed.
        self.assertEqual(header.count("result.mAnimationSpeed = speed;"), 3)
        self.assertEqual(header.count("result.mCancelMove /= speed;"), 3)
        self.assertEqual(header.count("result.mRootMotionSpeed = speed;"), 3)

    def test_duplicate_short_landing_is_rejected(self):
        source = self.source_with_short_landings() + f"(0, false, JumpLightLandShort) => {fixture_literal()},\n"
        with self.assertRaisesRegex(ImportError, "duplicate short landing"):
            parse_source(source)

    def test_missing_airborne_metadata_preserves_fallback(self):
        for source, expected in (
                (self.source() + f"(0, false, JumpLightLand) => {fixture_literal(1)},\n", "missing airborne window"),
                (self.source() + f"(0, false, JumpLightLand) => {fixture_literal()},\n"
                 + f"(0, false, false) => {fixture_air_literal()},\n", "missing landing damage metadata"),
                (self.source() + f"(0, false, false) => {fixture_air_literal()},\n", "missing landing definition")):
            with self.subTest(expected=expected):
                diagnostics = []
                definitions = parse_source(source, diagnostics)
                self.assertFalse(any(item.action == "JumpLight" for item in definitions))
                self.assertTrue(any(expected in message for message in diagnostics))

    def test_malformed_airborne_data(self):
        for source in (self.source_with_air_attacks().replace('from: 5.0, to: 9.0', 'from: 10.0, to: 9.0'),
                       self.source_with_air_attacks().replace('radius: 0.25', 'radius: NaN'),
                       self.source_with_air_attacks().replace('(0, false, false)', '(9, false, false)')):
            with self.subTest(source=source[-50:]), self.assertRaises(ImportError):
                parse_source(source)

    def test_malformed_and_missing_data(self):
        for text in ("", self.source().replace("total: 60.0", "total: NaN"),
                     self.source().replace("to: 13.0", "to: 90.0"),
                     self.source().replace("(0, false, Light1)", "(9, false, Light1)")):
            with self.subTest(text=text[:50]), self.assertRaises(ImportError):
                parse_source(text)

    def test_rejects_expressions_and_overflow(self):
        for literal in (fixture_literal(MAX_HITS + 1), fixture_literal(1).replace("mv: 1.2", "mv: __import__(\"os\")"),
                        fixture_literal(1).replace("total: 60.0", "total: 60.0, total: 70.0"),
                        fixture_literal(1).replace("total: 60.0", "total: 1e100")):
            with self.subTest(literal=literal[:50]), self.assertRaises(ImportError):
                parse_source(self.source(literal))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", nargs="?", type=Path, help="locally generated src/sim/extracted.rs")
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "components/eldencombat/imported_timings.hpp")
    parser.add_argument("--self-test", action="store_true", help="run safe literal parsing and conversion fixtures")
    arguments = parser.parse_args()
    if arguments.self_test:
        return 0 if unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(FixtureTests)).wasSuccessful() else 1
    if arguments.source is None:
        parser.error("source is required: run tools/extract.py against your local Elden Ring files first")
    try:
        diagnostics = []
        definitions = parse_source(arguments.source.read_text(encoding="utf-8"), diagnostics)
        header = generate_header(definitions)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(header, encoding="utf-8")
    except (OSError, ImportError) as error:
        print(f"Cannot import Elden Ring timings: {error}", file=sys.stderr)
        return 1
    print(f"Imported {len(definitions)} actions to {arguments.output}. Keep this generated file local.")
    for diagnostic in diagnostics:
        print(diagnostic, file=sys.stderr)
    print("Configure OpenMW again to enable the imported action table.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
