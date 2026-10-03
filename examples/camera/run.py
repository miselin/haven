#!/usr/bin/env python3
"""Verify native camera/clipping math and pixels, then package an offline animation."""
import argparse, base64, hashlib, io, json, math, pathlib, struct, subprocess, zlib
import html, re
import reference
OPTS=('Os','O0','O1','O2','O3')

def png(width,height,pixels):
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    rows=b''.join(b'\0'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')
def ppm(stream):
    while True:
        magic=stream.readline()
        if not magic:break
        assert magic==b'P6\n',magic
        comment=stream.readline().decode().strip().split();assert comment[0]=='#'
        stats={comment[i]:int(comment[i+1]) for i in range(1,len(comment),2)}
        width,height=map(int,stream.readline().split());assert (width,height)==(480,360)
        assert stream.readline()==b'255\n'
        pixels=stream.read(width*height*3);assert len(pixels)==width*height*3
        assert stats['invalid']==0
        yield stats,pixels

def check_math(exe):
    output=subprocess.check_output([str(exe),'1'],text=True);counts={};vertices={};cameras=[]
    for line in output.splitlines():
        p=line.split()
        if p[0]=='clip':counts[int(p[1])]=int(p[2]);vertices[int(p[1])]=[]
        elif p[0]=='vertex':vertices[int(p[1])].append(list(map(float,p[3:])))
        elif p[0]=='camera':cameras.append(list(map(float,p[1:])))
    maximum=0.
    for kind in range(9):
        expected=reference.clip(reference.fixture(kind));assert counts[kind]==len(expected)
        for native,(position,color) in zip(vertices[kind],expected):
            error=max(abs(a-b) for a,b in zip(native,[*position,*color]));assert error<=2e-6;maximum=max(maximum,error)
    assert len(cameras)==96
    for row in cameras:
        assert all(math.isfinite(v) for v in row)
        frame=int(row[0]);origin=reference.eye(frame*2.*math.pi/96.);expected=math.sqrt(reference.dot(reference.sub([0.,.1,4.],origin),reference.sub([0.,.1,4.],origin)))
        assert max(abs(x) for x in row[1:6])<=2e-6 and abs(row[6]-expected)<=2e-6
    return {'clip_cases':9,'camera_poses':96,'max_clip_attribute_error':maximum}

SOURCE_SECTIONS = [('transforms', 'Camera and model transforms', 'Haven uses row vectors: vertex * model * view. The look-at matrix maps the camera eye to the origin and its target onto positive camera Z.', ('dot', 'cross', 'unit', 'eye', 'view', 'model')), ('clipping', 'Near-plane clipping and projection', 'Camera-space edges crossing z = 1 produce interpolated vertices. Perspective division follows clipping; each projected vertex stores reciprocal depth and color divided by depth.', ('intersection', 'clip', 'project')), ('raster', 'Pixel coverage, depth and color', 'Triangle edge functions apply a top-left coverage rule. Barycentric weights interpolate 1/z and color/z; the nearest reciprocal depth wins and color is recovered by division.', ('edge', 'top_left', 'covered', 'triangle')), ('scene', 'Clipped triangles and scene assembly', 'The clipper fans each retained polygon into triangles. Three transformed cube meshes feed the software rasterizer.', ('clip_draw', 'object', 'render')), ('output', 'Native framebuffer and frame output', 'The native executable allocates the framebuffer, renders and validates each frame, and writes its RGB bytes to stdout as a P6 image. Python packages those existing pixels.', ('clear', 'validate_depth', 'emit', 'main'))]

def source_html(path, sections):
    """Embed exact current source, with named excerpts and an offline full-file download."""
    raw = path.read_bytes()
    lines = raw.decode('utf-8').splitlines(keepends=True)
    declarations = [(i, re.match(r'^(?:(?:pub|impure) )*fn (\w+)\b', line))
                    for i, line in enumerate(lines)]
    functions = {}
    for i, match in declarations:
        if match:
            end = next((j for j in range(i + 1, len(lines))
                        if re.match(r'^(?:(?:pub|impure) )*fn |^type |^data |^cimport ', lines[j])), len(lines))
            functions[match.group(1)] = (i, end)
    def code(text):
        return '<pre tabindex="0" aria-label="Haven source code"><code>' + html.escape(text, quote=False) + '</code></pre>'
    navigation = ' · '.join(f'<a href="#source-{key}">{html.escape(title)}</a>' for key, title, _, _ in sections)
    cards = []
    for index, (key, title, description, names) in enumerate(sections):
        excerpts = []
        for name in names:
            start, end = functions[name]  # Missing/renamed functions fail generation instead of showing stale code.
            while end > start and not lines[end - 1].strip():
                end -= 1
            excerpts.append(f'<p class="source-location">{path.name} · lines {start + 1}–{end}</p>' + code(''.join(lines[start:end])))
        cards.append(f'<details id="source-{key}" class="source-card"' + (' open' if index == 0 else '') +
                     f'><summary>{html.escape(title)}</summary><div class="source-content"><p>{html.escape(description)}</p>' + ''.join(excerpts) + '</div></details>')
    types = ''.join(line for line in lines if line.startswith('type '))
    digest = hashlib.sha256(raw).hexdigest()
    download = base64.b64encode(raw).decode('ascii')
    return ('<section id="source" class="source-section" aria-labelledby="source-heading">'
            '<h2 id="source-heading">Relevant Haven source</h2>'
            '<p>These excerpts come directly from the Haven file used by this runner. Expand a section to read the implementation.</p>'
            f'<p class="syntax-note"><strong>Reading the syntax:</strong> <code>fn … = expression;</code> returns an ordinary eagerly evaluated expression, exactly like a braced result body. It introduces no lazy values or deferred calls. The compiler gives eligible single-expression bodies an advisory LLVM inline hint, including equivalent braced bodies; the optimizer can decline it and symbols can remain when their addresses are needed. <code>iter each value of source indexed by index</code> copies its aggregate source once, visits vector lanes or matrix rows in ascending order, and binds immutable values plus an optional zero-based <code>u32</code> index. The index clause is optional. Numeric ranges use <code>iter each i of start:end[:step]</code> and retain their inclusive bounds and <code>i32</code> counter. Old spellings remain accepted; the formatter emits this sentence form. Use <code>map each value of source {{ expression }}</code> to eagerly create a new aggregate of the same shape and element type; matrices map rows and nested maps transform scalar cells. The source is evaluated once and captured, and bodies run sequentially. The optional index clause is the same as iteration. <code>let fvec3 v = fill 1.0;</code> replicates a scalar evaluated once into a contextually typed vector or matrix. These forms do not introduce in-place updates or allocator changes. <code>fold each value of source with acc = seed {{ expression }}</code> snapshots the source first, evaluates the seed once, then uses each body result as the next accumulator in ascending order. Empty arrays return the seed. Bindings are immutable; fold bodies reject break, continue and ret.</p><nav class="source-nav" aria-label="Source sections">{navigation}</nav>' +
            '<details class="source-card"><summary>Data types</summary><div class="source-content">' + code(types) + '</div></details>' +
            ''.join(cards) + '<details id="source-complete" class="source-card"><summary>Complete Haven file</summary><div class="source-content">' +
            code(raw.decode('utf-8')) + '</div></details>' +
            f'<p class="source-location"><a download="{path.name}" href="data:text/plain;base64,{download}">Download {path.name}</a>'
            f'<br>Source SHA-256: <code class="source-hash">{digest}</code></p></section>')

def main():
    here=pathlib.Path(__file__).resolve().parent;root=here.parents[1];parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=pathlib.Path,default=root/'src/_build/default/bin/haven.exe');parser.add_argument('--output',type=pathlib.Path,default=root/'output/camera');args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    fixtures=[reference.raster(reference.fixture_triangles(kind)) for kind in range(10)]
    reports=[];executables={};fixture_hashes=None
    for opt in OPTS:
        exe=output/('camera-'+opt);executables[opt]=exe
        subprocess.run([str(args.compiler.resolve()),'--'+opt,str(here/'camera.hv'), '--Xl', '-lm','-o',str(exe)],check=True)
        math_report=check_math(exe);frames=list(ppm(io.BytesIO(subprocess.check_output([str(exe),'2']))));assert len(frames)==10
        assert frames[0][1]==frames[1][1], 'depth must be independent of draw order'
        assert all(frames[k][1]==reference.background() and frames[k][0]['writes']==0 for k in (4,7,8,9))
        assert frames[6][0]['writes']==6400, ('shared-edge coverage',frames[6][0])
        comparisons=[]
        for kind,((stats,pixels),(expected,safe,_)) in enumerate(zip(frames,fixtures)):
            if kind in (4,7,8,9):comparison={'empty_coverage':'background only'}
            else:comparison=reference.compare(pixels,expected,safe)
            comparisons.append({'fixture':kind,'stats':stats,'reference':comparison})
            if opt=='O2':(output/f'fixture-{kind}.png').write_bytes(png(480,360,pixels))
        hashes=[hashlib.sha256(pixels).hexdigest() for _,pixels in frames]
        if fixture_hashes is None:fixture_hashes=hashes
        assert hashes==fixture_hashes, 'raster fixtures must agree across optimization levels'
        reports.append({'opt':opt,'math':math_report,'fixtures':comparisons,'fixture_sha256':hashes})
        print(opt+': nine clipping cases, 96 camera poses, depth/order/edge and pixel references pass',flush=True)
    frames=[];stats_list=[];hashes=[];scene_reports=[]
    with subprocess.Popen([str(executables['O2'])],stdout=subprocess.PIPE) as process:
        for stats,pixels in ppm(process.stdout):
            frame=stats['frame'];stats_list.append(stats);hashes.append(hashlib.sha256(pixels).hexdigest());data=png(480,360,pixels)
            frames.append('data:image/png;base64,'+base64.b64encode(data).decode())
            if frame in (0,24,48,72):
                (output/f'frame-{frame:03}.png').write_bytes(data)
                expected,safe,_=reference.raster(reference.scene_triangles(frame));comparison=reference.compare(pixels,expected,safe);scene_reports.append({'frame':frame,**comparison});(output/f'reference-{frame:03}.png').write_bytes(png(480,360,expected))
        assert process.wait()==0
    assert len(frames)==96 and len(set(hashes))==96
    assert any(s['clipped']>0 for s in stats_list) and any(s['quads']>0 for s in stats_list) and any(s['rejected']>0 for s in stats_list)
    report={'compiler':str(args.compiler.resolve()),'optimization_fixtures':reports,'scene_stats':stats_list,'scene_reference':scene_reports,'frames':96,'unique_frames':len(set(hashes)),'frame_sha256':hashes}
    (output/'verification.json').write_text(json.dumps(report,indent=2)+'\n');(output/'index.html').write_text(HTML.replace('__FRAMES__',json.dumps(frames)).replace('__STATS__',json.dumps(stats_list)).replace('__SOURCE__', source_html(here/'camera.hv', SOURCE_SECTIONS)))
    print('96 unique scene frames pass; selected full scenes match scalar pixel references. Open',output/'index.html')

HTML='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" href="data:,"><title>Haven / Camera and clipping</title><style>*{box-sizing:border-box}body{margin:0;background:#0b111c;color:#e3ecf5;font:15px system-ui}main{max-width:1040px;margin:44px auto;padding:0 28px}.eyebrow{color:#69dcc2;font-size:12px;letter-spacing:.16em}h1{font-size:44px;font-weight:500;letter-spacing:-.04em;margin:16px 0}p{color:#a2b3c6;line-height:1.6}img{display:block;width:100%;height:auto}.stage{background:#101c2b;border:1px solid #283a50;border-radius:9px;overflow:hidden;margin:25px 0}footer{display:flex;align-items:center;gap:20px;padding:16px 20px;background:#101927}button{background:#172638;border:1px solid #496076;border-radius:5px;color:#e3ecf5;font:inherit;padding:8px 17px}input{flex:1;accent-color:#69dcc2}output{font-variant-numeric:tabular-nums;color:#a9bfd2;font-size:13px}.stats{display:flex;gap:30px;margin:22px 0}.stats strong{display:block;color:#78e3c7;font-size:23px;font-weight:500}.stats span{font-size:13px;color:#a2b3c6}.note{font-size:13px}a{color:#78e3c7;text-underline-offset:3px}a:focus-visible,summary:focus-visible,pre:focus-visible{outline:2px solid #78e3c7;outline-offset:4px}.page-nav{margin:20px 0;font-size:14px}.pipeline,.source-section{margin-top:40px;scroll-margin-top:24px}.pipeline ol{color:#b9c9d9;line-height:1.7;padding-left:24px}.pipeline li{padding:4px 0}.source-nav{display:flex;flex-wrap:wrap;gap:6px 10px;line-height:1.8;font-size:14px;margin:20px 0}.source-card{border:1px solid #2c4057;border-radius:8px;background:#101927;margin:12px 0;scroll-margin-top:24px}.source-card summary{cursor:pointer;padding:16px 20px;font-weight:500;color:#dce9f6}.source-card[open] summary{border-bottom:1px solid #2c4057}.source-content{padding:0 20px 18px;min-width:0}.source-content p{font-size:14px}.source-location{font-size:12px!important;color:#92a9c1!important;margin:20px 0 8px}pre{background:#0b1320;border:1px solid #23384e;border-radius:6px;padding:18px;overflow:auto;max-height:32rem;margin:10px 0;tab-size:4}pre code{font:13px/1.7 ui-monospace,SFMono-Regular,Consolas,monospace;color:#d5e8f8;white-space:pre}.source-hash{overflow-wrap:anywhere;font-size:11px}h1{font-size:clamp(30px,5vw,44px)}@media(max-width:640px){main{padding:0 16px;margin:28px auto}.controls,.stats{flex-wrap:wrap;gap:16px}footer{flex-wrap:wrap;gap:12px;padding:12px}footer input{min-width:130px}.source-content{padding:0 12px 12px}pre{padding:12px}.source-nav{display:block}}</style><main><div class="eyebrow">HAVEN / SOFTWARE 3D</div><h1>A moving camera, up close.</h1><nav class="page-nav" aria-label="Page sections"><a href="#pipeline">How it works</a> · <a href="#source">Haven source</a></nav><p>Three rotating objects, a moving look-at camera, and a foreground cube crossing the near plane.<br>Haven clips geometry before perspective division and interpolates vertex color with reciprocal depth.</p><div class="stage"><img id="frame" alt="Three colored cubes with a moving camera and near-plane clipping"><footer><button id="toggle">Pause</button><input id="scrub" aria-label="Frame" type="range" min="0" max="95" value="0"><output id="counter"></output></footer></div><div class="stats"><div><strong id="clipped"></strong><span>triangles clipped or rejected</span></div><div><strong id="quads"></strong><span>clipped quadrilaterals</span></div><div><strong id="rejected"></strong><span>fully rejected triangles</span></div></div><p class="note">96 native CPU-rendered frames · 480 × 360 · 24 fps playback<br>Each optimization level passes clipping, camera, perspective-color, depth draw-order and shared-edge checks. Four scene frames also match an independent scalar rasterizer at interior pixels. This demo clips the near plane; it has no far-plane clip, antialiasing or mesh capping.</p><section id="pipeline" class="pipeline" aria-labelledby="pipeline-heading"><h2 id="pipeline-heading">How the cubes reach the screen</h2><ol><li><strong>Haven renders on the CPU.</strong> The OCaml Haven compiler lowers camera.hv through LLVM, emits a native object and links a native executable. The executable transforms and clips triangles, rasterizes their coverage, tests depth and writes every RGB pixel into a 480 × 360 framebuffer. It emits 96 P6 image frames to stdout.</li><li><strong>Python verifies and packages.</strong> run.py checks the native output against an independent scalar reference, converts the emitted RGB bytes to PNGs with Python’s standard library and embeds all 96 PNGs as data URLs in this file.</li><li><strong>The browser plays the images.</strong> JavaScript changes an &lt;img&gt; element’s source to select an embedded PNG, updates the counters and advances playback at a target 24 fps. The browser decodes and displays those images; it does not recompute the 3D scene. This viewer uses neither WebAssembly nor WebGL.</li></ol></section>__SOURCE__</main><script>
const frames=__FRAMES__,stats=__STATS__,img=document.getElementById('frame'),slider=document.getElementById('scrub'),button=document.getElementById('toggle');let current=+(new URLSearchParams(location.search).get('frame')||0),playing=!new URLSearchParams(location.search).has('frame');function show(){img.src=frames[current];slider.value=current;document.getElementById('counter').textContent=(current+1)+' / 96';for(const k of ['clipped','quads','rejected'])document.getElementById(k).textContent=stats[current][k];button.textContent=playing?'Pause':'Play'}button.onclick=()=>{playing=!playing;show()};slider.oninput=()=>{playing=false;current=+slider.value;show()};setInterval(()=>{if(playing){current=(current+1)%96;show()}},1000/24);show();function revealSource(){const node=document.getElementById(decodeURIComponent(location.hash.slice(1)));if(node&&node.tagName==='DETAILS')node.open=true}window.addEventListener('hashchange',revealSource);revealSource();</script></html>'''
if __name__=='__main__':main()
