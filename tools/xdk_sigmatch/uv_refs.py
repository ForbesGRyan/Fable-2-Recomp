"""Shared notation for UV transforms in ps-albedo.json and vs-transforms.json "uv".

A constant ref "c8.y" / "-c8.y" encodes as (negate << 11) | (bank << 10) | (reg * 4 + comp)
with bank 0 = vertex constants, 1 = pixel constants; None = identity (-1).
A stage list holds at most two {"scale": ref|None, "offset": ref|None}, applied in order.
"""
import re

_COMP = "xyzw"


def encode_ref(text, bank):
    if text is None:
        return -1
    m = re.fullmatch(r"(-?)c(\d+)\.([xyzw])", text)
    if not m or int(m.group(2)) > 255:
        raise ValueError(f"bad constant ref {text!r}")
    neg = 1 if m.group(1) else 0
    return (neg << 11) | (bank << 10) | (int(m.group(2)) * 4 + _COMP.index(m.group(3)))


def stages(items, bank):
    if len(items) > 2:
        raise ValueError(f"more than two UV stages: {items!r}")
    out = []
    for s in list(items) + [{}] * (2 - len(items)):
        out += [encode_ref(s.get("scale"), bank), encode_ref(s.get("offset"), bank)]
    return out


def input_comp(text):
    """'r0.y' (pixel shader input) or 'o3.w' (vertex shader export) -> (index, component)."""
    m = re.fullmatch(r"[ro](\d+)\.([xyzw])", text)
    if not m or int(m.group(1)) > 15:
        raise ValueError(f"bad interpolator component {text!r}")
    return int(m.group(1)), _COMP.index(m.group(2))
