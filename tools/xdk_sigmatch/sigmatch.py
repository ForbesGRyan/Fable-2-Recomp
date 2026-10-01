"""Match statically linked XDK functions between two Xbox 360 images.

Inputs are dumps from fable_2_xex_image_dump (<prefix>.img + <prefix>.json).
Function boundaries come from .pdata. Build-variant instruction fields are
masked: branch displacements always; at the "loose" level also the 16-bit
immediates of D-form instructions whose base register is not r1 (absolute
addresses, globals, struct offsets that moved between XDK builds).
"""
import json
import struct
from pathlib import Path

# D-form opcodes with a 16-bit immediate/displacement: addi, addis, ori,
# oris, lwz..stfdu (32-55), ld/std family (58, 62).
D_FORM_IMM = {14, 15, 24, 25, *range(32, 56), 58, 62}


class Image:
    def __init__(self, base, data, sections):
        self.base = base
        self.data = data
        self.sections = sections

    def word(self, addr):
        off = addr - self.base
        return struct.unpack_from(">I", self.data, off)[0]

    def contains(self, addr):
        return 0 <= addr - self.base <= len(self.data) - 4

    def is_executable(self, addr):
        return any(s["executable"] and s["address"] <= addr < s["address"] + s["size"]
                   for s in self.sections)


def load_image(prefix):
    meta = json.loads(Path(prefix + ".json").read_text())
    data = Path(prefix + ".img").read_bytes()
    img = Image(meta["base"], data, meta["sections"])
    img.meta = meta
    return img


def parse_pdata(image, address, size):
    """Function start -> length in bytes. Skips zero-length and non-code entries."""
    funcs = {}
    for off in range(0, size - size % 8, 8):
        start, packed = struct.unpack_from(">II", image.data, address - image.base + off)
        length = ((packed >> 8) & 0x3FFFFF) * 4
        if length == 0 or not image.is_executable(start) or not image.is_executable(start + length - 4):
            continue
        funcs[start] = length
    return funcs


def mask_for(word, level):
    op = word >> 26
    if op == 18:            # b / bl: LI displacement
        return 0xFC000003
    if op == 16:            # bc: BD displacement
        return 0xFFFF0003
    if level == "strict":
        return 0xFFFFFFFF
    ra = (word >> 16) & 0x1F
    if op in D_FORM_IMM and ra != 1:
        return 0xFFFF0000
    return 0xFFFFFFFF


def function_words(image, start, length):
    return [image.word(start + i) for i in range(0, length, 4)]


def matches(ref_words, cand_words, level):
    if len(ref_words) != len(cand_words):
        return False
    for r, c in zip(ref_words, cand_words):
        m = mask_for(r, level)
        if (r & m) != (c & m):
            return False
    return True
