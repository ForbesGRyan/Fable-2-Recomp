"""A numeric interpreter for the SDK's Xenos microcode disassembly (`shader_<HASH>.ucode.frag` /
`.vert`). Used to check a hand reading: run the dump on random inputs and compare with the closed
formula (ps_check.py, vs_check.py, light_check.py). Written as evidence of the materials sub-project
(its Task 9, enough for the terrain shaders); the lighting sub-project tracked it and added what the
mesh shaders need: vertex fetches, a0-relative constants, cndge / cndgt, trunc, sin, cos, maxa /
maxas / maxasf, the jump on a bool constant and loops with a count the caller gives. Kills, the cube
ops and getWeights2D are not interpreted: a shader that reaches one raises NotImplementedError.

Semantics follow the SDK's ucode.h (xenos AluVectorOpcode / AluScalarOpcode):
- an ALU instruction is a vector op and/or a scalar op; both read the registers as they were before
  the instruction, then the vector result is written under its mask, then the scalar result under
  its own (the destinations can be different registers);
- `ps` is the previous scalar result; a scalar op with an empty mask (`rN._`) only sets it;
- scalar operands: one register with two components gives a (first) and b (second); a constant
  and a register (mulsc, addsc, subsc) give a = constant, b = register;
- a vector source prints four components: destination component i reads source component
  swizzle[i]; dp4 / dp3 / dp2add reduce the first four / three / two;
- a fetch destination `rD.<swz>`: letter i is the SOURCE component written to destination
  component i, `_` keeps the old value, 0 and 1 are constants;
- `(p0)` / `(!p0)` run the instruction when the predicate has that value; `jmp` with a predicate
  jumps on it; `cexec bN` runs its block when bool constant N is set; `cjmp bN, L` / `cjmp !bN, L`
  jumps when the bool is set / clear;
- `vfetch_full` names a stream (`vfN`), its index operand and an `Offset` (0 when absent),
  `vfetch_mini` reads the stream and index of the full fetch before it at its own `Offset`; the
  caller's callback returns the element's four components and the destination swizzle applies as for
  a texture fetch; `vfetches` lists (instruction, stream, offset, index value);
- `c[N+a0]` is constant N + a0, with a0 set by maxas (floor(a + 0.5)), maxasf (floor(a)) or maxa
  (floor(first operand's w + 0.5)), clamped to [-256, 255];
- `loop iN, Lend` ... `endloop iN, Lbody` runs its body `loops[N]` times (the count field of integer
  constant N; 0 skips to Lend), and a predicated `endloop` leaves early when the predicate has the
  printed value (the SDK's own interpreter, graphics/pipeline/shader/interpreter.cpp). The loop's aL
  register is not modelled: an aL-relative operand is an error.
"""
import math
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import shader_trace as st  # noqa: E402

_COMPS = "xyzw"
_PRED = re.compile(r"\((!?)p0\)")
_CF = re.compile(r"^/\*\s*\d+\.\d+\s*\*/\s*(.*)$")
_CREL = re.compile(r"c\[(\d+)\+a0\]")


def _f(x):
    return float(x)


def _log2(a):
    if a != a:
        return a
    if a == 0:
        return -math.inf
    if a < 0:
        return math.nan
    return math.log2(a)


def _exp2(a):
    if a == -math.inf:
        return 0.0
    try:
        return 2.0 ** a
    except OverflowError:
        return math.inf


def _rsq(a):
    if a == 0:
        return math.inf
    if a < 0:
        return math.nan
    return 1.0 / math.sqrt(a)


def _rcp(a):
    return math.inf if a == 0 else 1.0 / a


def _trunc(a):
    return float(math.floor(a)) if a >= 0 else -float(math.floor(-a))


def _address(a):
    return int(min(max(math.floor(a), -256.0), 255.0))


class Machine:
    def __init__(self, text, consts, sampler, regs=None, bools=None, vfetch=None, loops=None):
        """consts: {register: [4 floats]}; sampler(slot, coords tuple, attrs dict) -> 4 floats;
        regs: {register: [4 floats]} initial values (interpolators or the patch inputs);
        vfetch(stream, offset, attrs dict) -> 4 floats, the vertex element as the fetch delivers it;
        loops: {integer constant: iteration count}."""
        self.lines = text.splitlines()
        self.c = {int(k): [float(x) for x in v] for k, v in consts.items()}
        self.sampler = sampler
        self.vfetch = vfetch
        self.loops = dict(loops or {})
        self.vstream = None
        self.vindex = None
        self.a0 = 0
        self.r = {i: [0.0] * 4 for i in range(64)}
        for k, v in (regs or {}).items():
            self.r[int(k)] = [float(x) for x in v]
        self.bools = set(bools or ())
        self.out = {}
        self.ps = 0.0
        self.p0 = False
        self.trace = []
        self.used_consts = set()
        self.fetches = []
        self.vfetches = []
        self._crel = []

    # --- operands ---------------------------------------------------------------------------
    def _src(self, s, n):
        if s.kind == "r":
            v = self.r[s.reg]
        elif s.kind == "crel":
            reg = self._crel.pop(0) + self.a0
            if reg not in self.c:
                raise KeyError(f"constant c{reg} (a0 = {self.a0}) not supplied")
            self.used_consts.add(reg)
            v = self.c[reg]
        elif s.kind == "c":
            if s.reg not in self.c:
                raise KeyError(f"constant c{s.reg} not supplied")
            self.used_consts.add(s.reg)
            v = self.c[s.reg]
        else:
            raise ValueError(f"operand {s}")
        if s.abs:
            v = [abs(x) for x in v]
        swz = s.swizzle
        out = [v[_COMPS.index(ch)] for ch in swz]
        if s.neg:
            out = [-x for x in out]
        if len(out) < n:
            raise ValueError(f"operand {s} has {len(out)} components, need {n}")
        return out

    def _write(self, ins, vals):
        """vals: 4 floats (per destination component)."""
        swz = ins.dest_swz or "xyzw"
        if ins.dest_kind == "r":
            tgt = self.r[ins.dest]
        else:
            key = {"o": f"o{ins.dest}", "oc": f"oC{ins.dest}", "pos": "oPos", "depth": "oDepth"}[ins.dest_kind]
            tgt = self.out.setdefault(key, [0.0] * 4)
        for i, ch in enumerate(swz[:4]):
            if ch == "_":
                continue
            v = 0.0 if ch == "0" else 1.0 if ch == "1" else vals[i]
            if ins.sat and ch not in "01":
                v = min(max(v, 0.0), 1.0) if v == v else 0.0
            tgt[i] = v

    # --- ops --------------------------------------------------------------------------------
    def _vector(self, ins):
        op = ins.op
        s = ins.srcs
        if op in ("add", "mul", "max", "min", "seq", "sgt", "sge", "sne", "maxa"):
            a, b = self._src(s[0], 4), self._src(s[1], 4)
            if op == "maxa":
                self.a0 = _address(a[3] + 0.5)
                op = "max"
            f = {"add": lambda x, y: x + y, "mul": lambda x, y: x * y, "max": max, "min": min,
                 "seq": lambda x, y: float(x == y), "sgt": lambda x, y: float(x > y),
                 "sge": lambda x, y: float(x >= y), "sne": lambda x, y: float(x != y)}[op]
            return [f(x, y) for x, y in zip(a, b)]
        if op == "mad":
            a, b, c = (self._src(x, 4) for x in s)
            return [x * y + z for x, y, z in zip(a, b, c)]
        if op == "cndeq":
            a, b, c = (self._src(x, 4) for x in s)
            return [y if x == 0 else z for x, y, z in zip(a, b, c)]
        if op in ("cndge", "cndgt"):
            a, b, c = (self._src(x, 4) for x in s)
            if op == "cndge":
                return [y if x >= 0 else z for x, y, z in zip(a, b, c)]
            return [y if x > 0 else z for x, y, z in zip(a, b, c)]
        if op in ("dp4", "dp3"):
            n = 4 if op == "dp4" else 3
            a, b = self._src(s[0], 4), self._src(s[1], 4)
            d = sum(a[i] * b[i] for i in range(n))
            return [d] * 4
        if op == "dp2add":
            a, b, c = (self._src(x, 4) for x in s)
            return [a[0] * b[0] + a[1] * b[1] + c[0]] * 4
        if op == "frc":
            a = self._src(s[0], 4)
            return [x - math.floor(x) for x in a]
        if op == "floor":
            return [math.floor(x) for x in self._src(s[0], 4)]
        if op == "trunc":
            return [_trunc(x) for x in self._src(s[0], 4)]
        raise NotImplementedError(f"vector op {op}")

    def _scalar(self, op, s):
        if op == "retain_prev":
            return self.ps
        if op in ("mulsc", "addsc", "subsc"):
            a, b = self._src(s[0], 1)[0], self._src(s[1], 1)[0]
            return a * b if op == "mulsc" else a + b if op == "addsc" else a - b
        if op in ("adds", "muls", "maxs", "mins", "subs", "maxas", "maxasf"):
            a, b = self._src(s[0], 2)[:2]
            if op in ("maxas", "maxasf"):
                self.a0 = _address(a + 0.5 if op == "maxas" else a)
                return max(a, b)
            return {"adds": a + b, "muls": a * b, "maxs": max(a, b), "mins": min(a, b), "subs": a - b}[op]
        a = self._src(s[0], 1)[0]
        if op == "adds_prev":
            return a + self.ps
        if op == "muls_prev":
            return a * self.ps
        if op == "subs_prev":
            return a - self.ps
        if op == "seqs":
            return float(a == 0)
        if op == "sgts":
            return float(a > 0)
        if op == "sges":
            return float(a >= 0)
        if op == "snes":
            return float(a != 0)
        if op == "floors":
            return float(math.floor(a))
        if op == "frcs":
            return a - math.floor(a)
        if op == "truncs":
            return _trunc(a)
        if op == "sin":
            return math.sin(a)
        if op == "cos":
            return math.cos(a)
        if op == "exp":
            return _exp2(a)
        if op == "log":
            return _log2(a)
        if op == "rcp":
            return _rcp(a)
        if op == "rsq":
            return _rsq(a)
        if op == "sqrt":
            return math.sqrt(a) if a >= 0 else math.nan
        if op in ("setp_eq", "setp_ne", "setp_gt", "setp_ge"):
            self.p0 = {"setp_eq": a == 0, "setp_ne": a != 0, "setp_gt": a > 0, "setp_ge": a >= 0}[op]
            return 0.0 if self.p0 else 1.0
        raise NotImplementedError(f"scalar op {op}")

    _SCALAR = {"retain_prev", "mulsc", "addsc", "subsc", "adds", "muls", "maxs", "mins", "subs", "adds_prev",
               "muls_prev", "subs_prev", "seqs", "sgts", "sges", "snes", "floors", "frcs", "exp", "log", "rcp",
               "rsq", "sqrt", "setp_eq", "setp_ne", "setp_gt", "setp_ge", "maxas", "maxasf", "truncs", "sin",
               "cos"}

    def _fetch(self, ins):
        coords = tuple(self._src(ins.srcs[0], 1 if ins.op == "tfetch1D" else 2))
        sample = [float(x) for x in self.sampler(ins.fetch_slot, coords, ins.attrs)]
        self.fetches.append((ins.index, ins.fetch_slot, coords))
        swz = ins.dest_swz
        tgt = self.r[ins.dest]
        for i, ch in enumerate(swz[:4]):
            if ch == "_":
                continue
            tgt[i] = 0.0 if ch == "0" else 1.0 if ch == "1" else sample[_COMPS.index(ch)]

    def _vfetch(self, ins):
        if self.vfetch is None:
            raise ValueError("a vertex fetch needs the vfetch callback")
        if ins.op == "vfetch_full":
            self.vstream = ins.fetch_slot
            self.vindex = self._src(ins.srcs[0], 1)[0]     # the index register, as the shader computed it
        offset = int(ins.attrs.get("Offset", 0))
        sample = [float(x) for x in self.vfetch(self.vstream, offset, ins.attrs)]
        self.vfetches.append((ins.index, self.vstream, offset, self.vindex))
        tgt = self.r[ins.dest]
        for i, ch in enumerate(ins.dest_swz[:4]):
            if ch == "_":
                continue
            tgt[i] = 0.0 if ch == "0" else 1.0 if ch == "1" else sample[_COMPS.index(ch)]

    # --- program ----------------------------------------------------------------------------
    def run(self):
        """Executes the shader. Groups the parsed rows into instructions (a vector row and its `+` row
        share an index and are applied together)."""
        items = []  # ("cf", word, pred(None/True/False), target/bool) | ("ins", [rows], pred)
        for line in self.lines:
            if not line.strip():
                continue
            m = _CF.match(line)
            if m:
                body = m.group(1).split("//")[0].strip()
                pm = _PRED.match(body)
                pred = None
                if pm:
                    pred = pm.group(1) != "!"
                    body = body[pm.end():].strip()
                words = body.split()
                items.append(("cf", words[0], pred, words[-1].rstrip(",") if len(words) > 1 else None, line))
                continue
            if line.strip().startswith("label "):
                items.append(("cf", "label", None, line.split()[-1], line))
                continue
            rows = st.parse(line if line.lstrip().startswith("/*") else "/* 0 */ " + line.strip().lstrip("+").strip())
            rows = [r for r in rows if not r.op.startswith("cf:")]
            if not rows:
                continue
            row = rows[0]
            pm = _PRED.search(line.split("//")[0][:60])
            pred = (pm.group(1) != "!") if pm else None
            cont = bool(re.match(r"^\s+\+", line))
            numbered = line.lstrip().startswith("/*")
            if row.op in ("serialize", "s:serialize"):
                continue
            if cont and items and items[-1][0] == "ins":
                items[-1][1].append(row)
            else:
                items.append(("ins", [row], pred, numbered, line))
        labels = {it[3]: i for i, it in enumerate(items) if it[0] == "cf" and it[1] == "label"}
        pc = 0
        block_on = True
        loop_stack = []
        steps = 0
        while pc < len(items):
            steps += 1
            if steps > 100000:
                raise RuntimeError("loop")
            it = items[pc]
            pc += 1
            if it[0] == "cf":
                _, word, pred, arg, _line = it
                if word in ("exec", "exece"):
                    block_on = True  # a predicated exec's instructions carry their own predicate
                elif word in ("cexec", "cexece"):
                    neg = arg.startswith("!")
                    n = int(arg.lstrip("!b"))
                    block_on = (n in self.bools) != neg
                elif word == "jmp":
                    if pred is None or pred == self.p0:
                        pc = labels[arg]
                elif word in ("loop", "endloop"):
                    n = int(_line.split(word, 1)[1].split(",")[0].strip().lstrip("i"))
                    if n not in self.loops:
                        raise NotImplementedError(f"loop i{n}: no iteration count given")
                    if word == "loop":
                        if self.loops[n] <= 0:
                            pc = labels[arg]
                        else:
                            loop_stack.append(0)
                    else:
                        loop_stack[-1] += 1
                        if loop_stack[-1] < self.loops[n] and not (pred is not None and pred == self.p0):
                            pc = labels[arg]
                        else:
                            loop_stack.pop()
                elif word == "cjmp":
                    cond = _line.split("cjmp", 1)[1].split(",")[0].strip()
                    if (int(cond.lstrip("!b")) in self.bools) != cond.startswith("!"):
                        pc = labels[arg]
                elif word in ("label", "alloc", "cnop"):
                    pass
                else:
                    raise NotImplementedError(f"control flow {word}")
                continue
            _, rows, pred, _numbered, line = it
            if not block_on:
                continue
            if pred is not None and pred != self.p0:
                continue
            first = rows[0]
            name = first.op[2:] if first.op.startswith("s:") else first.op
            if name.startswith("tfetch"):
                self._fetch(first)
                continue
            if name.startswith("vfetch"):
                self._vfetch(first)
                continue
            # The a0-relative constants of the instruction, in the order its operands are read (the
            # parser keeps no register number for them).
            self._crel = [int(n) for row in rows for n in _CREL.findall(row.raw.split("//")[0])]
            vec = None
            sca = None
            for row in rows:
                n = row.op[2:] if row.op.startswith("s:") else row.op
                if row.op.startswith("s:") or n in self._SCALAR:
                    sca = (row, n)
                else:
                    vec = row
            vres = self._vector(vec) if vec is not None else None
            sres = self._scalar(sca[1], sca[0].srcs) if sca is not None else None
            if vec is not None:
                self._write(vec, vres)
            if sca is not None:
                self.ps = sres
                if sca[0].dest is not None:
                    self._write(sca[0], [sres] * 4)
        return self.out


def load(dump_dir, hash_, kind):
    return (Path(dump_dir) / f"shader_{hash_}.ucode.{kind}").read_text(errors="replace")
