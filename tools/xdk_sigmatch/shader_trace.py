"""Trace albedo texture coordinates through SDK shader disassembly dumps.

Usage: shader_trace.py <dump_dir> <PS_HASH> [--vs <VS_HASH>]
       shader_trace.py <dump_dir> <PS_HASH> --alpha     (output alpha trace and kill instructions only)

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
_DEST = re.compile(r"^(?:(r)(\d+)|(o)(\d+)|(oPos)|(oDepth|oPts)|oC(\d+))(?:\.([xyzw_01]+))?$")
_COMPS = "xyzw"
_SAFE_CF = ("exec", "exece", "alloc", "cnop")
_COORD_OFFSETS = ("OffsetX", "OffsetY", "OffsetZ")


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
    swz = m.group(8) or "xyzw"
    mask = "".join(_COMPS[i] for i, ch in enumerate(swz[:4]) if ch != "_")
    if m.group(1):
        return int(m.group(2)), "r", mask, swz
    if m.group(3):
        return int(m.group(4)), "o", mask, swz
    if m.group(7) is not None:  # colour export oC<n>
        return int(m.group(7)), "oc", mask, swz
    return -1, "pos" if m.group(5) else "pts" if m.group(6) == "oPts" else "depth", mask, swz


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


def _coord_attr_problem(attrs):
    """Why a fetch's attributes move the sampled coordinate, or None. The disassembler prints
    them only when non-default; filter, LOD and FetchValidOnly overrides do not move it."""
    if attrs.get("UnnormalizedTextureCoords", "false") != "false":
        return "unnormalized texture coordinates"
    for name in _COORD_OFFSETS:
        if name in attrs:
            try:
                nonzero = float(attrs[name]) != 0.0
            except ValueError:
                nonzero = True
            if nonzero:
                return f"fetch {name}={attrs[name]}"
    return None


def trace_ps_albedo(instrs, tfetch):
    if tfetch.dim != "2D":
        return Unsupported(f"texture dimension {tfetch.dim}")
    if len(tfetch.coord_swz) != 2 or tfetch.coord_reg < 0:
        return Unsupported("coordinate is not a 2-component temp")
    pos = _find_pos(instrs, tfetch.index)
    if pos is None or instrs[pos].pred:
        return Unsupported("predicated or missing fetch")
    bad = _coord_attr_problem(instrs[pos].attrs)
    if bad:
        return Unsupported(bad)
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


_JUMPS = ("jmp", "cjmp")
_COND_EXEC = ("cexec", "cexece")
_MAX_ALPHA_SCALES = 2


def _flow_problem(instrs, pos):
    """Why the instruction at `pos` may not run on every path through the shader, or None.

    An instruction is unconditional when (a) the nearest exec line governing it (the last
    control-flow line before it that starts a block of instructions) is a plain exec/exece, not a
    cexec (a cexec block ends at the next control-flow line, so an earlier cexec block that has
    closed does not matter); (b) every forward jump (jmp/cjmp) before it has landed on its label
    before it, so it is not inside a jumped-over region; and (c) nothing before it is a loop, a
    backward jump or any other control flow (call, ret, ...). Predication (p0) is the caller's check
    (Instr.pred)."""
    pending, seen = set(), set()
    governing = None
    for ins in instrs[:pos]:
        if not ins.op.startswith("cf:"):
            continue
        word = ins.op[3:]
        if word in _SAFE_CF or word in _COND_EXEC:
            if word in ("exec", "exece", "cexec", "cexece"):
                governing = word
            continue
        target = ins.raw.split("//")[0].split()[-1]
        if word == "label":
            seen.add(target)
            pending.discard(target)
        elif word in _JUMPS:
            if target in seen:
                return f"backward jump to {target}"
            pending.add(target)
        else:
            return f"control flow: {word}"
    if governing in _COND_EXEC:
        return f"inside a conditional exec ({governing})"
    if pending:
        return f"inside a jumped-over region (to {', '.join(sorted(pending))})"
    return None


def _alpha_step(p, comp):
    """One producer `p` of the traced component -> (temp reg, temp comp, scale ref|None) or Unsupported.
    Accepts mov, max/maxs of a register with itself, and mul/mulsc by exactly one pixel constant
    component; a multiply of two registers is a computed term."""
    if p.sat:
        return Unsupported("_sat in the alpha chain")
    scalar = p.op.startswith("s:")
    op = p.op[2:] if scalar else p.op
    srcs = p.srcs
    idx = _COMPS.index(comp)

    def pick(src):  # component of a vector src that lines up with the destination component
        if src.kind not in ("r", "c") or src.reg is None or src.neg or src.abs:
            return Unsupported(f"alpha operand is not a plain register or constant ({op})")
        if len(src.swizzle) != (1 if scalar else 4):
            return Unsupported("alpha operand swizzle")
        return src.reg, src.swizzle[0 if scalar else idx]

    if op == "maxs" and len(srcs) == 1 and srcs[0].kind == "r" and len(srcs[0].swizzle) == 2 \
            and srcs[0].swizzle[0] == srcs[0].swizzle[1] and srcs[0].reg is not None \
            and not srcs[0].neg and not srcs[0].abs:
        return srcs[0].reg, srcs[0].swizzle[0], None
    if (op == "mov" and len(srcs) == 1) or (op == "max" and len(srcs) == 2 and srcs[0] == srcs[1]):
        t = pick(srcs[0])
        if isinstance(t, Unsupported):
            return t
        if srcs[0].kind != "r":
            return Unsupported("alpha is moved from a constant")
        return t[0], t[1], None
    if op in ("mul", "mulsc") and len(srcs) == 2:
        kinds = sorted(s.kind for s in srcs)
        if kinds == ["r", "r"]:
            return Unsupported("alpha is multiplied by a computed term (dissolve or fade)")
        if kinds != ["c", "r"]:
            return Unsupported(f"alpha {op} operands are not one register and one constant")
        c = pick(next(s for s in srcs if s.kind == "c"))
        t = pick(next(s for s in srcs if s.kind == "r"))
        if isinstance(c, Unsupported):
            return c
        if isinstance(t, Unsupported):
            return t
        if c[0] > 255:
            return Unsupported("constant register out of range")
        return t[0], t[1], f"c{c[0]}.{c[1]}"
    return Unsupported(f"alpha chain passes through {op}")


def trace_ps_alpha(instrs):
    """Where the pixel shader's output alpha (oC0.w) comes from.

    Returns {"source": "one", ...} for the literal 1, or {"source": "fetch", "slot", "component",
    "scale": [...]}: one component of a 2D texture fetch ("component" is the texture channel, after
    the destination swizzle: in `tfetch2D rD.wxyz` the letter at position i is the source channel
    written to destination component i), multiplied by 0 to 2 pixel constant components ("cN.c",
    in the order applied, fetch first). The caller decides whether the slot is the albedo's.

    The chain is walked back from oC0.w through mov, max/maxs of a register with itself and
    mul/mulsc by exactly one constant to the fetch. Every instruction on it, and the oC0.w write,
    must be unconditional and unpredicated (see _flow_problem: control-flow blocks that closed before
    them do not matter) and must be the nearest earlier writer of the traced component, so a
    conditional overwrite in between refuses. Refused: predicated, repeated or _sat writes, more than
    two constants, a multiply by a register ("computed term"), non-2D or coordinate-moving fetches,
    a constant fetch channel, any other op."""
    writers = [i for i, ins in enumerate(instrs) if ins.dest_kind == "oc" and ins.dest == 0 and "w" in ins.mask]
    if not writers:
        return Unsupported("oC0.w is not written")
    if len(writers) > 1:
        return Unsupported(f"oC0.w has {len(writers)} writers")
    pos = writers[0]
    ins = instrs[pos]
    dc = ins.dest_swz[3] if len(ins.dest_swz) == 4 else "?"
    if dc not in ("1", "w"):
        return Unsupported(f"oC0.w is written as {dc!r}")
    scales = []  # outermost first
    comp = "w"
    cur = ins
    cur_pos = pos
    while True:
        if cur.pred:
            return Unsupported("predicated write in the alpha chain")
        flow = _flow_problem(instrs, cur_pos)
        if flow:
            return Unsupported(f"alpha chain write at {cur.index} is conditional: {flow}")
        if cur_pos == pos and dc == "1":
            return {"source": "one", "slot": None, "component": None, "scale": []}
        if cur.op.startswith("tfetch"):
            if cur.op != "tfetch2D":
                return Unsupported(f"alpha source is a {cur.op[len('tfetch'):]} fetch")
            bad = _coord_attr_problem(cur.attrs)
            if bad:
                return Unsupported(bad)
            src = cur.dest_swz[_COMPS.index(comp)] if len(cur.dest_swz) == 4 else None
            if src not in tuple(_COMPS) or cur.fetch_slot is None:
                return Unsupported(f"fetch channel {src!r} is constant or unwritten")
            return {"source": "fetch", "slot": cur.fetch_slot, "component": src, "scale": list(reversed(scales))}
        r = _alpha_step(cur, comp)
        if isinstance(r, Unsupported):
            return r
        reg, comp, scale = r
        if scale is not None:
            scales.append(scale)
            if len(scales) > _MAX_ALPHA_SCALES:
                return Unsupported("alpha has more than two constant scales")
        # The nearest earlier write of reg.comp; writes at the consumer's own index land after its reads.
        found = None
        for i in range(cur_pos - 1, -1, -1):
            p = instrs[i]
            if p.op.startswith("cf:") or p.index == cur.index:
                continue
            if p.dest_kind == "r" and p.dest == reg and comp in p.mask:
                found = i
                break
        if found is None:
            return Unsupported(f"alpha source r{reg}.{comp} is not written by a texture fetch")
        cur, cur_pos = instrs[found], found


def find_kills(instrs):
    """Every pixel kill instruction: [{"index", "op", "pred"}] in program order."""
    return [{"index": i.index, "op": i.op.removeprefix("s:"), "pred": bool(i.pred)}
            for i in instrs if i.op.removeprefix("s:").startswith("kill")]


def trace_vs_export(instrs, interp, comp):
    """Trace export o<interp>.<comp> (comp: 0-3 or 'x'-'w') back to a vertex fetch component."""
    if isinstance(comp, int):
        comp = _COMPS[comp]
    writers = [i for i, ins in enumerate(instrs)
               if ins.dest_kind == "o" and ins.dest == interp and comp in ins.mask]
    if not writers:
        return Unsupported(f"o{interp}.{comp} is not exported")
    if len(writers) > 1:
        return Unsupported(f"o{interp}.{comp} has {len(writers)} writers")
    pos = writers[0]
    # A conditional region (cexec, jumps, loops) anywhere before the export: the export or its
    # inputs may be conditional even when the fetch and the export share one region, where the
    # backward walk below meets no control-flow line.
    for ins in instrs[:pos]:
        if ins.op.startswith("cf:") and ins.op[3:] not in _SAFE_CF:
            return Unsupported(f"control flow before export: {ins.op[3:]}")
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
    ap.add_argument("--alpha", action="store_true", help="print trace_ps_alpha and find_kills only")
    args = ap.parse_args(argv)
    ps = _load(args.dump_dir, "frag", args.ps_hash)
    if args.alpha:
        t = trace_ps_alpha(ps)
        print(f"alpha: unsupported: {t.reason}" if isinstance(t, Unsupported) else f"alpha: {t}")
        print(f"kills: {find_kills(ps)}")
        return 0
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
