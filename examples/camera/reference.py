"""Independent double-precision scalar camera, clipper and software-raster reference."""
import math
WIDTH, HEIGHT, NEAR = 480, 360, 1.
VERTICES = [(-1.,-1.,-1.),(1.,-1.,-1.),(1.,1.,-1.),(-1.,1.,-1.),(-1.,-1.,1.),(1.,-1.,1.),(1.,1.,1.),(-1.,1.,1.)]
INDICES = [0,2,1,0,3,2,4,5,6,4,6,7,0,4,7,0,7,3,1,2,6,1,6,5,3,7,6,3,6,2,0,1,5,0,5,4]
COLORS = [(.20,.75,.95),(.18,.90,.72),(.55,.40,.95),(.95,.45,.28),(.95,.78,.30),(.35,.55,.95)]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def sub(a,b): return [x-y for x,y in zip(a,b)]
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def unit(v): return [x/math.sqrt(dot(v,v)) for x in v]
def row(v,m): return [sum(v[i]*m[i][j] for i in range(4)) for j in range(4)]
def mul(a,b): return [row(v,b) for v in a]
def eye(angle): return [.55*math.sin(angle),.18*math.cos(angle),.2*math.sin(angle)]
def view(angle):
    origin=eye(angle); f=unit(sub([0.,.1,4.],origin)); r=unit(cross([0.,1.,0.],f)); u=cross(f,r)
    return [[r[i],u[i],f[i],0.] for i in range(3)]+[[-dot(origin,r),-dot(origin,u),-dot(origin,f),1.]]
def model(angle,scale,center):
    x=.4+.3*math.sin(angle);cx,sx=math.cos(x),math.sin(x);cy,sy=math.cos(angle),math.sin(angle)
    rx=[[1.,0.,0.,0.],[0.,cx,sx,0.],[0.,-sx,cx,0.],[0.,0.,0.,1.]]
    ry=[[cy,0.,-sy,0.],[0.,1.,0.,0.],[sy,0.,cy,0.],[0.,0.,0.,1.]]
    st=[[scale,0.,0.,0.],[0.,scale,0.,0.],[0.,0.,scale,0.],[*center,1.]]
    return mul(mul(rx,ry),st)
def clip(vertices):
    result=[]; previous=vertices[-1]
    for current in vertices:
        pi=previous[0][2]>=NEAR;ci=current[0][2]>=NEAR
        if pi != ci:
            t=(NEAR-previous[0][2])/(current[0][2]-previous[0][2])
            p=[a+(b-a)*t for a,b in zip(previous[0],current[0])];p[2]=NEAR
            color=[a+(b-a)*t for a,b in zip(previous[1],current[1])]
            result.append((p,color))
        if ci:result.append(current)
        previous=current
    return result

def fixture(kind):
    p=[[-.3,-.2,2.],[.5,-.2,3.],[0.,.6,4.]]
    if kind in (1,2,3,6):p[0][2]=.5
    if kind in (2,3,6):p[1][2]=.5
    if kind==3:p[2][2]=.5
    if kind==4:
        for v in p:v[2]=1.
    if kind==5:p[0][2],p[1][2],p[2][2]=1.,.5,2.
    if kind==6:p[2][2]=2.
    if kind==7:p[0][2],p[1][2],p[2][2]=-1.,0.,-.5
    if kind==8:p[1]=p[0].copy()
    return list(zip(p,[(1.,0.,0.),(0.,1.,0.),(0.,0.,1.)]))
def screen_vertex(x,y,z,color):return ([(x-240.)*z/270.,(180.-y)*z/270.,z],color)
def fixture_triangles(kind):
    a=screen_vertex(180.,110.,2.,(1.,0.,0.));b=screen_vertex(320.,130.,4.,(0.,1.,0.));c=screen_vertex(230.,270.,3.,(0.,0.,1.))
    d=screen_vertex(170.,100.,5.,(.9,.8,.1));e=screen_vertex(330.,140.,5.,(.9,.8,.1));f=screen_vertex(225.,280.,5.,(.9,.8,.1))
    if kind==0:return [(a,b,c),(d,e,f)]
    if kind==1:return [(d,e,f),(a,b,c)]
    if kind in (2,3,4,5):return [fixture({2:1,3:2,4:3,5:4}[kind])]
    if kind==7:return [(screen_vertex(600.,150.,2.,(1.,0.,0.)),screen_vertex(650.,150.,2.,(0.,1.,0.)),screen_vertex(600.,250.,2.,(0.,0.,1.)))]
    if kind==8:return [(a,a,b)]
    if kind==9:return [fixture(7)]
    color=(.6,.8,.2);points=[screen_vertex(x,y,2.,color) for x,y in [(200.,140.),(280.,140.),(280.,220.),(200.,220.)]]
    return [(points[0],points[1],points[2]),(points[0],points[2],points[3])]
def scene_triangles(frame):
    angle=frame*2.*math.pi/96.;camera=view(angle);result=[]
    objects=[(angle,.7,(-1.15,.1,4.6),0),(-angle+.7,.85,(1.25,.25,5.1),2),(angle*2.,.42,(-.35,-.28,1.2+.4*math.cos(angle)),4)]
    for a,scale,center,shift in objects:
        m=model(a,scale,center);mv=mul(m,camera)
        for face in range(12):
            verts=[VERTICES[INDICES[face*3+i]] for i in range(3)]
            world=[row([*v,1.],m)[:3] for v in verts]
            normal=unit(cross(sub(world[1],world[0]),sub(world[2],world[0])))
            light=.25+.75*abs(dot(normal,[.35,.65,-.675]));base=[x*light for x in COLORS[(face//2+shift)%6]]
            result.append([(row([*v,1.],mv)[:3],[base[i]*(.6+.2*v[i]) for i in range(3)]) for v in verts])
    return result

def background():return bytes(v for y in range(HEIGHT) for _ in range(WIDTH) for v in (12+y//36,20+y//24,34+y//18))
def project(v):
    p,c=v;q=1./p[2]
    return ([240.+p[0]*q*270.,180.-p[1]*q*270.,q],[x*q for x in c])
def edge(a,b,x,y):return (b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0])
def top_left(a,b):return b[1]<a[1] or (b[1]==a[1] and b[0]>a[0])
def raster(triangles):
    pixels=bytearray(background());depth=[0.]*(WIDTH*HEIGHT);safe=bytearray(WIDTH*HEIGHT);count=0
    for tri in triangles:
        poly=clip(tri)
        for i in range(1,len(poly)-1):
            verts=[project(poly[0]),project(poly[i]),project(poly[i+1])]
            a,b,c=[v[0] for v in verts];area=edge(a,b,c[0],c[1])
            if area<0.:verts[1],verts[2]=verts[2],verts[1];b,c=c,b;area=-area
            if area<=.0001:continue
            left=int(max(0.,min(a[0],b[0],c[0])));right=int(min(479.,max(a[0],b[0],c[0])+1.))
            top=int(max(0.,min(a[1],b[1],c[1])));bottom=int(min(359.,max(a[1],b[1],c[1])+1.))
            if left>right or top>bottom:continue
            inclusive=[top_left(b,c),top_left(c,a),top_left(a,b)]
            lengths=[math.hypot(c[0]-b[0],c[1]-b[1]),math.hypot(a[0]-c[0],a[1]-c[1]),math.hypot(b[0]-a[0],b[1]-a[1])]
            for y in range(top,bottom+1):
                for x in range(left,right+1):
                    es=[edge(b,c,x+.5,y+.5),edge(c,a,x+.5,y+.5),edge(a,b,x+.5,y+.5)]
                    if not all(e>0. or (e==0. and inc) for e,inc in zip(es,inclusive)):continue
                    ws=[e/area for e in es];q=sum(ws[k]*verts[k][0][2] for k in range(3));index=y*WIDTH+x
                    if q>depth[index]:
                        depth[index]=q;count+=1
                        safe[index]=int(min(e/length for e,length in zip(es,lengths))>1.)
                        for channel in range(3):
                            color=sum(ws[k]*verts[k][1][channel] for k in range(3))/q
                            pixels[index*3+channel]=int(max(0.,min(1.,color))*255.)
    return bytes(pixels),safe,count

def compare(native,expected,safe):
    assert len(native)==len(expected)==WIDTH*HEIGHT*3
    checked=0;maximum=0;bad=0
    for i,valid in enumerate(safe):
        if valid:
            checked+=1;error=max(abs(native[i*3+c]-expected[i*3+c]) for c in range(3));maximum=max(maximum,error)
            if error>1:bad+=1
    assert checked>1000
    assert bad==0, ('interior pixel reference mismatches',bad,checked,maximum)
    base=background();native_coverage=sum(native[i:i+3]!=base[i:i+3] for i in range(0,len(base),3));ref_coverage=sum(expected[i:i+3]!=base[i:i+3] for i in range(0,len(base),3))
    mismatch=sum((native[i:i+3]!=base[i:i+3])!=(expected[i:i+3]!=base[i:i+3]) for i in range(0,len(base),3))
    assert mismatch<=12,('coverage discrepancy',mismatch)
    return {'interior_pixels_checked':checked,'max_channel_error':maximum,'coverage_discrepancies':mismatch,'native_coverage':native_coverage,'reference_coverage':ref_coverage}
