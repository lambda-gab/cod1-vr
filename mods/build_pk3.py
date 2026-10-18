# Builds the optional VR data packs from the game's own files (read only):
#   pakz_vr_nohands.pk3 - first-person hands drawn with a fully transparent alpha-tested skin
#   pakz_vr_text.pk3    - the two training texts that mention the mouse, rewritten for VR
#   pakz_vr_scope.pk3   - scoped rifles without the full-screen scope overlay (not installed:
#                         the VR scope view uses the overlay as its frame)
# CoDSP.exe 1.5 only mounts pak*.pk3 and localized_*.pk3 from main; pakz_* sorts after pakb.pk3,
# so its files override the originals. Delete a pack to undo it.
import glob, os, re, struct, sys, zipfile

# usage: python build_pk3.py "<game folder>"   (the folder with CoDSP.exe)
GAME_MAIN = os.path.join(sys.argv[1] if len(sys.argv) > 1 else '.', 'main')
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out')
# cloth_masked is one of the game's model shader types (alphaFunc GE128): alpha 0 draws nothing
INVISIBLE_SKIN = 'cloth_masked@vr_invisible.tga'

def game_files(prefix):
    """Latest version of every file under prefix (later paks override earlier ones)."""
    files = {}
    for pk in sorted(glob.glob(os.path.join(GAME_MAIN, 'pak*.pk3'))):
        z = zipfile.ZipFile(pk)
        for n in z.namelist():
            if n.startswith(prefix) and not n.endswith('/'):
                files[n] = z.read(n)
    return files

def scope_pack():
    out = {}
    for name, data in game_files('weapons/sp/').items():
        fields = data.decode('latin1').split('\\')
        keys = fields[1::2]
        if 'adsOverlayShader' not in keys:
            continue
        i = 1 + 2 * keys.index('adsOverlayShader')
        if not fields[i + 1]:
            continue
        fields[i + 1] = ''
        if 'adsOverlayReticle' in keys:
            fields[1 + 2 * keys.index('adsOverlayReticle') + 1] = 'none'
        out[name] = '\\'.join(fields).encode('latin1')
    return out

def tga_transparent(w=4, h=4):
    header = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 8)
    return header + b'\x00\x00\x00\x00' * (w * h)

def replace_skins(data):
    # ... 0xffffffff, 0x00000000, u16 skin count, then that many NUL-terminated names to EOF
    marker = b'\xff\xff\xff\xff\x00\x00\x00\x00'
    at = data.rfind(marker)
    if at < 0:
        return None
    pos = at + len(marker)
    count = struct.unpack_from('<H', data, pos)[0]
    names = data[pos + 2:].split(b'\x00')
    if count == 0 or len(names) < count or any(not n for n in names[:count]) or any(names[count:]):
        return None   # not the layout we expect: leave this model alone
    return data[:pos + 2] + b''.join(INVISIBLE_SKIN.encode() + b'\x00' for _ in range(count))

def nohands_pack():
    out = {'skins/' + INVISIBLE_SKIN: tga_transparent()}
    skipped = []
    for name, data in game_files('xmodel/viewmodel_hands').items():
        new = replace_skins(data)
        if new is None:
            skipped.append(name)
        else:
            out[name] = new
    return out, skipped

# Tutorial texts that talk about the mouse. Only these sentences are ours; the rest of each
# .str file comes from the player's own localized pak.
VR_TEXTS = {
    'localizedstrings/english/training.str': {
        'FIVESIGNS': 'Point your gun hand at the five large signs.',
        'CLIMBLADDER': 'To climb a ladder, push the move stick into the ladder and point your gun hand up.',
    },
}

def text_pack():
    out = {}
    for name, texts in VR_TEXTS.items():
        data = None
        for pk in sorted(glob.glob(os.path.join(GAME_MAIN, 'localized_english_pak*.pk3'))):
            z = zipfile.ZipFile(pk)
            if name in z.namelist():
                data = z.read(name)   # later paks override earlier ones
        if data is None:
            continue
        s = data.decode('latin1')
        changed = 0
        for ref, text in texts.items():
            # REFERENCE <ref>  /  LANG_ENGLISH "<text>"
            pat = re.compile(r'(REFERENCE\s+' + re.escape(ref) + r'\s*\r?\n\s*LANG_ENGLISH\s+")[^"]*(")')
            s, k = pat.subn(lambda m: m.group(1) + text + m.group(2), s, count=1)
            changed += k
        if changed:
            out[name] = s.encode('latin1')
    return out

def write(path, files):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for n in sorted(files):
            z.writestr(n, files[n])

if __name__ == '__main__':
    if not os.path.isdir(GAME_MAIN):
        sys.exit('usage: python build_pk3.py "<game folder with CoDSP.exe>"')
    os.makedirs(OUT, exist_ok=True)
    scope = scope_pack()
    write(os.path.join(OUT, 'pakz_vr_scope.pk3'), scope)
    print('scope pack:', sorted(scope))
    hands, skipped = nohands_pack()
    write(os.path.join(OUT, 'pakz_vr_nohands.pk3'), hands)
    print('nohands pack:', len(hands) - 1, 'hand models; skipped:', skipped)
    texts = text_pack()
    write(os.path.join(OUT, 'pakz_vr_text.pk3'), texts)
    print('text pack:', sorted(texts))
