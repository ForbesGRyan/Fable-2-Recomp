"""Trace albedo texture coordinates through SDK shader disassembly dumps.

Usage: shader_trace.py <dump_dir> <PS_HASH> [--vs <VS_HASH>]

Input is the text written by `fable_2.exe --dump_shaders=<dir>`
(`shader_<HASH>.ucode.frag` / `.ucode.vert`). Output shapes match ps-albedo.json
axes and vs-transforms.json "uv" entries (see uv_refs.py for the ref notation).
"""
import argparse
import re
import sys
from collections import namedtuple
from pathlib import Path

Src = namedtuple("Src", "neg reg kind swizzle abs")  # kind: 'r', 'c', 'crel' (relative constant), '?'
Instr = namedtuple("Instr", "index op dest dest_kind mask dest_swz srcs fetch_slot pred sat attrs raw")
TFetch = namedtuple("TFetch", "index dim dest mask coord_reg coord_swz slot")
VFetch = namedtuple("VFetch", "ordinal index dest dest_swizzle fmt offset")
Trace = namedtuple("Trace", "base_reg base_comp stages")


class Unsupported:
    def __init__(self, reason):
        self.reason = reason

    def __repr__(self):
        return f"Unsupported({self.reason!r})"


_FMT = {"FMT_16_16": 25, "FMT_16_16_16_16": 26, "FMT_16_16_FLOAT": 31, "FMT_16_16_16_16_FLOAT": 32,
        "FMT_32_32_FLOAT": 37, "FMT_32_32_32_32_FLOAT": 38, "FMT_32_32_32_FLOAT": 57}
_LINE = re.compile(r"^/\*\s*(\d+)(?:\.\d+)?\s*\*/\s+(.*)$")
_CF = re.compile(r"^/\*\s*\d+\.\d+\s*\*/\s*(.*)$")
_CONT = re.compile(r"^\s+\+\s+(.*)$")
_PRED = re.compile(r"^\((!?)p0\)\s*")
_SRC = re.compile(r"^(-?)(r|c)(_abs)?(?:\[([^\]]*)\]|(\d+))(?:\.([xyzw]+))?$")
_DEST = re.compile(r"^(?:(r)(\d+)|(o)(\d+)|(oPos)|(oDepth))(?:\.([xyzw_01]+))?$")
_COMPS = "xyzw"
_SAFE_CF = ("exec", "exece", "alloc", "cnop")


def _split_operands(text):
    """Split 'a, b, Name=v, ...' into (positional operands, {Name: value})."""
    pos, attrs = [], {}
    for p in (p.strip() for p in text.split(",")):
        if not p:
            continue
        m = re.match(r"^(\w+)=(.*)$", p)
        if m:
            attrs[m.group(1)] = m.group(2)
        elif not attrs:
            pos.append(p)
    return pos, attrs


def _parse_src(text):
    m = _SRC.match(text)
    if not m:
        return Src(False, None, "?", "xyzw", False)
    neg, kind, absf, rel, num, swz = m.groups()
    swz = swz or "xyzw"
    if rel is not None:
        # r_abs[9] is an absolute-value read of r9; c[132+aL] is a relative constant.
        if kind == "r" and absf and rel.isdigit():
            return Src(bool(neg), int(rel), "r", swz, True)
        return Src(bool(neg), None, "crel", swz, bool(absf))
    return Src(bool(neg), int(num), kind, swz, bool(absf))


def _parse_dest(text):
    m = _DEST.match(text)
    if not m:
        return None, None, "", ""
    swz = m.group(7) or "xyzw"
    mask = "".join(_COMPS[i] for i, ch in enumerate(swz[:4]) if ch != "_")
    if m.group(1):
        return int(m.group(2)), "r", mask, swz
    if m.group(3):
        return int(m.group(4)), "o", mask, swz
    return -1, "pos" if m.group(5) else "depth", mask, swz


def _parse_op(index, text, scalar, region_pred, raw):
    text = text.split("//")[0].strip()
    pred = region_pred
    m = _PRED.match(text)
    if m:
        pred = True
        text = text[m.end():]
    head, _, rest = text.partition(" ")
    sat = head.endswith("_sat")
    if sat:
        head = head[:-4]
    pos, attrs = _split_operands(rest)
    dest, dkind, mask, dswz = None, None, "", ""
    srcs, slot = [], None
    if head.startswith(("tfetch", "vfetch")):
        if pos:
            dest, dkind, mask, dswz = _parse_dest(pos[0])
        for p in pos[1:]:
            if re.fullmatch(r"(tf|vf)\d+", p):
                slot = int(p[2:])
            else:
                srcs.append(_parse_src(p))
    elif pos and _DEST.match(pos[0]) and head != "serialize":
        dest, dkind, mask, dswz = _parse_dest(pos[0])
        srcs = [_parse_src(p) for p in pos[1:]]
    else:
        srcs = [_parse_src(p) for p in pos]
    return Instr(index, ("s:" + head) if scalar else head, dest, dkind, mask, dswz, srcs, slot,
                 pred, sat, attrs, raw)


def parse(text):
    """Disassembly text -> list[Instr]. Control flow lines become op "cf:<word>" (index -1);
    scalar co-issue lines (`+ op ...`) become their own Instr with op "s:<op>"."""
    out = []
    last_index = -1
    region_pred = False
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        m = _CF.match(line)
        if m:
            body = m.group(1).split("//")[0].strip()
            pm = _PRED.match(body)
            region_pred = bool(pm)  # instructions under a predicated exec are predicated
            if pm:
                body = body[pm.end():]
            words = body.split()
            out.append(Instr(-1, "cf:" + (words[0] if words else ""), None, None, "", "", [], None,
                             region_pred, False, {}, line))
            continue
        if stripped.startswith("label "):
            region_pred = False
            out.append(Instr(-1, "cf:label", None, None, "", "", [], None, False, False, {}, line))
            continue
        m = _LINE.match(line)
        if m:
            last_index = int(m.group(1))
            out.append(_parse_op(last_index, m.group(2), False, region_pred, line))
            continue
        m = _CONT.match(line)
        if m:
            out.append(_parse_op(last_index, m.group(1), True, region_pred, line))
            continue
        if last_index >= 0:  # instruction line whose index comment is omitted (shares the previous index)
            out.append(_parse_op(last_index, stripped, False, region_pred, line))
    return out


def texture_fetches(instrs):
    out = []
    for ins in instrs:
        if not ins.op.startswith("tfetch"):
            continue
        coord = ins.srcs[0] if ins.srcs else Src(False, -1, "?", "", False)
        out.append(TFetch(ins.index, ins.op[len("tfetch"):], ins.dest, ins.mask,
                          coord.reg if coord.kind == "r" and coord.reg is not None else -1,
                          coord.swizzle, ins.fetch_slot))
    return out


def vfetches(instrs):
    out = []
    for ins in instrs:
        if not ins.op.startswith("vfetch"):
            continue
        out.append(VFetch(len(out), ins.index, ins.dest, ins.dest_swz,
                          _FMT.get(ins.attrs.get("DataFormat")), int(ins.attrs.get("Offset", 0))))
    return out


def _cref(src, comp):
    return f"{'-' if src.neg else ''}c{src.reg}.{src.swizzle[_COMPS.index(comp)]}"


def _step(ins, comp):
    """One producer of `comp` -> (stage|None, temp reg, temp comp), or Unsupported."""
    if ins.pred:
        return Unsupported("predicated write")
    if ins.sat:
        return Unsupported("_sat")
    if ins.op.startswith("s:"):
        return Unsupported(f"scalar co-issue {ins.op[2:]} writes component")
    if any(s.abs for s in ins.srcs):
        return Unsupported("r_abs source")
    if any(s.kind in ("crel", "?") for s in ins.srcs):
        return Unsupported("relative or unknown operand")
    idx = _COMPS.index(comp)
    srcs = ins.srcs
    op = ins.op

    def temp(s):
        if s.neg:
            return Unsupported("negated temp operand")
        return s.reg, s.swizzle[idx]

    if (op == "mov" and len(srcs) == 1) or (op == "max" and len(srcs) == 2 and srcs[0] == srcs[1]):
        s = srcs[0]
        if s.kind != "r":
            return Unsupported("move from constant")
        t = temp(s)
        return t if isinstance(t, Unsupported) else (None, t[0], t[1])
    if op in ("mul", "add") and len(srcs) == 2:
        consts = [s for s in srcs if s.kind == "c"]
        temps = [s for s in srcs if s.kind == "r"]
        if len(consts) != 1 or len(temps) != 1:
            return Unsupported(f"{op} needs exactly one constant operand")
        t = temp(temps[0])
        if isinstance(t, Unsupported):
            return t
        ref = _cref(consts[0], comp)
        stage = {"scale": ref, "offset": None} if op == "mul" else {"scale": None, "offset": ref}
        return stage, t[0], t[1]
    if op == "mad" and len(srcs) == 3:
        a, b, c = srcs
        if c.kind != "c":
            return Unsupported("mad addend is not constant")
        consts = [s for s in (a, b) if s.kind == "c"]
        temps = [s for s in (a, b) if s.kind == "r"]
        if len(consts) != 1 or len(temps) != 1:
            return Unsupported("mad needs one constant and one temp multiplicand")
        t = temp(temps[0])
        if isinstance(t, Unsupported):
            return t
        return {"scale": _cref(consts[0], comp), "offset": _cref(c, comp)}, t[0], t[1]
    if op.startswith("tfetch"):
        return Unsupported("dependent texture fetch")
    if op.startswith("vfetch"):
        return Unsupported("vertex fetch")
    return Unsupported(f"op {op}")


def _find_pos(instrs, index):
    for i, ins in enumerate(instrs):
        if ins.index == index and not ins.op.startswith("cf:"):
            return i
    return None


def _walk(instrs, pos, reg, comp, vs):
    """Backward from position `pos` (exclusive). Returns (Trace, writer fetch Instr|None) or Unsupported.
    With vs=True the walk ends at a vertex fetch writing the register (returned as the writer)."""
    stages = []
    i = pos - 1
    while i >= 0:
        ins = instrs[i]
        if ins.op.startswith("cf:"):
            if ins.op[3:] not in _SAFE_CF:
                return Unsupported(f"control flow: {ins.op[3:]}")
        elif ins.dest_kind == "r" and ins.dest == reg and comp in ins.mask:
            if vs and ins.op.startswith("vfetch") and not ins.pred:
                return Trace(reg, comp, list(reversed(stages))), ins
            r = _step(ins, comp)
            if isinstance(r, Unsupported):
                return r
            stage, reg, comp = r
            if stage is not None:
                stages.append(stage)
        i -= 1
    if vs:
        return Unsupported("vertex shader reads a register no vertex fetch wrote")
    return Trace(reg, comp, list(reversed(stages))), None


def trace_component(instrs, before_index, reg, comp):
    """Trace reg.comp as read by the instruction with index `before_index` back to a shader input."""
    pos = _find_pos(instrs, before_index)
    if pos is None:
        return Unsupported(f"no instruction with index {before_index}")
    r = _walk(instrs, pos, reg, comp, False)
    return r if isinstance(r, Unsupported) else r[0]


def trace_ps_albedo(instrs, tfetch):
    if tfetch.dim != "2D":
        return Unsupported(f"texture dimension {tfetch.dim}")
    if len(tfetch.coord_swz) != 2 or tfetch.coord_reg < 0:
        return Unsupported("coordinate is not a 2-component temp")
    pos = _find_pos(instrs, tfetch.index)
    if pos is None or instrs[pos].pred:
        return Unsupported("predicated or missing fetch")
    out = {}
    for axis, comp in zip("uv", tfetch.coord_swz):
        r = trace_component(instrs, tfetch.index, tfetch.coord_reg, comp)
        if isinstance(r, Unsupported):
            return Unsupported(f"{axis}: {r.reason}")
        if len(r.stages) > 2:
            return Unsupported(f"{axis}: more than two stages")
        if r.base_reg > 15:
            return Unsupported(f"{axis}: input r{r.base_reg} is not an interpolator")
        out[axis] = {"input": f"r{r.base_reg}.{r.base_comp}", "stages": r.stages}
    return out


def trace_vs_export(instrs, interp, comp):
    """Trace export o<interp>.<comp> (comp: 0-3 or 'x'-'w') back to a vertex fetch component."""
    if isinstance(comp, int):
        comp = _COMPS[comp]
    pos = None
    for i in range(len(instrs) - 1, -1, -1):
        ins = instrs[i]
        if ins.dest_kind == "o" and ins.dest == interp and comp in ins.mask:
            pos = i
            break
    if pos is None:
        return Unsupported(f"o{interp}.{comp} is not exported")
    r = _step(instrs[pos], comp)
    if isinstance(r, Unsupported):
        return Unsupported(f"export: {r.reason}")
    stage, reg, c = r
    w = _walk(instrs, pos, reg, c, True)
    if isinstance(w, Unsupported):
        return w
    trace, fetch = w
    stages = list(trace.stages) + ([stage] if stage is not None else [])
    if len(stages) > 2:
        return Unsupported("more than two stages")
    f = next(v for v in vfetches(instrs) if v.index == fetch.index and v.dest == fetch.dest)
    if f.fmt is None:
        return Unsupported(f"format {fetch.attrs.get('DataFormat')}")
    src = f.dest_swizzle[_COMPS.index(trace.base_comp)] if len(f.dest_swizzle) == 4 else None
    if src not in tuple(_COMPS):
        return Unsupported(f"fetch component {src!r} is constant or unwritten")
    return {"fetch": f.ordinal, "src": src, "format": f.fmt, "offset": f.offset, "stages": stages}


def _load(dump_dir, kind, hash_):
    path = Path(dump_dir) / f"shader_{hash_}.ucode.{kind}"
    return parse(path.read_text(errors="replace"))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump_dir")
    ap.add_argument("ps_hash")
    ap.add_argument("--vs", dest="vs_hash")
    args = ap.parse_args(argv)
    ps = _load(args.dump_dir, "frag", args.ps_hash)
    vs = _load(args.dump_dir, "vert", args.vs_hash) if args.vs_hash else None
    for tf in texture_fetches(ps):
        print(f"tfetch{tf.dim} tf{tf.slot} -> r{tf.dest}.{tf.mask} coord r{tf.coord_reg}.{tf.coord_swz} @ {tf.index}")
        t = trace_ps_albedo(ps, tf)
        if isinstance(t, Unsupported):
            print(f"  unsupported: {t.reason}")
            continue
        for axis in "uv":
            print(f"  {axis}: {t[axis]}")
            if vs is not None:
                inp = t[axis]["input"]
                interp, comp = int(inp[1:inp.index(".")]), inp[-1]
                print(f"     vs o{interp}.{comp}: {trace_vs_export(vs, interp, comp)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
