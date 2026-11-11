#!/usr/bin/env python3
"""Extract saved JSON leaf paths from the config serializers and settings declarations."""
from __future__ import annotations
import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SETTINGS = ROOT / "src/config/settings.cpp"
HEADERS = sorted((ROOT / "src/config").glob("*.hpp"))
MENU = ROOT / "src/ui/menu"
OUTPUT = ROOT / "docs/ui_remake/config_keys.csv"
ENUM_VALUES: dict[str, set[int]] = {}

# Shared combat rows receive setting references rather than binding widgets directly in the page.
COMBAT_ROW_BINDINGS = {
    "master_row": [(0, "", "Master Switch"), (1, "", "Mode")],
    "checks_row": [(0, ".airborne", "Checks"), (0, ".smoke", "Checks"), (0, ".flashed", "Checks")],
    "visibility_row": [(0, ".walls", "Only Visible")],
    "aim_parts_row": [(0, "", "Hitboxes")],
    "trigger_timing_row": [(0, "", "Delay Before"), (1, "", "Randomize"),
                            (2, "", "Outlier Chance"), (3, "", "Outlier Delay"),
                            (4, "", "Delay After")],
    "prediction_row": [(0, ".enabled", "Prediction"), (0, ".max_horizon_ms", "Max Horizon"),
                        (0, ".acceleration", "Acceleration")],
    "damage_override_row": [(0, "", "Damage Override"), (1, "", "Override Damage"),
                             (2, "", "Mode"), (3, "", "Key")],
    "fov_row": [(0, "", "Radius"), (1, ".selection", "Mode"), (1, ".near_fov", "Max Radius"),
                (1, ".target_near_fov", "Max Radius"), (1, ".near_distance_m", "Full Size At"),
                (1, ".target_near_distance_m", "Full Size At"), (1, ".far_distance_m", "Min Size At"),
                (1, ".target_far_distance_m", "Min Size At"), (1, ".far_fov", "Min Radius"),
                (1, ".target_far_fov", "Min Radius"), (1, ".distance_curve", "Falloff"),
                (1, ".target_distance_curve", "Falloff"), (2, "", "Visualization"), (3, "", "Visualization")],
    "humanizer_row": [(0, "", "Amount"), (1, "", "Smoothing"),
                       (2, ".gravity", "Gravity"), (2, ".wind", "Wind"), (2, ".max_step", "Max Step"),
                       (2, ".damping", "Damping"), (2, ".reaction_min_ms", "Reaction Min"),
                       (2, ".reaction_max_ms", "Reaction Max"), (2, ".curve", "Curve"),
                       (2, ".overshoot_chance", "Overshoot Chance"),
                       (2, ".overshoot_amount", "Overshoot Amount"), (2, ".jitter", "Jitter"),
                       (2, ".deadzone", "Deadzone")],
    "multipoint_row": [(0, "", "Multi-Point"), (1, ".performance", "Performance"),
                       (1, ".caps", "Cap Points"), (1, ".sides", "Side Points"),
                       (1, ".display", "Display Multipoints"), (1, ".head_scale", "Head Scale"),
                       (1, ".body_scale", "Body Scale"), (1, ".limb_scale", "Limbs Scale")],
    "rcs_row": [(0, ".enabled", "RCS"), (0, ".start_bullet", "Start Bullet"),
                 (0, ".pitch", "Pitch Strength"), (0, ".yaw", "Yaw Strength"),
                 (0, ".response_ms", "Correction Time")],
    "chams_material_rows": [(0, ".type", "Material"), (0, ".color", "Color"),
                            (0, ".wireframe", "Wireframe"), (0, ".roughness", "Roughness"),
                            (0, ".metalness", "Metalness"), (0, ".exponent", "Exponent"),
                            (0, ".falloff", "Falloff"), (0, ".fresnel_fill", "Fresnel Filling"),
                            (0, ".strength", "Strength"), (0, ".speed", "Speed"),
                            (0, ".tint", "Tint")],
}

@dataclass
class Member:
    name: str
    type: str
    initializer: str

@dataclass
class Serializer:
    type_name: str
    variable: str
    body: str

def matching_brace(source: str, opening: int) -> int:
    depth = 0
    quote = None
    escape = False
    for index in range(opening, len(source)):
        char = source[index]
        if quote:
            if escape: escape = False
            elif char == "\\": escape = True
            elif char == quote: quote = None
        elif char in ("\"", "'"): quote = char
        elif char == "{": depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0: return index
    raise ValueError(f"unclosed brace at byte {opening}")

def remove_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//[^\n]*", "", source)

def extract_serializers(source: str) -> dict[str, Serializer]:
    result = {}
    pattern = re.compile(r"void to_json\(json& j, const ([\w:]+)& (\w+)\)\s*\{")
    for match in pattern.finditer(source):
        opening = source.find("{", match.start())
        end = matching_brace(source, opening)
        result[match.group(1)] = Serializer(match.group(1), match.group(2), source[opening + 1:end])
    return result

def parse_pair_group(group: str) -> tuple[str, str] | None:
    match = re.match(r'\s*"([^"\n]+)"\s*,', group)
    if not match:
        return None
    return match.group(1), group[match.end():].strip()

def object_pairs(text: str) -> list[tuple[str, str]]:
    """Read only direct key/value pairs from the first json{{...}} initializer."""
    marker = re.search(r"(?:nlohmann::)?json\s*\{", text)
    if marker:
        opening = text.find("{", marker.start())
    elif text.lstrip().startswith("{"):
        opening = text.find("{")
    else:
        return []
    end = matching_brace(text, opening)
    content = text[opening + 1:end]
    pairs = []
    index = 0
    while index < len(content):
        if content[index] != "{":
            index += 1
            continue
        close = matching_brace(content, index)
        pair = parse_pair_group(content[index + 1:close])
        if pair:
            pairs.append(pair)
        index = close + 1
    return pairs

def serialized_pairs(serializer: Serializer) -> list[tuple[str, str]]:
    pairs = object_pairs(serializer.body)
    if not pairs:
        pairs = [pair for match in re.finditer(r'\{"([^"\n]+)"\s*,\s*([^,}]+)', serializer.body)
                 if (pair := (match.group(1), match.group(2).strip()))]
    present = {key for key, _ in pairs}
    for match in re.finditer(r'j\["([^"\n]+)"\]\s*=\s*([^;]+);', serializer.body):
        if match.group(1) not in present:
            pairs.append((match.group(1), match.group(2).strip()))
            present.add(match.group(1))
    return pairs

def find_structs(paths: list[Path]) -> dict[str, dict[str, Member]]:
    result = {}
    instances = {}
    start_struct = re.compile(r"\bstruct\s+(\w+)[^{;]*\{")
    field = re.compile(r"^\s*(.+?)\s+([A-Za-z_]\w*)\s*(?:\{(.*)\}|=\s*(.*))?\s*;\s*$")
    for path in paths:
        source = remove_comments(path.read_text(encoding="utf-8"))
        for match in start_struct.finditer(source):
            opening = source.find("{", match.start())
            end = matching_brace(source, opening)
            suffix = source[end + 1:end + 160]
            instance = re.match(r"\s*([A-Za-z_]\w*)\s*(?:\{[^;]*\})?\s*;", suffix, flags=re.S)
            if instance:
                instances[instance.group(1)] = match.group(1)
        for match in start_struct.finditer(source):
            opening = source.find("{", match.start())
            end = matching_brace(source, opening)
            body = source[opening + 1:end]
            depth = 1
            members = {}
            pending = ""
            for line in body.splitlines():
                struct_match = re.search(r"\bstruct\s+(\w+)", line)
                if pending:
                    pending += " " + line.strip()
                    if ";" in line:
                        parsed = field.match(pending.strip())
                        if parsed and "(" not in parsed.group(1):
                            initializer = parsed.group(3) if parsed.group(3) is not None else parsed.group(4)
                            members[parsed.group(2)] = Member(parsed.group(2), parsed.group(1).strip(), (initializer or "{}").strip())
                        pending = ""
                elif depth == 1 and ";" in line and not line.strip().startswith(("enum ", "struct ", "class ")):
                    parsed = field.match(line.strip())
                    if parsed and "(" not in parsed.group(1):
                        initializer = parsed.group(3) if parsed.group(3) is not None else parsed.group(4)
                        members[parsed.group(2)] = Member(parsed.group(2), parsed.group(1).strip(), (initializer or "{}").strip())
                elif depth == 1 and re.match(r"(?:[\w:<>,*&]+\s+)+[A-Za-z_]\w*\s*(?:\{|=|;)", line.strip()) and "(" not in line:
                    pending = line.strip()
                nested = re.match(r"^\s*}\s*(\w+)\s*(?:\{(.*)\})?\s*;", line)
                if nested and depth == 2 and nested.group(1) in instances:
                    members[nested.group(1)] = Member(nested.group(1), instances[nested.group(1)], (nested.group(2) or "{}").strip())
                depth += line.count("{") - line.count("}")
            for name, member in members.items():
                if name in instances:
                    member.type = instances[name]
            result[match.group(1)] = members
    return result

def norm_type(value: str) -> str:
    return " ".join(re.sub(r"\b(const|volatile)\b|[&*]", "", value).split()).strip()

def array_type(value: str) -> str | None:
    match = re.fullmatch(r"(?:std::)?(?:array|vector)\s*<\s*(.+?)\s*(?:,\s*[^<>]+)?\s*>", norm_type(value))
    return match.group(1).strip() if match else None

def split_values(value: str) -> list[str]:
    value = value.strip()
    if value.startswith("{") and value.endswith("}"): value = value[1:-1]
    values, depth, start = [], 0, 0
    for i, char in enumerate(value):
        if char in "([{": depth += 1
        elif char in ")]}": depth -= 1
        elif char == "," and depth == 0:
            if value[start:i].strip(): values.append(value[start:i].strip())
            start = i + 1
    if value[start:].strip(): values.append(value[start:].strip())
    return values

def default_type(value: str) -> str:
    value = norm_type(value)
    if array_type(value): return "[]"
    if value == "bool": return "false"
    if value in ("std::string", "string"): return '""'
    if value in ("float", "double"): return "0.0"
    if re.search(r"\b(char|int|short|long|uint\w*_t|int\w*_t|size_t)\b", value): return "0"
    if value.endswith("rgba"): return "(0, 0, 0, 0)"
    return "{}"

def enum_default(value: str) -> str:
    value = re.sub(r"^static_cast<int>\((.*)\)$", r"\1", value.strip())
    if re.fullmatch(r"'[^']'", value):
        return str(ord(value[1]))
    windows_keys = {"VK_LBUTTON": 1, "VK_RBUTTON": 2, "VK_SHIFT": 0x10, "VK_CONTROL": 0x11,
                    "VK_SPACE": 0x20, "VK_F8": 0x77}
    if value in windows_keys:
        return str(windows_keys[value])
    candidate = value
    qualified = re.findall(r"(?:[A-Za-z_]\w*::)+[A-Za-z_]\w*", candidate)
    for token in sorted(qualified, key=len, reverse=True):
        suffix = "::".join(token.split("::")[-2:])
        matches = ENUM_VALUES.get(token) or ENUM_VALUES.get(suffix)
        if matches and len(matches) == 1:
            candidate = candidate.replace(token, str(next(iter(matches))))
    for token, values in sorted(ENUM_VALUES.items(), key=lambda entry: -len(entry[0])):
        if "::" in token: continue
        if len(values) == 1:
            candidate = re.sub(rf"\b{re.escape(token)}\b", str(next(iter(values))), candidate)
    candidate = re.sub(r"(?<=\d)[uUlL]+\b", "", candidate)
    if re.fullmatch(r"[0-9xXa-fA-F\s|&~<>()+\-*/]+", candidate):
        try: return str(int(eval(candidate, {"__builtins__": {}}, {})))
        except (SyntaxError, TypeError, ValueError, ZeroDivisionError): pass
    exact = ENUM_VALUES.get(value)
    if exact and len(exact) == 1:
        return str(next(iter(exact)))
    terminal = value.split("::")[-1]
    matches = ENUM_VALUES.get(terminal)
    if matches and len(matches) == 1:
        return str(next(iter(matches)))
    return value

def field_default(member: Member | None, value_type: str) -> str:
    if member is None: return default_type(value_type)
    init = member.initializer.strip()
    if init in ("{}", ""): return default_type(member.type)
    if array_type(member.type): return "[]" if init == "{}" else "[" + ", ".join(split_values(init)) + "]"
    if member.type.endswith("rgba") and "," in init: return "(" + ", ".join(split_values(init)) + ")"
    if init.startswith("{") and init.endswith("}"): return "(" + ", ".join(split_values(init)) + ")"
    return enum_default(re.sub(r"\s+", " ", init).strip())

def collect_enums(paths: list[Path]) -> dict[str, set[int]]:
    result: dict[str, set[int]] = {}
    pattern = re.compile(r"enum\s+(?:class\s+)?(\w+)?\s*(?::\s*[\w:]+)?\s*\{")
    for path in paths:
        source = remove_comments(path.read_text(encoding="utf-8"))
        for match in pattern.finditer(source):
            opening = source.find("{", match.start())
            end = matching_brace(source, opening)
            enum_name = match.group(1) or ""
            next_value = 0
            local: dict[str, int] = {}
            for item in split_values(source[opening + 1:end]):
                pair = re.match(r"\s*(\w+)\s*(?:=\s*(.*))?$", item, flags=re.S)
                if not pair: continue
                name, expression = pair.group(1), pair.group(2)
                value = next_value
                if expression:
                    expression = expression.strip()
                    candidate = expression
                    for token, numeric in sorted(local.items(), key=lambda entry: -len(entry[0])):
                        candidate = re.sub(rf"\b{re.escape(token)}\b", str(numeric), candidate)
                    candidate = re.sub(r"(?<=\d)[uUlL]+\b", "", candidate)
                    if re.fullmatch(r"[0-9xXa-fA-F\s|&~<>()+\-*/]+", candidate):
                        try: value = int(eval(candidate, {"__builtins__": {}}, {}))
                        except (SyntaxError, TypeError, ValueError, ZeroDivisionError): pass
                local[name] = value
                result.setdefault(name, set()).add(value)
                if enum_name: result.setdefault(enum_name + "::" + name, set()).add(value)
                next_value = value + 1
    result.setdefault("activation::always", {0})
    result.setdefault("activation::hold", {1})
    result.setdefault("activation::toggle", {2})
    result.setdefault("wall_policy::block", {0})
    result.setdefault("wall_policy::penetration", {1})
    result.setdefault("wall_policy::ignore", {2})
    return result

def add_static_constants(structs: dict[str, dict[str, Member]]) -> None:
    for type_name, members in structs.items():
        for name, member in members.items():
            if "static constexpr" not in member.type:
                continue
            value = enum_default(member.initializer)
            if re.fullmatch(r"-?\d+", value):
                numeric = int(value)
                ENUM_VALUES.setdefault(name, set()).add(numeric)
                ENUM_VALUES.setdefault(type_name + "::" + name, set()).add(numeric)

def serializer_for(value: str, serializers: dict[str, Serializer], context: str | None = None) -> Serializer | None:
    value = norm_type(value)
    if value.endswith("rgba"):
        return Serializer("foundation::rgba", "color", '{"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a}')
    if value in serializers: return serializers[value]
    if context and "::" not in value:
        scope = context.split("::")
        while scope:
            candidate = "::".join(scope + [value])
            if candidate in serializers: return serializers[candidate]
            scope.pop()
    matches = [item for key, item in serializers.items() if key.split("::")[-1] == value.split("::")[-1]]
    return matches[0] if len(matches) == 1 else None

def member_from_expr(expression: str, variable: str) -> str | None:
    match = re.search(rf"\b{re.escape(variable)}\.([A-Za-z_]\w*)\b", expression)
    return match.group(1) if match else None

def menu_widgets(menu: Path) -> dict[str, str]:
    """Map saved paths to labels from actual old-menu widget argument bindings."""
    result: dict[str, list[str]] = {}
    widget_names = r"toggle_row|toggle_color_row|toggle_popup_row|slider_row|select_row|color_row|keybind_row|text_input_row|visibility_row|checks_row|aim_parts_row|multipoint_row|ImGui::Checkbox|ImGui::Slider[A-Za-z]*|ImGui::Combo|ImGui::ColorEdit[A-Za-z]*|ImGui::Input[A-Za-z]*"
    call_pattern = re.compile(rf"\b({widget_names})\s*\(")
    alias_pattern = re.compile(r"auto\s*&\s*(\w+)\s*=\s*([^;]+);")
    reference = re.compile(r"\b([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)+)")
    def close_paren(text: str, opening: int) -> int:
        depth, quote, escape = 0, False, False
        for i in range(opening, len(text)):
            c = text[i]
            if quote:
                if escape: escape = False
                elif c == "\\": escape = True
                elif c == '"': quote = False
            elif c == '"': quote = True
            elif c == "(": depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0: return i
        return min(len(text), opening + 1000)
    def saved_path(expression: str, aliases: dict[str, str]) -> str | None:
        expression = re.sub(r"\s+", "", expression)
        expression = re.sub(r"\.data(?:\(\))?$", "", expression)
        expression = expression.replace("config::visual_settings", "esp").replace("config::general_settings", "misc")
        expression = expression.replace("config::combat_settings.global", "combat_global")
        expression = re.sub(r"config::combat_settings.overrides\[[^]]+\]", "combat_overrides[*]", expression)
        head = expression.split(".", 1)[0]
        if head in aliases:
            expression = aliases[head] + expression[len(head):]
        expression = re.sub(r"\[[^]]*\]", "[*]", expression)
        expression = expression.replace("misc.m_walk_bot.mode", "misc.m_walk_bot.route")
        if expression in ("esp", "misc", "combat_global", "combat_overrides[*]"):
            return expression
        if expression.startswith(("esp.", "misc.", "combat_global.")):
            return expression
        if expression.startswith("combat_overrides[*]."):
            return expression
        return None
    for path in menu.rglob("*"):
        if path.suffix not in (".cpp", ".hpp"): continue
        text = remove_comments(path.read_text(encoding="utf-8", errors="replace"))
        functions = list(re.finditer(r"\b(?:void|bool|int|float|auto)\s+[\w:]+\s*\([^;{}]*\)\s*\{", text))
        for function in functions:
            opening = text.find("{", function.start())
            end = matching_brace(text, opening)
            body = text[opening + 1:end]
            aliases: dict[str, str] = {}
            for _ in range(4):
                changed = False
                for match in alias_pattern.finditer(body):
                    name, expression = match.group(1), match.group(2).strip()
                    value = saved_path(expression, aliases)
                    if value and aliases.get(name) != value:
                        aliases[name] = value
                        changed = True
                if not changed: break
            for call in call_pattern.finditer(body):
                opening_call = body.find("(", call.start())
                call_end = close_paren(body, opening_call)
                arguments = body[opening_call + 1:call_end]
                arguments = re.split(r",\s*\[(?:&|=|\])", arguments, maxsplit=1)[0]
                literals = re.findall(r'"([^"\n]*)"', arguments)
                label = next((value for value in literals if value and not value.startswith("##")), "")
                if not label: continue
                for ref in reference.findall(arguments):
                    resolved = saved_path(ref, aliases)
                    if resolved:
                        result.setdefault(resolved, [])
                        if label not in result[resolved]: result[resolved].append(label)
            helper_pattern = re.compile(r"\b(" + "|".join(map(re.escape, COMBAT_ROW_BINDINGS)) + r")\s*\(")
            for call in helper_pattern.finditer(body):
                opening_call = body.find("(", call.start())
                call_end = close_paren(body, opening_call)
                args = split_values(body[opening_call + 1:call_end])
                for index, suffix, label in COMBAT_ROW_BINDINGS[call.group(1)]:
                    if index >= len(args): continue
                    resolved = saved_path(args[index], aliases)
                    if resolved:
                        resolved = re.sub(r"\[[^]]*\]", "[*]", resolved)
                        resolved += suffix
                        result.setdefault(resolved, [])
                        if label not in result[resolved]: result[resolved].append(label)
    return {path: "; ".join(labels) for path, labels in result.items()}

def build_rows(serializers: dict[str, Serializer], structs: dict[str, dict[str, Member]], controls: dict[str, str]) -> list[dict[str, str]]:
    roots = [("combat_global", "combat_profile::global_settings"), ("combat_overrides[*]", "combat_profile::group_overrides"), ("esp", "visual_profile"), ("misc", "general_profile")]
    rows = []
    def add(path: str, member_name: str | None, field_type: str, default: str, inherited: tuple[str, str] | None = None) -> None:
        label = controls.get(path, "")
        if not label:
            ancestors = [key for key in controls if path.startswith(key + ".")]
            if ancestors:
                label = controls[max(ancestors, key=len)]
        found = bool(label)
        if inherited and not found:
            found, label = inherited[0] == "yes", inherited[1]
        rows.append({"key": path, "type": "boolean" if field_type == "bool" else "string" if "string" in field_type else "number",
                     "default": default, "has_menu_control": "yes" if found else "no", "control_label": label if found else ""})
    def visit(type_name: str, prefix: str, override: str | None = None, stack: tuple[str, ...] = ()) -> None:
        serializer = serializer_for(type_name, serializers)
        if not serializer: raise ValueError(f"No serializer for {type_name} at {prefix}")
        if type_name in stack: raise ValueError(f"Serializer recursion at {prefix}: {type_name}")
        fields = structs.get(type_name.split("::")[-1], {})
        overrides = split_values(override[1:-1]) if override and override.startswith("(") and override.endswith(")") else []
        for number, (key, expression) in enumerate(serialized_pairs(serializer)):
            member_name = member_from_expr(expression, serializer.variable)
            inline = object_pairs(expression)
            chain_match = re.search(rf"\b{re.escape(serializer.variable)}((?:\.[A-Za-z_]\w*)+)", expression)
            chain = chain_match.group(1).strip(".").split(".") if chain_match else []
            member = fields.get(chain[0]) if chain else None
            if not inline:
                for part in chain[1:]:
                    nested_fields = structs.get(norm_type(member.type).split("::")[-1], {}) if member else {}
                    member = nested_fields.get(part)
            if member_name is None and key in fields:
                member_name, member = key, fields[key]
            field_type = norm_type(member.type) if member else "number"
            path = prefix + "." + key
            nested_serializer = serializer_for(field_type, serializers, type_name)
            if nested_serializer:
                field_type = nested_serializer.type_name
            default = field_default(member, field_type)
            if key == "buy_presets" and "buy_presets_json" in expression:
                preset = structs.get("buy_preset", {})
                item = structs.get("buy_item", {})
                for field_name in ("custom", "random_one"):
                    member_def = preset.get(field_name)
                    add(path + "[*]." + field_name, field_name, member_def.type, field_default(member_def, member_def.type))
                for field_name in ("enabled", "code", "side", "money_rule", "money"):
                    member_def = item.get(field_name)
                    field_value_type = "std::string" if field_name == "code" else member_def.type
                    field_value_default = '""' if field_name == "code" else field_default(member_def, field_value_type)
                    add(path + "[*].items[*]." + field_name, field_name, field_value_type, field_value_default)
                continue
            if key == "version" and expression.strip() == "1":
                add(path, key, "int", "1")
                continue
            inherited = None
            nested_default = overrides[number] if number < len(overrides) else None
            if inline:
                visit_inline(field_type, path, inline, member, structs, serializers, controls, rows, add, stack + (type_name,))
                continue
            nested = nested_serializer
            element = array_type(field_type)
            if nested:
                child_override = nested_default or (default if default.startswith("(") else None)
                if field_type.endswith("rgba"):
                    child_override = default if default.startswith("(") else None
                visit(field_type, path, child_override, stack + (type_name,))
            elif element:
                element = norm_type(element)
                element_serializer = serializer_for(element, serializers, type_name)
                arr_path = path + "[*]"
                if element_serializer:
                    visit(element_serializer.type_name, arr_path, None, stack + (type_name,))
                else:
                    add(arr_path, member_name, element, default_type(element), inherited)
            else:
                if nested_default is not None: default = nested_default
                add(path, member_name, field_type, default, inherited)
    def visit_inline(type_name: str, prefix: str, pairs: list[tuple[str, str]], member: Member | None,
                     structs: dict[str, dict[str, Member]], serializers: dict[str, Serializer], widgets: dict[str, str],
                     rows: list[dict[str, str]], add, stack: tuple[str, ...]) -> None:
        type_name = norm_type(type_name)
        fields = structs.get(type_name.split("::")[-1], {})
        parent_name = member.name if member else None
        inherited = None
        for key, expression in pairs:
            path = prefix + "." + key
            # Inline JSON expressions retain the base serializer variable; identify the final member token.
            tokens = re.findall(r"\.([A-Za-z_]\w*)", expression)
            child_name = tokens[-1] if tokens else None
            child = fields.get(child_name or "")
            child_type = norm_type(child.type) if child else "number"
            child_default = field_default(child, child_type)
            nested_inline = object_pairs(expression)
            if nested_inline:
                visit_inline(child_type, path, nested_inline, child, structs, serializers, controls, rows, add, stack)
            elif serializer_for(child_type, serializers, type_name):
                # Nested serializers such as rgba are expanded into their actual JSON leaves.
                ser = serializer_for(child_type, serializers, type_name)
                if child_type.endswith("rgba"):
                    component_defaults = split_values(child_default[1:-1]) if child_default.startswith("(") else []
                    for index, (component, component_expr) in enumerate(serialized_pairs(ser)):
                        channel = re.search(r"\.([A-Za-z_]\w*)$", component_expr)
                        channel_name = channel.group(1) if channel else component
                        value = component_defaults[index] if index < len(component_defaults) else "0"
                        add(path + "." + component, channel_name, "int", value, inherited)
                else:
                    visit(ser.type_name, path, child_default if child_default.startswith("(") else None, stack)
            elif array_type(child_type):
                element = norm_type(array_type(child_type))
                add(path + "[*]", child_name, element, default_type(element), inherited)
            else:
                add(path, child_name, child_type, child_default, inherited)
    for prefix, type_name in roots:
        visit(type_name, prefix)
    rows.sort(key=lambda row: row["key"])
    keys = [row["key"] for row in rows]
    if len(keys) != len(set(keys)): raise ValueError("Duplicate paths: " + ", ".join(sorted(k for k in set(keys) if keys.count(k) > 1)))
    return rows

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    global ENUM_VALUES
    ENUM_VALUES = collect_enums(HEADERS + [SETTINGS])
    serializers = extract_serializers(SETTINGS.read_text(encoding="utf-8"))
    structs = find_structs(HEADERS)
    add_static_constants(structs)
    widgets = menu_widgets(MENU)
    rows = build_rows(serializers, structs, widgets)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=["key", "type", "default", "has_menu_control", "control_label"])
        writer.writeheader()
        writer.writerows(rows)
    print(f"Wrote {len(rows)} serialized leaf paths to {args.output}")
    for root in ("combat_global", "combat_overrides", "esp", "misc"):
        print(f"{root}: {sum(row['key'].startswith(root) for row in rows)}")
    print(f"old UI heuristic: {sum(row['has_menu_control'] == 'yes' for row in rows)} yes, {sum(row['has_menu_control'] == 'no' for row in rows)} no")

if __name__ == "__main__": main()
