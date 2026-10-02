#!/usr/bin/env python3
"""Compile and execute Haven, package its PPM pixels in a portable animated viewer."""
import argparse
import base64
import json
import pathlib
import struct
import subprocess
import time
import zlib


def png(width, height, pixels):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=pathlib.Path, default=root / 'src/_build/default/bin/haven.exe')
    parser.add_argument('--output', type=pathlib.Path, default=root / 'output/cube')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    executable = output / 'cube'
    subprocess.run([str(args.compiler.resolve()), '--O2', str(root / 'examples/cube/cube.hv'),
                    '-o', str(executable)], check=True)
    frames = []
    hashes = []
    import hashlib
    start = time.monotonic()
    with subprocess.Popen([str(executable)], stdout=subprocess.PIPE) as process:
        while True:
            header = process.stdout.readline()
            if not header:
                break
            if header != b'P6\n':
                raise ValueError(f'Invalid PPM header: {header!r}')
            width, height = map(int, process.stdout.readline().split())
            if process.stdout.readline() != b'255\n':
                raise ValueError('Expected 8-bit PPM')
            pixels = process.stdout.read(width * height * 3)
            if len(pixels) != width * height * 3:
                raise ValueError('Truncated frame')
            hashes.append(hashlib.sha256(pixels).hexdigest())
            data = png(width, height, pixels)
            if len(frames) in (0, 12, 24, 48, 72):
                (output / f'frame-{len(frames):03}.png').write_bytes(data)
            frames.append('data:image/png;base64,' + base64.b64encode(data).decode())
        if process.wait() != 0:
            raise RuntimeError('Haven cube exited unsuccessfully')
    elapsed = time.monotonic() - start
    if len(frames) != 96 or len(set(hashes)) < 90:
        raise ValueError(f'Expected 96 distinct rendered poses; got {len(frames)} frames and {len(set(hashes))} unique')
    template = '''<!doctype html><html lang="en"><meta charset="utf-8">
<link rel="icon" href="data:,"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Haven / Spinning cube</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#0b111c;color:#e2edf7;font:16px system-ui,sans-serif}
main{max-width:1000px;margin:60px auto;padding:0 28px}.eyebrow{color:#63d6b8;letter-spacing:.2em;font-size:12px}
h1{font-size:42px;font-weight:500;letter-spacing:-.04em;margin:16px 0 12px}p{color:#9aaac0;line-height:1.6}
.stage{background:#101d30;border:1px solid #29384b;border-radius:14px;overflow:hidden;margin-top:28px}
img{display:block;width:100%;height:auto;image-rendering:auto}footer{display:flex;align-items:center;gap:20px;padding:18px 24px;background:#101927}
button{border:1px solid #3c546b;background:#172b3e;color:#e2edf7;border-radius:6px;padding:9px 18px;font:inherit;cursor:pointer}
input{flex:1;accent-color:#63d6b8}output{color:#b4c4d6;font-variant-numeric:tabular-nums;font-size:13px;min-width:92px}
.note{font-size:13px}code{color:#b8d4ed}
</style><main><div class="eyebrow">HAVEN ENGINEERING SPIKE</div><h1>A cube, end to end.</h1>
<p>Native vectors and matrices. Homogeneous transforms. Perspective projection.<br>Depth-tested triangles, lit and rasterized by the OCaml-compiled Haven program.</p>
<div class="stage"><img id="frame" alt="A colored 3D cube rendered by Haven"><footer>
<button id="toggle">Pause</button><input id="scrub" aria-label="Frame" type="range" min="0" max="95" value="0"><output id="counter"></output>
</footer></div><p class="note">96 CPU-rendered frames · 480 × 360 · 24 fps playback<br>
<code>v * rotation_x * rotation_y * translation * perspective</code><br>The browser displays Haven's pixels; geometry and rendering run in Haven.</p></main>
<script>
const frames=__FRAMES__; const image=document.getElementById('frame'), slider=document.getElementById('scrub'),
button=document.getElementById('toggle'), counter=document.getElementById('counter');
let index=0, playing=true, previous=0;
function show(i){index=i;image.src=frames[i];slider.value=i;counter.textContent=String(i+1).padStart(2,'0')+' / 96 frames';}
function pause(){playing=false;button.textContent='Play';}
button.onclick=()=>{playing=!playing;button.textContent=playing?'Pause':'Play';};
slider.oninput=()=>{pause();show(Number(slider.value));};
function tick(time){if(playing && time-previous>=1000/24){show((index+1)%frames.length);previous=time;}requestAnimationFrame(tick);}
show(0);requestAnimationFrame(tick);
</script></html>'''
    (output / 'index.html').write_text(template.replace('__FRAMES__', json.dumps(frames)))
    metrics = {'frames': len(frames), 'unique_frames': len(set(hashes)), 'width': width, 'height': height,
               'render_and_encode_seconds': round(elapsed, 3), 'frame_sha256': hashes}
    (output / 'metrics.json').write_text(json.dumps(metrics, indent=2) + '\n')
    print(f'{len(frames)} frames, {len(set(hashes))} unique, {elapsed:.2f}s including PNG encoding')
    print(f'Open {output / "index.html"}')


if __name__ == '__main__':
    main()
