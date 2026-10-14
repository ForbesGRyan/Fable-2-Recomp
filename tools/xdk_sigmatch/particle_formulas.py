"""Closed billboard formula for vertex shader 29B6506FBACEB93A.

The native model preserves all four vertex lights, curve interpolation, camera
basis, rotation and distance fades. The third fetched float4 is unused by this
variant. Relative curve reads must fit the captured register bank.
"""
import math

VS = '29B6506FBACEB93A'

def dot(a, b):
    return sum(x*y for x, y in zip(a, b))

def norm(a):
    length = math.sqrt(dot(a, a))
    return [x/length for x in a] if length else [0.0]*3

def sat(x):
    return min(max(x, 0.0), 1.0)

def vertex(index, position, shape, c, soft=False):
    age = shape[3]
    first = int(age)
    assert 0 <= first <= 98
    fraction = age-math.floor(age)
    curve = lambda base: [c[base+first][i]*(1-fraction)+c[base+first+1][i]*fraction for i in range(4)]
    colour, size, stretch = curve(116), curve(136), curve(156)
    # Geometry corners run (0,0), (1,0), (1,1), (0,1); texture V flips.
    corner = index % 4
    u = float(corner in (1, 2))
    v = float(corner in (2, 3))
    angle = ((shape[2]*c[254][3]+c[255][1]) % 1.0)*c[254][1]+c[255][3]
    x = (u+c[253][0])*shape[0]*size[0]
    y = (v+c[253][0])*shape[1]*size[1]
    rotated = [(x*math.cos(angle)-y*math.sin(angle))*stretch[0],
               (x*math.sin(angle)+y*math.cos(angle))*stretch[1]]
    center = [dot(c[row], position[:3]+[1.0]) for row in (72, 73, 74)]
    world = [center[i]+rotated[0]*c[115][i]+rotated[1]*c[114][i] for i in range(3)]
    normal = norm([a+b for a,b in zip(norm([c[46][i]-center[i] for i in range(3)]),
                                    [x*c[255][1] for x in norm([world[i]-center[i] for i in range(3)])])])
    distance = math.sqrt(sum((c[46][i]-world[i])**2 for i in range(3)))
    alpha = sat((distance-c[47][0])*c[47][2])*sat((c[113][0]-distance)*c[113][1])*c[113][2]*colour[3]
    lit = list(c[8][:3])
    for light in range(4):
        direction = [c[76+light][i]-world[i]*c[76+light][3] for i in range(3)]
        factor = sat(dot(norm(direction), normal))/(1+c[253][1]*c[90][light]*dot(direction, direction))
        for i in range(3):
            lit[i] += c[83+light][i]*factor
    rgb = [lit[i]*(1-size[2])+colour[i]*size[2] for i in range(3)]
    result={'oPos': [dot(c[row],world+[1.0]) for row in range(4)],
            'o0': [u, 1-v, size[3], 0.0]}
    if soft:result.update(o1=world+[0.0],o2=rgb+[alpha])
    else:result['o1']=rgb+[alpha]
    return result

def pixel(uv, colour, texels, exposure, thresholds=(), rates=()):
    sampled = list(texels[0])
    for i in range(1, len(texels)):
        weight = sat((uv[2]-thresholds[i-1])*rates[i-1])
        sampled = [a*(1-weight)+b*weight for a,b in zip(sampled, texels[i])]
    return [sampled[i]**2*colour[i]*colour[3]*exposure for i in range(3)]+[sampled[3]*colour[3]]


def distortion_vertex(index, position, shape, c):
    """00D85B797ECDDF7B: unlit billboard, camera c8, centre c113..115.

    The captured literal permutation is c253=(4,2pi,1,1/2pi),
    c254=(.25,.5,-2,-pi), c255=(-.5,0,0,0). This is a separate
    producer; it must not inherit the 29B billboard's lights or fade bank.
    """
    first = int(shape[3])
    assert 0 <= shape[3] < 99
    fraction = shape[3]-math.floor(shape[3])
    curve = lambda base: [c[base+first][i]*(1-fraction)+c[base+first+1][i]*fraction for i in range(4)]
    colour, size, stretch = curve(116), curve(136), curve(156)
    corner = index % 4
    u, v = float(corner in (1, 2)), float(corner in (2, 3))
    angle = ((shape[2]*c[253][3]+c[254][1]) % 1.0)*c[253][1]+c[254][3]
    x = (u+c[255][0])*shape[0]*size[0]
    y = (v+c[255][0])*shape[1]*size[1]
    rx = (x*math.cos(angle)-y*math.sin(angle))*stretch[0]
    ry = (x*math.sin(angle)+y*math.cos(angle))*stretch[1]
    centre = [dot(c[row], position[:3]+[1.0]) for row in (113, 114, 115)]
    world = [centre[i]+rx*c[73][i]+ry*c[72][i] for i in range(3)]
    distance = math.sqrt(sum((c[8][i]-world[i])**2 for i in range(3)))
    fade = sat((distance-c[46][0])*c[46][2])*sat((c[47][0]-distance)*c[47][1])*c[47][2]
    return {'oPos': [dot(c[row], world+[1.0]) for row in range(4)],
            'o0': [u, 1-v, size[3], 0.0], 'o1': [fade*x for x in colour]}


def distortion_pixel(colour, texel):
    """8D2CB9BC0F8D6BDA: squared RGB, no alpha/exposure RGB gain."""
    return [texel[i]*colour[i]*texel[i] for i in range(3)]+[texel[3]*colour[3]]

def soft_pixel(uv,world,colour,window,c,texel,guest_depth):
    sample_uv=[abs(window[i])*c[0][i+2]+c[0][i] for i in range(2)]
    clip=[2*sample_uv[0]-1,1-2*sample_uv[1],1-guest_depth,1]
    reconstructed=[dot(c[row],clip) for row in range(116,120)]
    reconstructed=[x/reconstructed[3] for x in reconstructed[:3]]
    distance2=sum((a-b)**2 for a,b in zip(world,reconstructed))*c[31][0]
    high=distance2>.5
    t=sat(2*(1-distance2 if high else distance2))
    fade=1-.5*t**4 if high else .5*t**4
    if texel[3]*colour[3]<=c[255][2]:fade=0.0
    return [x*fade for x in pixel(uv,colour,[texel],c[20][3])]
