"""Presentation formulas traced from the game's sprite and text microcode.

Values here precede render-target blending, quantization and the scanout ramp.
Sampler callbacks return fetch-swizzled float4 values. This module does not
infer a UI pass from pitch or claim support for unknown shader pairs.
"""

SPRITE_VS = '7B22146D51E0E4F5'
SPRITE_PS = '0542FFDF2B118509'
TEXT_VS = '901891D9B86C181A'
TEXT_PS = '94C373AB309F37DB'
VIDEO_VS = 'BD2477FEDE6BEAB1'
VIDEO_PS = '130DCA19B2727F68'
COPY_VS = 'D5E12A97B3A84881'
COPY_PS = '686DE15FC84D8B73'


def power_rgb(rgb, exponent):
    return [abs(v) ** exponent for v in rgb]


def sprite_vertex(position, coordinates, alpha, colour, matrix):
    p = list(position[:3]) + [1.0]
    return {'oPos': [sum(x*y for x, y in zip(row, p)) for row in matrix],
            'o0': list(coordinates), 'o1': list(position[:3]) + [alpha],
            'o2': list(colour)}


def text_vertex(position, uv, offset, literal=2.0):
    return {'oPos': [position[0] + offset[0]*literal,
                     position[1] + offset[1]*literal, position[2], 1.0],
            'o0': list(uv) + [0.0, 0.0]}


def sprite_pixel(uv, alpha, colour, window, c, sample):
    first = sample(13, uv[:2])
    rgb = power_rgb(first[:3], c[30][0])
    a = first[3]*alpha
    if c[31][0] == c[255][0]:
        second = sample(14, uv[2:4])
        second_rgb = power_rgb(second[:3], c[30][0])
        weight = c[47][0]
        rgb = [(1-weight)*x + weight*y for x, y in zip(rgb, second_rgb)]
        a *= second[3]
    if c[46][0] == c[255][0]:
        rgb = [x - (c[255][0]-y)*colour[3] for x, y in zip(rgb, colour[:3])]
    if c[77][0] == c[255][0]:
        coords = [abs(window[i])*c[0][i+2] + c[0][i] for i in range(2)]
        a *= sample(15, coords)[3]
    return rgb + [a]


def text_pixel(uv, window, c, sample):
    taps = [sample(13, [uv[0]+dx*c[46][0], uv[1]+dy*c[46][1]])[3]
            for dx, dy in ((0,-1),(-1,-1),(-1,0),(-1,1),(0,1),(1,1),(1,0),(1,-1))]
    first = sample(13, uv)
    a = first[3]*c[31][3]
    rgb = power_rgb([first[i]*c[31][i] for i in range(3)], c[30][0])
    # Instruction 21 compares scaled alpha with c255.x. Instruction 32 keeps
    # fully covered pixels; outline coverage also applies to a zero centre.
    if a < c[255][0]:
        outline = max(c[255][1], *taps)*c[47][3]
        a = a*(c[255][0]-outline) + outline
    if c[77][0] == c[255][0]:
        coords = [abs(window[i])*c[0][i+2] + c[0][i] for i in range(2)]
        a *= sample(14, coords)[3]
    return rgb + [a]


def video_pixel(uv, c, sample):
    y, cr, cb = (sample(slot, uv)[3] for slot in range(3))
    rgb = [y*c[253][2] + cr*c[253][3] + c[255][0],
           cb*c[253][0] + y*c[254][2] + cr*c[254][3] + c[254][0],
           cb*c[253][1] + y*c[254][2] + c[254][1]]
    return [rgb[i]*c[31][i] for i in range(3)] + [c[31][3]]
