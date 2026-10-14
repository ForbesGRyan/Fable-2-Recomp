"""Closed references for the opening town's post variants.

These do not enable native support. Each shader needs its own produced images;
using the ordinary composite without those dependencies would lose effects.
Sampling callbacks take nominal microcode coordinates, before fetch offsets.
"""
from light_formulas import post_composite


def sat(value):
    return min(max(value, 0.0), 1.0)


def distortion_577(uv, distortion, c31, c46, c47):
    """577CB5267A8C92C3 instructions 3..10; c254.w=2, c255.x=1."""
    edge = [sat(abs(uv[i + 2]) * 2.0 + c47[i]) for i in range(2)]
    fade = min(1.0 - edge[i] * c47[i + 2] for i in range(2))
    return [uv[0] + fade * c46[0] * c31[0] * (distortion[1] - distortion[0]),
            uv[1] + fade * c46[1] * c31[0] * (distortion[3] - distortion[2])]


def distortion_156(uv, distortion, c31, c46, c47):
    """15675EB73FB39170 instructions 5..15; c254.x=1."""
    # The intervening yx/wz swizzles double each component; they don't add Z+W.
    fade = min(1.0 - sat(2.0 * abs(uv[3]) + c47[1]) * c47[3],
               1.0 - sat(2.0 * abs(uv[2]) + c47[0]) * c47[2])
    return [uv[0] + fade * c46[0] * c31[0] * (distortion[1] - distortion[0]),
            uv[1] + fade * c46[1] * c31[0] * (distortion[3] - distortion[2])]


def dof_156(depth, c18, c70, c71):
    """15675 instructions 16..28, including near/far tests and distance fade.

    The ordinary finite reference domain excludes the reciprocal singularity.
    Capture must establish supported endpoint behavior separately.
    """
    distance = c18[1] / (1.0 - c18[0] - depth)
    near = min(max((c70[1] - distance) * c70[0], 0.0), c71[0])
    far = min(max((distance - c70[2]) * c70[3], 0.0), c71[1])
    amount = near if c70[1] > distance else far if distance > c70[2] else 0.0
    return amount * (1.0 - sat(distance - 2550.0))


def composite_577(uv, c, sample):
    coords = distortion_577(uv, sample(3, uv[:2]), c[31], c[46], c[47])
    return post_composite(sample(0, coords), sample(2, coords),
                          lambda value: sample(1, [value])[0],
                          c[77][0], c[78][0], c[79][0], c[69], c[70][0], clamp=False)


def composite_156(uv, c, sample):
    coords = distortion_156(uv, sample(5, uv[:2]), c[31], c[46], c[47])
    sharp, blurred = sample(0, coords), sample(4, coords)
    amount = dof_156(sample(1, coords)[0], c[18], c[70], c[71])
    scene = [sharp[i] + amount * (blurred[i] - sharp[i]) for i in range(3)]
    return post_composite(scene, sample(3, coords),
                          lambda value: sample(2, [value])[0],
                          c[77][0], c[78][0], c[79][0], c[69], c[51][0], clamp=False)


def composite_f51(uv, c, sample):
    """F51AF7DB09EDED71: separate HDR blur, point depth, no distortion."""
    sharp, blurred = sample(0, uv[:2]), sample(4, uv[:2])
    amount = dof_156(sample(1, uv[:2])[0], c[18], c[78], c[79])
    scene = [sharp[i] + amount * (blurred[i] - sharp[i]) for i in range(3)]
    return post_composite(scene, sample(3, uv[:2]),
                          lambda value: sample(2, [value])[0],
                          c[31][0], c[46][0], c[47][0], c[77], c[69][0], clamp=False)
