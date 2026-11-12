#!/usr/bin/env python3
"""Merge reviewed control fragments and rebuild the OLD-menu saved-key crosswalk."""
import csv
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DOC = ROOT / 'docs/ui_remake'
FIELDS = ['page', 'section', 'label', 'widget_type', 'config_key', 'default', 'min', 'max',
          'condition', 'source_file', 'source_line']
KEY_FIELDS = ['key', 'type', 'default', 'has_menu_control', 'control_label']


def read(path):
    with path.open(newline='', encoding='utf-8-sig') as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        if any(None in row for row in rows):
            raise ValueError(f'Unexpected extra columns: {path}')
        return reader.fieldnames, rows


def write(path, fields, rows):
    with path.open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def normalize(key):
    return re.sub(r'\[(?:\d+|\d+\.\.\d+|\*|i|index)\]', '[*]', key.strip().rstrip('.'))


def covers(binding, key):
    binding, key = normalize(binding), normalize(key)
    return key == binding or key.startswith(binding + '.') or key.startswith(binding + '[')


def bindings(row):
    return [part.strip() for part in row['config_key'].split(';') if part.strip()]


def proposed(row):
    return 'new control (no old UI)' in (row['condition'] + row['widget_type'])


def validate_locations(row):
    files = row['source_file'].split(';')
    locations = row['source_line'].split(';')
    if len(files) > 1 and len(files) != len(locations):
        raise ValueError(f'Mismatched source locations: {row}')
    for index, location in enumerate(locations):
        file = ROOT / files[index if len(files) > 1 else 0].strip()
        location = location.strip()
        annotated = re.fullmatch(r'(?:caller|helper) (.+):(\d+(?:-\d+)?)', location)
        if annotated:
            name, location = annotated.groups()
            file = ROOT / name if '/' in name else file.parent / name
        span = re.fullmatch(r'(\d+)(?:-(\d+))?', location)
        if not file.is_file() or not span:
            raise ValueError(f'Invalid original control location: {row}')
        start, end = int(span[1]), int(span[2] or span[1])
        if not 1 <= start <= end <= len(file.read_text().splitlines()):
            raise ValueError(f'Invalid original control line range: {row}')


def main():
    controls = []
    for path in sorted((DOC / 'parts').glob('*_controls.csv')):
        fields, rows = read(path)
        if fields != FIELDS:
            raise ValueError(f'Wrong columns in {path}: {fields}')
        controls.extend(rows)
    if not controls:
        raise ValueError('No reviewed control fragments')
    for row in controls:
        if not all(row[field] for field in ('page', 'section', 'label', 'widget_type', 'config_key')):
            raise ValueError(f'Missing control identity/binding: {row}')
        if not proposed(row):
            validate_locations(row)
    identities = [tuple(row[field] for field in FIELDS) for row in controls]
    if len(identities) != len(set(identities)):
        raise ValueError('Duplicate exact control rows')
    fields, keys = read(DOC / 'config_keys.csv')
    if fields != KEY_FIELDS:
        raise ValueError(f'Wrong saved-key columns: {fields}')
    if len(keys) != len({row['key'] for row in keys}):
        raise ValueError('Duplicate serialized key paths')
    for key in keys:
        labels = []
        for row in controls:
            if proposed(row):
                continue
            if any(covers(binding, key['key']) for binding in bindings(row)):
                labels.append(f"{row['page']} / {row['section']} / {row['label']}")
        key['has_menu_control'] = 'yes' if labels else 'no'
        key['control_label'] = ' | '.join(dict.fromkeys(labels))
    external_fields, external = read(DOC / 'external_settings.csv')
    external_keys = {row['key'] for row in external}
    if len(external_keys) != len(external):
        raise ValueError('Duplicate external setting paths')
    missing_bindings = []
    for row in controls:
        for binding in bindings(row):
            if binding.startswith('@') or binding in external_keys:
                continue
            if not any(covers(binding, key['key']) for key in keys):
                missing_bindings.append(f"{row['page']} / {row['label']}: {binding}")
    print('Old control rows:', sum(not proposed(row) for row in controls))
    print('Proposed new-control rows:', sum(proposed(row) for row in controls))
    print('Counts by page:', dict(sorted(Counter(row['page'] for row in controls).items())))
    print('Saved leaf-key rows:', len(keys))
    print('Saved leaf keys with old UI:', sum(row['has_menu_control'] == 'yes' for row in keys))
    if missing_bindings:
        print('Unmatched bindings requiring manual review:')
        print('\n'.join(missing_bindings))
        raise SystemExit(1)
    write(DOC / 'controls.csv', FIELDS, controls)
    write(DOC / 'config_keys.csv', KEY_FIELDS, keys)
    no_controls = [row['key'] for row in keys if row['has_menu_control'] == 'no']
    report = DOC / 'schema/no_menu_controls.md'
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(
        '# Saved keys without an old-menu control\n\n'
        'Generated by `scripts/assemble_inventory.py` from the reviewed control fragments. '
        'This includes internal/compatibility fields and inactive settings, not only candidates '
        'for new controls. See the schema audit and `NEEDS_DECISION.md` before exposing them.\n\n'
        f'{len(no_controls)} serialized leaf paths have no old-menu control binding. '
        'Color channels and array templates are separate saved-key rows.\n\n'
        + ''.join(f'- `{key}`\n' for key in no_controls), encoding='utf-8')
    print(f'External persisted settings: {len(external_keys)} (hotkeys.cfg, separate from JSON)')
    print('Inventory bindings match JSON/external saved keys or explicit @ui/@action/@state entries.')
    print('This checks inventory structure, NOT existence/behavior of a future new menu.')


if __name__ == '__main__':
    main()
