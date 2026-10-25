#!/usr/bin/env python3
"""Batch v83 login scene repair for the modified Linux HeavenClient.

Usage:
    python patch_v83_login_scenes.py                 # dry run
    python patch_v83_login_scenes.py --apply         # backed-up edit
    python patch_v83_login_scenes.py --root /path/to/MapleStory-Client --apply

Only alters scene composition, asset drawing, and matching hit-test coordinates.
Does not replace any networking, authentication, creation, or exit logic.
"""
import argparse
import difflib
import re
from pathlib import Path
from datetime import datetime
import shutil
import sys

TAG = "V83_LOGIN_SCENES_V2"
FILES = (
    "IO/UITypes/UILogin.cpp",
    "IO/UITypes/UIWorldSelect.cpp",
    "IO/UITypes/UICharSelect.cpp",
)


def cut_function(src, signature):
    hits = list(re.finditer(re.escape(signature), src))
    if len(hits) != 1:
        raise ValueError(f"expected one function '{signature}', found {len(hits)}")
    start = hits[0].start()
    brace = src.find('{', hits[0].end())
    if brace < 0:
        raise ValueError(f"missing opening brace: {signature}")
    nesting = 0
    for i in range(brace, len(src)):
        if src[i] == '{':
            nesting += 1
        elif src[i] == '}':
            nesting -= 1
            if nesting == 0:
                return start, i + 1, src[start:i + 1]
    raise ValueError(f"missing closing brace: {signature}")


def update_function(src, signature, fn):
    start, end, fragment = cut_function(src, signature)
    new = fn(fragment)
    if new == fragment:
        raise ValueError(f"no modifications in '{signature}' (unsupported source variant?)")
    return src[:start] + new + src[end:]


def require_one(src, old, new, label):
    count = src.count(old)
    if count != 1:
        raise ValueError(f"{label}: expected exact anchor once, found {count}")
    return src.replace(old, new, 1)


def replace_regex(src, pat, repl, label, required=True):
    new, count = re.subn(pat, repl, src, count=1, flags=re.MULTILINE)
    if required and count != 1:
        raise ValueError(f"{label}: expected one regex match, found {count}")
    return new


def patch_login(src):
    if TAG in src:
        print("  Login already patched; leaving unchanged")
        return src

    def change_ctor(ctor):
        anchor = 'sprites.emplace_back(common["frame"], Point<int16_t>(400, 300));'
        additions = '''// V83_LOGIN_SCENES_V2: v83 light shafts, one animation per effect child.
        // They belong to the title scene, under the parchment frame.
        for (int i = 0; i < 6; ++i)
        {
            nl::node beam = title["effect"][std::to_string(i)];
            if (beam)
                sprites.emplace_back(beam, Point<int16_t>(735, -10));
        }
        '''
        return require_one(ctor, anchor, additions + anchor, "login frame")

    return update_function(src, 'UILogin::UILogin()', change_ctor)


def patch_world(src):
    if TAG in src:
        print("  World selection already patched; leaving unchanged")
        return src

    def ctor_changes(ctor):
        # Backdrop must be first, and the scroll is under the rope hanger.
        ctor = replace_regex(
            ctor,
            r'\s*sprites\.emplace_back\(\s*obj\["WorldSelect"\]\["neoCity"\]\["0"\]\["0"\],\s*background_pos\s*\);',
            '\n        // V83_LOGIN_SCENES_V2: NeoCity backdrop is drawn before the other scene sprites.\n',
            'world backdrop')
        ctor = replace_regex(
            ctor,
            r'\s*sprites\.emplace_back\(\s*obj\["WorldSelect"\]\["signboard"\]\["0"\]\["0"\],\s*Point<int16_t>\(400,\s*80\)\s*\);',
            '\n        // The rope hanger is drawn above the scroll in draw().\n',
            'world rope hanger')
        ctor = replace_regex(
            ctor,
            r'\s*sprites\.emplace_back\(common\["step"\]\["1"\],\s*Point<int16_t>\(40,\s*0\)\);',
            '\n        // Step indicator is drawn above the parchment in draw().\n',
            'world step indicator')

        # Correct the 4x5 v83 channel button grid, even if previous attempts
        # moved it. Keep button IDs, activation, and network logic intact.
        ctor = replace_regex(
            ctor,
            r'\d+\s*\+\s*static_cast<int16_t>\(\(i % 4\) \* \d+\),\s*\d+\s*\+\s*static_cast<int16_t>\(\(i / 4\) \* \d+\)',
            '198 + static_cast<int16_t>((i % 4) * 106),\n                305 + static_cast<int16_t>((i / 4) * 35)',
            'world 4x5 channel-grid position')
        ctor = replace_regex(
            ctor,
            r'worldselect\["BtGoworld"\],\s*Point<int16_t>\(\d+,\s*\d+\)',
            'worldselect["BtGoworld"],\n                Point<int16_t>(504, 444)',
            'world Enter Channel button')
        return ctor

    src = update_function(src, 'UIWorldSelect::UIWorldSelect()', ctor_changes)

    def draw_changes(draw):
        before = 'UIElement::draw_sprites(alpha);'
        # The NeoCity city image is bitmap 0/0; draw from its authored origin,
        # stretching to fit the v83 800x600 scene like the v83 reference.
        background = '''// V83_LOGIN_SCENES_V2: draw the background beneath all objects.
        static const Texture v83_city(
            nl::nx::map["Obj"]["login.img"]["WorldSelect"]["neoCity"]["0"]["0"]);
        if (v83_city.is_valid())
        {
            Point<int16_t> origin = v83_city.get_origin();
            v83_city.draw(DrawArgument(origin, origin,
                Point<int16_t>(800, 600), 1.0f, 1.0f, 1.0f, 0.0f));
        }
        '''
        draw = require_one(draw, before, background + before, 'world draw backdrop')
        hanger = '''// The rope and stage tab sit IN FRONT of the scroll, not behind it.
        static const Texture v83_hanger(
            nl::nx::map["Obj"]["login.img"]["WorldSelect"]["signboard"]["0"]["0"]);
        if (v83_hanger.is_valid())
            v83_hanger.draw(position + Point<int16_t>(400, 80));
        static const Texture v83_step(nl::nx::ui["Login.img"]["Common"]["step"]["1"]);
        if (v83_step.is_valid())
            v83_step.draw(position + Point<int16_t>(40, 0));

        '''
        draw = require_one(draw, 'UIElement::draw_buttons(alpha);', hanger + 'UIElement::draw_buttons(alpha);', 'world draw hanger')
        # The optional view-recommended list in this fork can be unpopulated;
        # avoid out-of-range texture reads when the server reports no choices.
        draw = draw.replace('recommended_world_textures[recommended_worldid].draw(position + Point<int16_t>(336, 187));',
            'if (recommended_worldid < recommended_world_textures.size())\n                recommended_world_textures[recommended_worldid].draw(position + Point<int16_t>(336, 187));')
        return draw

    src = update_function(src, 'void UIWorldSelect::draw(float alpha) const', draw_changes)

    def world_buttons(func):
        func = replace_regex(
            func,
            r'\d+\s*\+\s*static_cast<int16_t>\(index \* \d+\),\s*\d+\s*\)',
            '182 + static_cast<int16_t>(index * 27),\n                    158)',
            'world plank positions')
        return func

    src = update_function(src, 'void UIWorldSelect::draw_world()', world_buttons)
    return src


def patch_char(src):
    if TAG in src:
        print("  Character selection already patched; leaving unchanged")
        return src

    def ctor_changes(ctor):
        ctor = replace_regex(
            ctor,
            r'\s*if\s*\(backs\["1"\]\)\s*sprites\.emplace_back\(backs\["1"\],\s*Point<int16_t>\(400,\s*300\)\);',
            '\n        // V83_LOGIN_SCENES_V2: the narrow sky strip is tiled in draw().\n',
            'char sky sprite')
        # Match hit-testing rectangles to the visually displayed bridge slots.
        ctor = replace_regex(
            ctor,
            r'get_character_slot_pos\(i,\s*105,\s*144\)',
            'get_character_slot_pos(i, 253, 298)',
            'char slot click areas')
        return ctor

    src = update_function(src, 'UICharSelect::UICharSelect(', ctor_changes)

    def draw_changes(draw):
        sky = '''// V83_LOGIN_SCENES_V2: back/1 is a narrow vertical sky strip.
        // Draw it before the tree and bridge; stretching a single copy
        // sideways produces the blue column seen in the old build.
        static const Texture v83_sky(nl::nx::map["Back"]["login.img"]["back"]["1"]);
        if (v83_sky.is_valid() && v83_sky.width() > 0)
        {
            const auto o = v83_sky.get_origin();
            const int tile = v83_sky.width();
            for (int x = 0; x < 800; x += tile)
            {
                Point<int16_t> p(static_cast<int16_t>(x + o.x()), o.y());
                v83_sky.draw(DrawArgument(p, p,
                    Point<int16_t>(static_cast<int16_t>(tile), 600),
                    1.0f, 1.0f, 1.0f, 0.0f));
            }
        }
        // Clouds belong between the sky and the foreground scenery.
        static const Texture v83_cloud_back(nl::nx::map["Back"]["login.img"]["back"]["3"]);
        if (v83_cloud_back.is_valid())
            v83_cloud_back.draw(position + Point<int16_t>(238, 71));
        '''
        draw = require_one(draw, 'UIElement::draw_sprites(inter);', sky + 'UIElement::draw_sprites(inter);', 'char sky and tree drawing')
        draw = require_one(draw, 'UIElement::draw_sprites(inter);',
            'UIElement::draw_sprites(inter);\n        static const Texture v83_cloud_front(nl::nx::map["Back"]["login.img"]["back"]["5"]);\n        if (v83_cloud_front.is_valid())\n            v83_cloud_front.draw(position + Point<int16_t>(301, 407));',
            'char foreground cloud')
        draw = replace_regex(draw,
            r'get_character_slot_pos\(i,\s*135,\s*234\)',
            'get_character_slot_pos(i, 278, 374)', 'actual character feet position')
        draw = replace_regex(draw,
            r'get_character_slot_pos\(i,\s*130,\s*234\)',
            'get_character_slot_pos(i, 278, 374)', 'empty character feet position')
        # The scroll art is a separate node from the paper's stat sheet.
        draw = require_one(draw,
            'charinfo.draw(position + charinfopos);',
            '''static const Texture v83_info_scroll(
                        nl::nx::ui["Login.img"]["CharSelect"]["scroll"]["0"]["3"]);
                    Point<int16_t> scroll_pos(
                        static_cast<int16_t>(charpos.x() - 108), charinfopos.y());
                    if (v83_info_scroll.is_valid())
                        v83_info_scroll.draw(position + scroll_pos);
                    // The sheet's WZ origin is 45,57, so compensate for it.
                    charinfo.draw(position + scroll_pos + Point<int16_t>(62, 93));''',
            'selected character info scroll')
        draw = replace_regex(draw,
            r'Point<int16_t> pos_adj = Point<int16_t>\(668,\s*365\);',
            'Point<int16_t> pos_adj = Point<int16_t>(\n                        static_cast<int16_t>(charpos.x() + 2), 280);',
            'selected character stats text placement')
        draw = replace_regex(draw,
            r'\s*signpost\[j\]\.draw\(chararg\);',
            '\n                // The single v83 signpost is already in the scene background.',
            'remove duplicate signpost for each character')
        return draw

    src = update_function(src, 'void UICharSelect::draw(float inter) const', draw_changes)

    def slot_position(func):
        return replace_regex(func,
            r'int16_t x = 125 \* \(index % 4\);\s*int16_t y = 200 \* \(index > 3\);\s*return Point<int16_t>\(x \+ x_adj, y \+ y_adj\);',
            '// v83: one row of eight character positions on the rope bridge.\n        int16_t x = static_cast<int16_t>(72 * index);\n        return Point<int16_t>(static_cast<int16_t>(x + x_adj), static_cast<int16_t>(y_adj));',
            'one row bridge layout')

    src = update_function(src, 'Point<int16_t> UICharSelect::get_character_slot_pos(', slot_position)
    return src


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--apply', action='store_true', help='Write files after backing them up')
    ap.add_argument('--root', type=Path,
                    default=Path.home()/'Desktop/Projects/MS/MapleStory-Client')
    args = ap.parse_args()
    root = args.root.expanduser().resolve()
    print('v83 login/world/character scene patch')
    print('Project:', root)
    print('Mode:', 'APPLY' if args.apply else 'DRY RUN')

    transformations = (patch_login, patch_world, patch_char)
    new_content = {}
    originals = {}
    try:
        # Validate all changes BEFORE touching any source file.
        for name, transform in zip(FILES, transformations):
            path = root/name
            original = path.read_text()
            new = transform(original)
            originals[name] = original
            new_content[name] = new
            print(f'  {name}:', 'unchanged' if new == original else 'ready')
        modified = [name for name in FILES if new_content[name] != originals[name]]
        if not modified:
            print('All three files are already patched. No changes.')
            return
        if not args.apply:
            print('\nDry run passed. Use --apply to change', len(modified), 'files.')
            return

        backup_dir = root/('v83-scene-backup-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
        backup_dir.mkdir(parents=True, exist_ok=False)
        for name in modified:
            dest = backup_dir/name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(root/name, dest)
        # All preflight checks passed; commit as same-directory atomic renames.
        for name in modified:
            dest = root/name
            temp = dest.with_name(dest.name + '.v83-new.tmp')
            temp.write_text(new_content[name])
            temp.replace(dest)
        print('\nApplied to', len(modified), 'files.')
        print('Backups:', backup_dir)
        print('Rebuild: cmake --build build -j 6 > ~/maplestory-build.log 2>&1')
    except (ValueError, OSError) as e:
        print('\nStopped safely:', e, file=sys.stderr)
        print('No files changed by this invocation unless all preflight checks passed.', file=sys.stderr)
        raise SystemExit(1)

if __name__ == '__main__':
    main()
