#!/usr/bin/env python3
"""
Nokia 3310 (NHM-5 v06.39) Series 20 Asset Extractor.

Parses NHM5NY06.390 (MCU) and NHM5NY06.39E (PPM) directly and generates:
  - src/s20_assets.h
  - src/s20_assets.c
containing fonts, bitmaps, animations, localized strings (EN/RU), and the menu tree.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MCU_PATH = os.path.join(ROOT, "NHM5NY06.390")
PPM_PATH = os.path.join(ROOT, "NHM5NY06.39E")
OUT_H = os.path.join(ROOT, "src", "s20_assets.h")
OUT_C = os.path.join(ROOT, "src", "s20_assets.c")
OUT_GAMES_H = os.path.join(ROOT, "src", "s20_games_data.h")
OUT_GAMES_C = os.path.join(ROOT, "src", "s20_games_data.c")


def load_fls(path):
    with open(path, "rb") as f:
        d = f.read()
    pos = 0
    out = bytearray()
    base = None
    while pos < len(d):
        if pos + 9 > len(d):
            break
        addr = (d[pos + 1] << 16) | (d[pos + 2] << 8) | d[pos + 3]
        size = (d[pos + 5] << 16) | (d[pos + 6] << 8) | d[pos + 7]
        if base is None:
            base = addr
        out.extend(d[pos + 9 : pos + 9 + size])
        pos += 9 + size
    return base, bytes(out)


def parse_ppm_strings(ppm):
    def read_sub(off, count, is_ucs2):
        data = ppm[off + 16 :]
        lens = data[:count]
        assert data[count] == 0xFF, f"Expected 0xFF sentinel at {off:x}"
        s_pos = count + 1
        out = []
        for l in lens:
            if is_ucs2:
                raw = data[s_pos : s_pos + l * 2]
                s_pos += l * 2
                cps = [struct.unpack(">H", raw[i : i + 2])[0] for i in range(0, len(raw), 2)]
                # Strip leading 0x04FF UCS-2 marker if present and trailing 0x0000
                if cps and cps[0] == 0x04FF:
                    cps = cps[1:]
                while cps and cps[-1] == 0:
                    cps.pop()
                s = "".join(chr(c) for c in cps)
            else:
                raw = data[s_pos : s_pos + l]
                s_pos += l
                s = raw.decode("latin-1", errors="replace").rstrip("\x00")
            out.append(s)
        return out

    comm = read_sub(0x2990, 51, False)
    engl = read_sub(0x2C30, 0x535, False)
    russ = read_sub(0x74A0, 0x535, True)
    return comm + engl, comm + russ


def parse_ppm_fonts(ppm):
    fonts = []
    for i in range(4):
        e = 0x043C + i * 0x2C
        o1, o2, o3, n1, n2, flg, _ = struct.unpack(">IIIHHII", ppm[e : e + 24])
        n1_s = ppm[e + 24 : e + 34].split(b"\x00")[0].decode("ascii", "ignore")
        n2_s = ppm[e + 35 : e + 43].split(b"\x00")[0].decode("ascii", "ignore")
        fallback = struct.unpack(">b", ppm[e + 41 : e + 42])[0]
        glyphs = {}
        for k in range(n2 + 1):
            p = e + o3 + k * 8
            c1, c2, val = struct.unpack(">HHI", ppm[p : p + 8])
            if c1 > c2 or c1 == 0xFFFF:
                continue
            grp = (val >> 10) & 0xF
            h = (val >> 5) & 0x1F
            ascent = val & 0x1F
            base_bit = val >> 14
            o1_e = e + o1 + grp * 12
            data_rel, gflg, w = struct.unpack(">IHH", ppm[o1_e : o1_e + 8])
            data_base = (o1_e + data_rel) if (gflg & 1) else data_rel
            for ch in range(c1, c2 + 1):
                bit_off = base_bit + (ch - c1) * h
                ptr = data_base + (bit_off >> 3) * w
                shift = bit_off & 7
                cols = []
                for x in range(w):
                    b0 = ppm[ptr + x]
                    b1 = ppm[ptr + x + w] if (shift + h > 8) else 0
                    b2 = ppm[ptr + x + 2 * w] if (shift + h > 16) else 0
                    val24 = b0 | (b1 << 8) | (b2 << 16)
                    cols.append((val24 >> shift) & ((1 << h) - 1))
                glyphs[ch] = (w, h, ascent, cols)
        fonts.append(
            {
                "id": i,
                "name": f"{n1_s}_{n2_s}",
                "fallback": fallback,
                "glyphs": glyphs,
            }
        )
    return fonts


def parse_bitmaps(mcu_base, mcu):
    # Bitmap ID mapping table at 0x301b78 (384 uint16 entries),
    # Model mask at 0x3019fc, descriptors at 0x300844 (378 x 12 bytes)
    id_map = []
    for bid in range(384):
        o = 0x301B78 - mcu_base + bid * 2
        idx = struct.unpack(">H", mcu[o : o + 2])[0]
        # Note: NHM-5 model mask matches 0x56 / 0xff; verify if idx entry mask has bit 0x02 or 0x04
        # Look up first matching variant in [idx .. next_id_idx]
        next_o = 0x301B78 - mcu_base + (bid + 1) * 2
        next_idx = struct.unpack(">H", mcu[next_o : next_o + 2])[0] if bid < 383 else idx + 1
        chosen = idx
        for cand in range(idx, max(idx + 1, min(378, next_idx))):
            m = mcu[0x3019FC - mcu_base + cand]
            if m & 0x02:  # NHM-5 (3310) 84x48 (m=0x56 or m=0xff)
                chosen = cand
                break
        id_map.append(chosen)

    bitmaps = []
    for idx in range(378):
        o2 = 0x300844 - mcu_base + idx * 12
        data_ptr, u4, w, h, u10 = struct.unpack(">IIBBH", mcu[o2 : o2 + 12])
        nbytes = ((h + 7) // 8) * w
        if w > 0 and h > 0 and mcu_base <= data_ptr and (data_ptr - mcu_base + nbytes) <= len(mcu):
            raw = mcu[data_ptr - mcu_base : data_ptr - mcu_base + nbytes]
        else:
            w, h, raw = 0, 0, b""
        bitmaps.append((w, h, raw))
    return id_map, bitmaps


def parse_animations(mcu_base, mcu):
    # Master anim table at 0x325bec (36 entries x 12 bytes)
    # Sequence pointers at 0x2f34a4 (33 x 4 bytes), byte-lengths at 0x32b644
    anims = []
    for aid in range(36):
        o = 0x325BEC - mcu_base + aid * 12
        fn, w1, w2 = struct.unpack(">III", mcu[o : o + 12])
        delay_val = (w1 >> 16) & 0xFFFF
        frame_cnt = (w1 >> 8) & 0xFF
        seq_idx = w1 & 0xFF
        mode = (w2 >> 24) & 0xFF
        frames = []
        if (fn & ~1) == 0x2EA1A0 and seq_idx < 33:
            ptr_o = 0x2F34A4 - mcu_base + seq_idx * 4
            seq_ptr = struct.unpack(">I", mcu[ptr_o : ptr_o + 4])[0]
            nbytes = mcu[0x32B644 - mcu_base + seq_idx]
            nframes = nbytes // 2
            so = seq_ptr - mcu_base
            frames = [struct.unpack(">H", mcu[so + k * 2 : so + k * 2 + 2])[0] for k in range(nframes)]
        anims.append((aid, delay_val, mode, frames))
    return anims


def parse_menus(mcu_base, mcu, strings_en):
    # Walk all menus reachable from Main Menu (0x30fa3c)
    menus = []
    addr_to_id = {}

    def resolve_tid(ptr):
        if mcu_base <= ptr < mcu_base + len(mcu):
            o = ptr - mcu_base
            if mcu[o] == 4 and mcu[o + 1] != 0xFF:
                tid = (mcu[o + 1] << 8) | mcu[o + 2]
                if tid < len(strings_en):
                    return tid
        return 0xFFFF

    def walk(caddr):
        if caddr in addr_to_id or not (mcu_base <= caddr < mcu_base + len(mcu)):
            return addr_to_id.get(caddr, -1)
        mid = len(menus)
        addr_to_id[caddr] = mid
        co = caddr - mcu_base
        items_ptr, hdr_word, state_ptr = struct.unpack(">III", mcu[co : co + 12])
        count = (hdr_word >> 24) & 0xFF
        f1 = (hdr_word >> 16) & 0xFF
        f2 = hdr_word & 0xFFFF
        m = {
            "id": mid,
            "addr": caddr,
            "count": count,
            "f1": f1,
            "f2": f2,
            "state_ptr": state_ptr,
            "items": [],
        }
        menus.append(m)
        for idx in range(count + 1):
            io = items_ptr - mcu_base + idx * 20
            txt_ptr, arg, hnd, u12, flags = struct.unpack(">IIIII", mcu[io : io + 20])
            tid = resolve_tid(txt_ptr)
            hnd_ctx = (hnd >> 16) & 0xFFFF
            hnd_fn = hnd & 0xFFFF
            sub_id = -1
            if idx > 0 and hnd_fn == 0x0387 and (mcu_base <= arg < mcu_base + len(mcu)):
                sub_id = walk(arg)
            m["items"].append(
                {
                    "text_id": tid,
                    "arg": arg,
                    "hnd_ctx": hnd_ctx,
                    "hnd_fn": hnd_fn,
                    "u12": u12 & 0xFFFF,
                    "flags": flags,
                    "anim_id": flags & 0xFF,
                    "sub_id": sub_id,
                }
            )
        return mid

    walk(0x30FA3C)
    return menus


def parse_tones(mcu_base, mcu, ppm):
    # Frequency table at 0x32afd0 (102 uint16 entries in Hz; index 60 = 440 Hz = A4)
    freqs = [
        struct.unpack(">H", mcu[0x32AFD0 - mcu_base + i * 2 : 0x32AFD0 - mcu_base + i * 2 + 2])[0]
        for i in range(102)
    ]

    # PPM TONE chunk starts at 0x25db0, subchunks start at 0x25dc0
    tones = []
    pos = 0x25DC0
    end = 0x25DB0 + 0x1698
    while pos + 8 <= end:
        tid, sub_len = struct.unpack(">II", ppm[pos : pos + 8])
        if tid == 0 or sub_len == 0 or pos + sub_len > end:
            break
        tag = ppm[pos + 8 : pos + 12].decode("ascii", "ignore")
        nlen = ppm[pos + 16]
        name = ppm[pos + 17 : pos + 17 + nlen].decode("latin-1", "ignore").rstrip("\x00")
        pl = ppm[pos + 17 + nlen : pos + sub_len]
        while pl and pl[-1] == 0xFF:
            pl = pl[:-1]
        tones.append({"id": tid, "tag": tag, "name": name, "data": pl})
        pos += sub_len
    return freqs, tones


def c_escape_bytes(s):
    b = s.encode("utf-8")
    out = []
    for byte in b:
        if byte == 0x5C:
            out.append("\\\\")
        elif byte == 0x22:
            out.append('\\"')
        elif byte == 0x0A:
            out.append("\\n")
        elif byte == 0x0D:
            out.append("\\r")
        elif 32 <= byte < 127:
            out.append(chr(byte))
        else:
            out.append(f"\\{byte:03o}")
    return '"' + "".join(out) + '"'


def read_12b_sprite(mcu_base, mcu, addr):
    o = addr - mcu_base
    ptr, zero, w, h, pad = struct.unpack(">IIBBH", mcu[o : o + 12])
    nbytes = ((h + 7) // 8) * w
    raw = mcu[ptr - mcu_base : ptr - mcu_base + nbytes]
    return (w, h, raw)


def scan_12b_sprites(mcu_base, mcu, start, end):
    res = []
    a = start
    while a <= end - 12:
        o = a - mcu_base
        ptr, zero, w, h, pad = struct.unpack(">IIBBH", mcu[o : o + 12])
        if 0x300000 <= ptr <= 0x340000 and zero == 0 and 1 <= w <= 84 and 1 <= h <= 48 and pad == 0:
            nbytes = ((h + 7) // 8) * w
            if (ptr - mcu_base + nbytes) <= len(mcu):
                res.append((a, w, h, mcu[ptr - mcu_base : ptr - mcu_base + nbytes]))
                a += 12
                continue
        a += 2
    return res


def parse_games_data(mcu_base, mcu):
    # Snake II: 39 sprites at 0x327E10, 9 speeds at 0x3280FC, 6 mazes at 0x328108
    snake_sprites = [read_12b_sprite(mcu_base, mcu, 0x327E10 + i * 12) for i in range(39)]
    snake_speeds = list(mcu[0x3280FC - mcu_base : 0x3280FC - mcu_base + 9])
    snake_mazes = []
    for m in range(6):
        o = 0x328108 - mcu_base + m * 16
        cnt, _, _, _, ptr, sx, sy, _, _, fx, fy, _, _ = struct.unpack(">BBBBI8B", mcu[o : o + 16])
        segs = []
        for k in range(cnt):
            so = ptr - mcu_base + k * 8
            ax, ay, _, _, bx, by = mcu[so : so + 6]
            segs.append((ax, ay, bx, by))
        snake_mazes.append({"sx": sx, "sy": sy, "fx": fx, "fy": fy, "segs": segs})

    # Space Impact: 83 sprites at 0x312DD8, 7 terrains at 0x311740, 4 Y-paths at 0x312104
    si_sprites = [read_12b_sprite(mcu_base, mcu, 0x312DD8 + i * 12) for i in range(83)]
    si_terrains = [list(mcu[0x311740 - mcu_base + i * 32 : 0x311740 - mcu_base + (i + 1) * 32]) for i in range(7)]
    si_paths = [
        [struct.unpack(">b", mcu[0x312104 - mcu_base + i * 84 + j : 0x312104 - mcu_base + i * 84 + j + 1])[0] for j in range(84)]
        for i in range(4)
    ]

    # Bantumi: 4 boards (0x31D0C0..0x31D0EB) + 11 bowls/arrows (0x31D208..0x31D28B)
    bantumi_sprites = [read_12b_sprite(mcu_base, mcu, 0x31D0C0 + i * 12) for i in range(4)] + [
        read_12b_sprite(mcu_base, mcu, 0x31D208 + i * 12) for i in range(11)
    ]

    # Pairs II: flip frames (0x31AD6C..0x31ADBF), 28 icons (0x31ADC0..0x31AF27), HUD/boards (0x31AF70..0x31AFDB)
    pairs_flip = [w_h_raw[1:] for w_h_raw in scan_12b_sprites(mcu_base, mcu, 0x31AD60, 0x31ADC0)]
    pairs_cards = [read_12b_sprite(mcu_base, mcu, 0x31ADC0 + i * 12) for i in range(28)]
    pairs_misc = [read_12b_sprite(mcu_base, mcu, 0x31AF70 + i * 12) for i in range(9)]

    return {
        "snake_sprites": snake_sprites,
        "snake_speeds": snake_speeds,
        "snake_mazes": snake_mazes,
        "si_sprites": si_sprites,
        "si_terrains": si_terrains,
        "si_paths": si_paths,
        "bantumi_sprites": bantumi_sprites,
        "pairs_flip": pairs_flip,
        "pairs_cards": pairs_cards,
        "pairs_misc": pairs_misc,
    }


def write_games_data(gd):
    all_bmps = []
    bmp_blob = bytearray()

    def reg_bmps(lst):
        start = len(all_bmps)
        for w, h, raw in lst:
            off = len(bmp_blob)
            bmp_blob.extend(raw)
            all_bmps.append((w, h, off))
        return start, len(lst)

    sn_off, sn_cnt = reg_bmps(gd["snake_sprites"])
    si_off, si_cnt = reg_bmps(gd["si_sprites"])
    bn_off, bn_cnt = reg_bmps(gd["bantumi_sprites"])
    pf_off, pf_cnt = reg_bmps(gd["pairs_flip"])
    pc_off, pc_cnt = reg_bmps(gd["pairs_cards"])
    pm_off, pm_cnt = reg_bmps(gd["pairs_misc"])

    h_lines = [
        "#ifndef S20_GAMES_DATA_H",
        "#define S20_GAMES_DATA_H",
        "",
        '#include "s20_assets.h"',
        "#include <stdint.h>",
        "",
        f"#define S20_SNAKE_SPRITE_CNT   {sn_cnt}",
        f"#define S20_SI_SPRITE_CNT      {si_cnt}",
        f"#define S20_BANTUMI_SPRITE_CNT {bn_cnt}",
        f"#define S20_PAIRS_FLIP_CNT     {pf_cnt}",
        f"#define S20_PAIRS_CARD_CNT     {pc_cnt}",
        f"#define S20_PAIRS_MISC_CNT     {pm_cnt}",
        "",
        "typedef struct {",
        "    uint8_t x0, y0, x1, y1;",
        "} s20_snake_wall_seg_t;",
        "",
        "typedef struct {",
        "    uint8_t sx, sy;",
        "    uint8_t fx, fy;",
        "    uint8_t seg_count;",
        "    s20_snake_wall_seg_t segs[10];",
        "} s20_snake_maze_t;",
        "",
        "extern const uint8_t s20_games_bmp_data[];",
        "extern const s20_bitmap_t s20_snake_sprites[S20_SNAKE_SPRITE_CNT];",
        "extern const uint8_t s20_snake_speeds[9];",
        "extern const s20_snake_maze_t s20_snake_mazes[6];",
        "",
        "extern const s20_bitmap_t s20_si_sprites[S20_SI_SPRITE_CNT];",
        "extern const uint8_t s20_si_terrains[7][32];",
        "extern const int8_t s20_si_paths[4][84];",
        "",
        "extern const s20_bitmap_t s20_bantumi_sprites[S20_BANTUMI_SPRITE_CNT];",
        "",
        "extern const s20_bitmap_t s20_pairs_flip_sprites[S20_PAIRS_FLIP_CNT];",
        "extern const s20_bitmap_t s20_pairs_card_sprites[S20_PAIRS_CARD_CNT];",
        "extern const s20_bitmap_t s20_pairs_misc_sprites[S20_PAIRS_MISC_CNT];",
        "",
        "#endif",
        "",
    ]
    with open(OUT_GAMES_H, "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines))

    c_lines = [
        '#include "s20_games_data.h"',
        "",
        f"const uint8_t s20_games_bmp_data[{len(bmp_blob)}] = {{",
    ]
    for off in range(0, len(bmp_blob), 16):
        c_lines.append("    " + ", ".join(f"0x{b:02x}" for b in bmp_blob[off : off + 16]) + ",")
    c_lines.append("};")
    c_lines.append("")

    def emit_bmp_arr(name, start, cnt):
        c_lines.append(f"const s20_bitmap_t {name}[{cnt}] = {{")
        for w, h, off in all_bmps[start : start + cnt]:
            c_lines.append(f"    {{ {w}, {h}, {off}u }},")
        c_lines.append("};")
        c_lines.append("")

    emit_bmp_arr("s20_snake_sprites", sn_off, sn_cnt)
    c_lines.append("const uint8_t s20_snake_speeds[9] = { " + ", ".join(str(x) for x in gd["snake_speeds"]) + " };")
    c_lines.append("const s20_snake_maze_t s20_snake_mazes[6] = {")
    for mz in gd["snake_mazes"]:
        segs_str = ", ".join(f"{{ {a}, {b}, {c}, {d} }}" for (a, b, c, d) in mz["segs"])
        c_lines.append(f"    {{ {mz['sx']}, {mz['sy']}, {mz['fx']}, {mz['fy']}, {len(mz['segs'])}, {{ {segs_str} }} }},")
    c_lines.append("};")
    c_lines.append("")

    emit_bmp_arr("s20_si_sprites", si_off, si_cnt)
    c_lines.append("const uint8_t s20_si_terrains[7][32] = {")
    for tr in gd["si_terrains"]:
        c_lines.append("    { " + ", ".join(str(x) for x in tr) + " },")
    c_lines.append("};")
    c_lines.append("const int8_t s20_si_paths[4][84] = {")
    for pt in gd["si_paths"]:
        c_lines.append("    { " + ", ".join(str(x) for x in pt) + " },")
    c_lines.append("};")
    c_lines.append("")

    emit_bmp_arr("s20_bantumi_sprites", bn_off, bn_cnt)
    emit_bmp_arr("s20_pairs_flip_sprites", pf_off, pf_cnt)
    emit_bmp_arr("s20_pairs_card_sprites", pc_off, pc_cnt)
    emit_bmp_arr("s20_pairs_misc_sprites", pm_off, pm_cnt)

    with open(OUT_GAMES_C, "w", encoding="utf-8") as f:
        f.write("\n".join(c_lines))
    print(f"Generated {OUT_GAMES_H} and {OUT_GAMES_C} ({len(all_bmps)} game sprites, {len(bmp_blob)} bytes)")


def main():
    os.makedirs(os.path.join(ROOT, "src"), exist_ok=True)
    mcu_base, mcu = load_fls(MCU_PATH)
    ppm_base, ppm = load_fls(PPM_PATH)

    strings_en, strings_ru = parse_ppm_strings(ppm)
    fonts = parse_ppm_fonts(ppm)
    id_map, bitmaps = parse_bitmaps(mcu_base, mcu)
    anims = parse_animations(mcu_base, mcu)
    menus = parse_menus(mcu_base, mcu, strings_en)
    tone_freqs, tones = parse_tones(mcu_base, mcu, ppm)
    games_data = parse_games_data(mcu_base, mcu)
    write_games_data(games_data)

    total_glyphs = sum(len(f["glyphs"]) for f in fonts)
    print(
        f"Extracted: {len(strings_en)} strings (EN/RU), {len(fonts)} fonts ({total_glyphs} glyphs), "
        f"{len(bitmaps)} bitmaps, {len(anims)} anims, {len(menus)} menus, {len(tones)} tones"
    )

    # Write s20_assets.h
    h_lines = [
        "#ifndef S20_ASSETS_H",
        "#define S20_ASSETS_H",
        "",
        "#include <stdint.h>",
        "",
        "#define S20_NUM_STRINGS " + str(len(strings_en)),
        "#define S20_NUM_FONTS   " + str(len(fonts)),
        "#define S20_NUM_BITMAPS " + str(len(bitmaps)),
        "#define S20_NUM_BMP_IDS " + str(len(id_map)),
        "#define S20_NUM_ANIMS   " + str(len(anims)),
        "#define S20_NUM_MENUS   " + str(len(menus)),
        "#define S20_NUM_TONE_FREQS " + str(len(tone_freqs)),
        "#define S20_NUM_TONES   " + str(len(tones)),
        "",
        "typedef enum {",
        "    S20_FONT_LARGE_BOLD = 0,",
        "    S20_FONT_SMALL_PLAIN = 1,",
        "    S20_FONT_SMALL_BOLD = 2,",
        "    S20_FONT_TINY_PLAIN = 3",
        "} s20_font_id_t;",
        "",
        "typedef enum {",
        "    S20_LANG_EN = 0,",
        "    S20_LANG_RU = 1",
        "} s20_lang_t;",
        "",
        "typedef struct {",
        "    uint16_t codepoint;",
        "    uint8_t width;",
        "    uint8_t height;",
        "    uint8_t ascent;",
        "    uint32_t data_offset;",
        "} s20_glyph_t;",
        "",
        "typedef struct {",
        "    const char *name;",
        "    int8_t fallback;",
        "    uint16_t glyph_count;",
        "    const s20_glyph_t *glyphs;",
        "    const uint16_t *col_data;",
        "} s20_font_t;",
        "",
        "typedef struct {",
        "    uint8_t width;",
        "    uint8_t height;",
        "    uint32_t data_offset;",
        "} s20_bitmap_t;",
        "",
        "typedef struct {",
        "    uint16_t delay;",
        "    uint8_t mode;",
        "    uint8_t frame_count;",
        "    const uint16_t *frames;",
        "} s20_anim_t;",
        "",
        "typedef struct {",
        "    uint16_t text_id;",
        "    uint32_t arg;",
        "    uint16_t hnd_ctx;",
        "    uint16_t hnd_fn;",
        "    uint16_t u12;",
        "    uint32_t flags;",
        "    uint8_t anim_id;",
        "    int16_t submenu_id;",
        "} s20_menu_item_t;",
        "",
        "typedef struct {",
        "    uint8_t count;",
        "    uint8_t f1;",
        "    uint16_t f2;",
        "    uint32_t state_ptr;",
        "    const s20_menu_item_t *items;",
        "} s20_menu_t;",
        "",
        "typedef struct {",
        "    uint8_t id;",
        "    const char *name;",
        "    uint16_t len;",
        "    const uint8_t *data;",
        "} s20_tone_t;",
        "",
        "extern const char * const s20_strings_en[S20_NUM_STRINGS];",
        "extern const char * const s20_strings_ru[S20_NUM_STRINGS];",
        "extern const s20_font_t s20_fonts[S20_NUM_FONTS];",
        "extern const uint16_t s20_bitmap_id_map[S20_NUM_BMP_IDS];",
        "extern const s20_bitmap_t s20_bitmaps[S20_NUM_BITMAPS];",
        "extern const uint8_t s20_bitmap_data[];",
        "extern const s20_anim_t s20_anims[S20_NUM_ANIMS];",
        "extern const s20_menu_t s20_menus[S20_NUM_MENUS];",
        "extern const uint16_t s20_tone_freqs[S20_NUM_TONE_FREQS];",
        "extern const s20_tone_t s20_tones[S20_NUM_TONES];",
        "",
        "const char *s20_get_string(uint16_t text_id, s20_lang_t lang);",
        "const s20_glyph_t *s20_find_glyph(int font_id, uint16_t cp, int *out_font_id);",
        "const s20_bitmap_t *s20_get_bitmap(uint16_t bmp_id);",
        "",
        "#endif /* S20_ASSETS_H */",
        "",
    ]
    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("\n".join(h_lines))

    # Write s20_assets.c
    c_lines = [
        '#include "s20_assets.h"',
        "#include <stddef.h>",
        "",
        "const char * const s20_strings_en[S20_NUM_STRINGS] = {",
    ]
    for i, s in enumerate(strings_en):
        comma = "," if i + 1 < len(strings_en) else ""
        c_lines.append(f"    {c_escape_bytes(s)}{comma}")
    c_lines.append("};")
    c_lines.append("")
    c_lines.append("const char * const s20_strings_ru[S20_NUM_STRINGS] = {")
    for i, s in enumerate(strings_ru):
        comma = "," if i + 1 < len(strings_ru) else ""
        c_lines.append(f"    {c_escape_bytes(s)}{comma}")
    c_lines.append("};")
    c_lines.append("")

    # Fonts
    for fi, fnt in enumerate(fonts):
        col_pool = []
        g_entries = []
        for cp in sorted(fnt["glyphs"].keys()):
            w, h, asc, cols = fnt["glyphs"][cp]
            off = len(col_pool)
            col_pool.extend(cols)
            g_entries.append((cp, w, h, asc, off))
        c_lines.append(f"static const uint16_t s20_font_{fi}_cols[] = {{")
        for off in range(0, len(col_pool), 16):
            chunk = ", ".join(f"0x{v:04x}" for v in col_pool[off : off + 16])
            c_lines.append(f"    {chunk},")
        c_lines.append("};")
        c_lines.append(f"static const s20_glyph_t s20_font_{fi}_glyphs[] = {{")
        for cp, w, h, asc, off in g_entries:
            c_lines.append(f"    {{ 0x{cp:04x}, {w}, {h}, {asc}, {off} }},")
        c_lines.append("};")
        c_lines.append("")

    c_lines.append("const s20_font_t s20_fonts[S20_NUM_FONTS] = {")
    for fi, fnt in enumerate(fonts):
        c_lines.append(
            f'    {{ "{fnt["name"]}", {fnt["fallback"]}, {len(fnt["glyphs"])}, s20_font_{fi}_glyphs, s20_font_{fi}_cols }},'
        )
    c_lines.append("};")
    c_lines.append("")

    # Bitmaps
    c_lines.append("const uint16_t s20_bitmap_id_map[S20_NUM_BMP_IDS] = {")
    for off in range(0, len(id_map), 16):
        c_lines.append("    " + ", ".join(str(v) for v in id_map[off : off + 16]) + ",")
    c_lines.append("};")
    c_lines.append("")

    bmp_blob = bytearray()
    c_lines.append("const s20_bitmap_t s20_bitmaps[S20_NUM_BITMAPS] = {")
    for w, h, raw in bitmaps:
        doff = len(bmp_blob)
        bmp_blob.extend(raw)
        c_lines.append(f"    {{ {w}, {h}, {doff} }},")
    c_lines.append("};")
    c_lines.append("")
    c_lines.append("const uint8_t s20_bitmap_data[] = {")
    for off in range(0, len(bmp_blob), 16):
        c_lines.append("    " + ", ".join(f"0x{b:02x}" for b in bmp_blob[off : off + 16]) + ",")
    c_lines.append("};")
    c_lines.append("")

    # Animations
    for aid, delay, mode, frames in anims:
        if frames:
            fstr = ", ".join(f"0x{fr:04x}" for fr in frames)
            c_lines.append(f"static const uint16_t s20_anim_{aid}_frames[] = {{ {fstr} }};")
    c_lines.append("const s20_anim_t s20_anims[S20_NUM_ANIMS] = {")
    for aid, delay, mode, frames in anims:
        fptr = f"s20_anim_{aid}_frames" if frames else "NULL"
        c_lines.append(f"    {{ {delay}, {mode}, {len(frames)}, {fptr} }},")
    c_lines.append("};")
    c_lines.append("")

    # Menus
    for m in menus:
        c_lines.append(f"static const s20_menu_item_t s20_menu_{m['id']}_items[] = {{")
        for it in m["items"]:
            c_lines.append(
                f"    {{ 0x{it['text_id']:04x}, 0x{it['arg']:08x}u, 0x{it['hnd_ctx']:04x}, 0x{it['hnd_fn']:04x}, 0x{it['u12']:04x}, 0x{it['flags']:08x}u, 0x{it['anim_id']:02x}, {it['sub_id']} }},"
            )
        c_lines.append("};")
    c_lines.append("const s20_menu_t s20_menus[S20_NUM_MENUS] = {")
    for m in menus:
        c_lines.append(
            f"    {{ {m['count']}, 0x{m['f1']:02x}, 0x{m['f2']:04x}, 0x{m['state_ptr']:08x}u, s20_menu_{m['id']}_items }},"
        )
    c_lines.append("};")
    c_lines.append("")

    # Tone frequencies & ringtones
    c_lines.append("const uint16_t s20_tone_freqs[S20_NUM_TONE_FREQS] = {")
    for off in range(0, len(tone_freqs), 12):
        c_lines.append("    " + ", ".join(str(v) for v in tone_freqs[off : off + 12]) + ",")
    c_lines.append("};")
    c_lines.append("")
    for ti, tn in enumerate(tones):
        c_lines.append(f"static const uint8_t s20_tone_{ti}_data[] = {{")
        for off in range(0, len(tn["data"]), 16):
            c_lines.append("    " + ", ".join(f"0x{b:02x}" for b in tn["data"][off : off + 16]) + ",")
        c_lines.append("};")
    c_lines.append("const s20_tone_t s20_tones[S20_NUM_TONES] = {")
    for ti, tn in enumerate(tones):
        c_lines.append(
            f'    {{ {tn["id"]}, {c_escape_bytes(tn["name"])}, {len(tn["data"])}, s20_tone_{ti}_data }},'
        )
    c_lines.append("};")
    c_lines.append("")

    # Helper lookup functions
    c_lines.extend(
        [
            "const char *s20_get_string(uint16_t text_id, s20_lang_t lang) {",
            "    if (text_id >= S20_NUM_STRINGS) return \"\";",
            "    return (lang == S20_LANG_RU) ? s20_strings_ru[text_id] : s20_strings_en[text_id];",
            "}",
            "",
            "const s20_glyph_t *s20_find_glyph(int font_id, uint16_t cp, int *out_font_id) {",
            "    int cur = font_id;",
            "    for (int depth = 0; depth < 4 && cur >= 0 && cur < S20_NUM_FONTS; depth++) {",
            "        const s20_font_t *f = &s20_fonts[cur];",
            "        int lo = 0, hi = (int)f->glyph_count - 1;",
            "        while (lo <= hi) {",
            "            int mid = (lo + hi) >> 1;",
            "            uint16_t mcp = f->glyphs[mid].codepoint;",
            "            if (mcp == cp) {",
            "                if (out_font_id) *out_font_id = cur;",
            "                return &f->glyphs[mid];",
            "            }",
            "            if (mcp < cp) lo = mid + 1; else hi = mid - 1;",
            "        }",
            "        if (f->fallback == 0) break;",
            "        cur += f->fallback;",
            "    }",
            "    return NULL;",
            "}",
            "",
            "const s20_bitmap_t *s20_get_bitmap(uint16_t bmp_id) {",
            "    if (bmp_id >= S20_NUM_BMP_IDS) return NULL;",
            "    uint16_t idx = s20_bitmap_id_map[bmp_id];",
            "    if (idx >= S20_NUM_BITMAPS) return NULL;",
            "    return &s20_bitmaps[idx];",
            "}",
            "",
        ]
    )

    with open(OUT_C, "w", encoding="utf-8") as f:
        f.write("\n".join(c_lines))
    print(f"Generated {OUT_H} and {OUT_C}")


if __name__ == "__main__":
    main()
