"""Closed weather grid, capsule avoidance and atlas formulas (19A01C01/718C12FF).

The eight capsules act on the jittered cell position, before flutter and wind.
No vertex stream or runtime guest instruction execution is involved.
"""
import math

VS='19A01C01290E20A7'
PS='718C12FF9E1B888B'
LITERALS=[[-3.14159274,10,6.28318548,8],[2,1,.800000012,1.5915494],
          [-5,-8,-.5,.499999583],[.5,-1,.300000012,5],[.600000024,.899999976,.400000006,0],
          [87.4400024,71.6500015,76.8499985,-1],[1.41421354,3.14159274,2.71828175,.100000001],
          [17.0100002,13.5100002,56.0999985,5],[87.2300034,51.9399986,46.8699989,.25],
          [7.69999981,5.5,6.5999999,6.28318024],[.130999997,.310000002,.151999995,1.5],
          [.5,732.050781,236.067978,645.751282]]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def add(a,b):return [x+y for x,y in zip(a,b)]
def sub(a,b):return [x-y for x,y in zip(a,b)]
def mul(a,b):return [x*b for x in a]
def frac(x):return x-math.floor(x)
def sat(x):return min(max(x,0),1)
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def norm(a):
    length=math.sqrt(dot(a,a))
    return mul(a,1/length) if length else [0,0,0]

def capsule(p,h,phase,time,a,b,inverse_radius,c):
    axis=sub(b[:3],a[:3])
    length2=dot(axis,axis)
    projection=sat(dot(sub(p,a[:3]),axis)/length2) if length2 else 0
    offset=sub(add(a[:3],mul(axis,projection)),p)
    weight=sat(1-math.sqrt(dot(offset,offset))*inverse_radius)
    age=time-(a[3]+(b[3]-a[3])*projection)
    if weight<=0 or age<=0 or age>=phase:return [0,0,0]
    random_axis=sub(mul(h,2),[1,1,1])
    tangent=norm(cross(axis,random_axis))
    radial=norm(cross(tangent,random_axis))
    advance=age*(h[0]+.5)
    angle=frac((advance+h[1]*c[253][3]+h[0])*c[245][3]+.5)*c[244][2]+c[244][0]
    fade=sat(1-(advance-c[248][1])**2)
    advance2=advance*advance
    inward=mul(offset,advance2/(advance2+c[247][2]))
    axial=mul(norm(axis),c[247][3]*advance2/(advance2+.5))
    orbit=mul(sub(mul(tangent,math.sin(angle)),mul(radial,math.cos(angle))),c[247][2]*fade**4)
    return mul(add(add(inward,axial),orbit),weight)

def vertex(index,c):
    n=index+.5
    z=math.floor(n/(c[46][0]*c[46][1]))
    rem=n-z*c[46][0]*c[46][1]
    y=math.floor(rem/c[46][0]);x=math.floor(rem-y*c[46][0])
    cell=add(mul([x,y,z],c[46][3]),c[8][:3])
    hash_=frac(dot([cell[i]*cell[i] for i in (2,0,1)],c[250][:3]))
    phase=frac(c[47][0]*c[250][3]+hash_)*c[244][1]
    h=[frac(hash_*v) for v in c[255][1:]]
    p=add(cell,sub(h,[.5]*3))
    alpha=phase*.5 if phase<2 else 1-(phase-8)*.5 if phase>8 else 1
    size_random=frac(dot([h[2],h[0],h[1]],c[253][:3]))
    flutter=[2*frac((phase+h[j])*c[254][i])-1 for i,j in enumerate((1,0,2))]
    cubic=flutter[0]**3-flutter[0]
    sx=c[245][2]*c[75][3]*cubic*(1.5-size_random)
    sy=c[248][1]*(flutter[2]**3-flutter[2])*c[75][3]*(1.5-size_random)
    sz=c[248][0]*(1-(flutter[1]**3-flutter[1]+c[248][2])**2)*c[75][3]*(1.5-size_random)
    world=add(p,[sx+c[144][0]*(phase-5),sy+c[144][1]*(phase-5),sz-c[144][2]*(phase-5)*c[75][3]])
    for i in range(8):
        world=add(world,capsule(p,h,phase,c[47][0],c[128+2*i],c[129+2*i],c[73+i//4][i%4],c))
    for first in (113,118,123):
        if sum(sat(dot(c[j],world+[1])) for j in range(first,first+5))==0:world=c[72][:3]
    clip=[dot(c[j],world+[1]) for j in range(4)]
    hashes=[frac(dot([h[2],h[0],h[1]],c[j][:3])) for j in (251,252,249)]
    angle=(hashes[1]*c[246][3]+.5)*c[244][2]+c[244][0]
    s=math.sin(angle)*.25;co=math.cos(angle)*.25
    fade=sat(1-(math.sqrt(dot(sub(world,c[72][:3]),sub(world,c[72][:3])))-c[75][0])/(c[75][1]-c[75][0]))
    return {'oPos':clip,'oPts':[c[75][2]*size_random/clip[3],0,0,0],
            'o0':[fade*alpha,0,0,0],'o1':[math.floor(hashes[0]*2)*.5+.25,math.floor(hashes[1]*2)*.5+.25,0,0],
            'o2':[s,co,0,0],'o3':[-co,s,0,0] if hashes[2]>.5 else [co,-s,0,0]}

def pixel(atlas,basis_x,basis_y,point,texel,fade,exposure,tint):
    uv=[atlas[i]+(2*abs(point[0])-1)*basis_x[i]+(2*abs(point[1])-1)*basis_y[i] for i in range(2)]
    return uv,[texel[i]*fade*exposure*tint[i] for i in range(3)]+[1]
