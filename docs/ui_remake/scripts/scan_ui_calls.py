#!/usr/bin/env python3
"""Extract UI call sites for manual inventory review; not a C++ semantic parser."""
import argparse
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CALLS = {
    'toggle_row', 'toggle_color_row', 'toggle_color_popup_row', 'toggle_color2_row',
    'toggle_color2_popup_row', 'toggle_popup_row', 'slider_row', 'select_row',
    'multi_select_row', 'button_row', 'text_input_row', 'keybind_row', 'color_row',
    'settings_popup_row', 'color_swatch', 'color_swatch_inline', 'color_swatch_labeled', 'settings_popup',
    'subtabs', 'bar_settings_popup', 'player_weapon_popup', 'player_flags_popup',
    'material_popup', 'small_button', 'pill_button', 'hint_input', 'action_menu',
    'Checkbox', 'SliderInt', 'SliderFloat', 'Combo', 'Selectable', 'Button',
    'InvisibleButton', 'InputText', 'BeginPopupContextItem', 'MenuItem',
}
PATTERN = re.compile(r'\b(' + '|'.join(sorted(CALLS)) + r')\s*\(')


def masked(source):
    # Keep offsets/newlines stable while removing comments and string interiors.
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), source)


def scan():
    for path in sorted((ROOT / 'src/ui/menu').glob('*')):
        if path.suffix not in ('.hpp', '.cpp'):
            continue
        source = path.read_text(encoding='utf-8')
        clean = masked(source)
        for match in PATTERN.finditer(clean):
            start = match.end()
            depth, end = 1, start
            while end < len(clean) and depth:
                depth += (clean[end] == '(') - (clean[end] == ')')
                end += 1
            if depth:
                raise ValueError(f'Unbalanced call in {path}:{start}')
            # Exclude ordinary function definitions/prototypes, retain actual call sites.
            before = clean[clean.rfind('\n', 0, match.start()) + 1:match.start()].strip()
            if before in ('void', 'bool', 'inline void', 'inline bool', 'static void', 'static bool'):
                continue
            arguments = source[start:end - 1].strip()
            literal = re.match(r'"((?:\\.|[^"\\])*)"', arguments)
            yield {
                'source_file': str(path.relative_to(ROOT)),
                'source_line': source.count('\n', 0, match.start()) + 1,
                'function': match[1],
                'label_literal': literal[1] if literal else '',
                'arguments': re.sub(r'\s+', ' ', arguments),
            }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/ui_remake/source_calls.csv')
    args = parser.parse_args()
    rows = list(scan())
    with args.output.open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=['source_file', 'source_line', 'function', 'label_literal', 'arguments'], lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    print(f'{len(rows)} syntactic call sites; expand shared helpers and dynamic arrays in controls.csv.')


if __name__ == '__main__':
    main()
