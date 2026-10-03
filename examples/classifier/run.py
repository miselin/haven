#!/usr/bin/env python3
"""Compile/train Haven at every optimization level and verify a scalar Python reference."""
import argparse
import base64
import hashlib
import html
import re
import json
import math
import pathlib
import subprocess
import time
import reference

OPTS = ('Os', 'O0', 'O1', 'O2', 'O3')
SEEDS = (1337, 2024)

def close(actual, expected, tolerance, label):
    assert len(actual) == len(expected), label
    error = max(abs(a-b) for a,b in zip(actual,expected))
    assert all(math.isfinite(x) for x in actual) and error <= tolerance, (label, error, tolerance)
    return error

def parse(output):
    values = {'gradient': [], 'input_gradient': [], 'history': [], 'prediction': [], 'parameter': [], 'grid': []}
    for line in output.splitlines():
        parts = line.split(); kind = parts[0]
        if kind in values: values[kind].append([float(x) for x in parts[1:]])
        else: values[kind] = float(parts[1])
    return values

SOURCE_SECTIONS = [('backprop', 'Forward pass and backpropagation', 'Row vectors feed two dense layers. Output error propagates through the transposed weights; outer products accumulate all four samples at a batch factor of 0.25.', ('forward', 'input_gradient', 'gradient')), ('helpers', 'Vector and matrix helpers', 'These are the actual generic dense, sigmoid, reduction, transpose and outer-product helpers used by this model.', ('sum', 'ones', 'sigmoid', 'dense', 'transpose_into', 'outer_into')), ('loss', 'Stable loss and weight update', 'Binary cross-entropy is evaluated from the logit. The update subtracts the gradient with learning rate 1.', ('sample_loss', 'loss', 'initialize', 'update')), ('training', 'Gradient checks and training loop', 'The native entry point checks finite differences, performs 20,000 updates and prints losses, predictions, parameters and the decision grid.', ('main',))]

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
    here = pathlib.Path(__file__).resolve().parent
    root = here.parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=pathlib.Path, default=root/'src/_build/default/bin/haven.exe')
    parser.add_argument('--output', type=pathlib.Path, default=root/'output/classifier')
    args = parser.parse_args(); output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    profiles = []
    refs = {seed: reference.train(seed) for seed in SEEDS}
    for opt in OPTS:
        exe = output/('classifier-'+opt)
        subprocess.run([str(args.compiler.resolve()), '--'+opt, str(here/'classifier.hv'), '--Xl', '-lm', '-o', str(exe)], check=True)
        for seed in SEEDS:
            start = time.perf_counter()
            run = subprocess.run([str(exe),str(seed),'grid'], capture_output=True,text=True,check=True)
            elapsed = time.perf_counter()-start
            values = parse(run.stdout); ref = refs[seed]
            errors = {}
            errors['initial_gradient'] = close([row[1] for row in values['gradient']], ref['initial_gradient'],1e-6,'analytic gradients')
            errors['finite_difference'] = close([row[2] for row in values['gradient']], ref['initial_gradient'],3e-5,'finite differences')
            assert values['gradient_max_error'] <= 3e-5
            assert [row[0] for row in values['history']] == [row[0] for row in ref['history']]
            errors['history'] = close([row[1] for row in values['history']],[row[1] for row in ref['history']],2e-5,'training history')
            errors['prediction'] = close([row[1] for row in values['prediction']],ref['predictions'],1e-5,'XOR probabilities')
            errors['parameter'] = close([row[1] for row in values['parameter']],ref['parameters'],5e-4,'trained parameters')
            p = reference.initialize(seed); hidden, _, prediction = reference.forward(p,(.35,.65))
            d1 = [(prediction-1.)*p[9+c]*hidden[c]*(1.-hidden[c]) for c in range(3)]
            input_gradient = [sum(d1[c]*p[r*3+c] for c in range(3)) for r in range(2)]
            errors['input_gradient'] = close([row[1] for row in values['input_gradient']],input_gradient,1e-6,'input gradients / rectangular transpose')
            errors['input_finite_difference'] = close([row[2] for row in values['input_gradient']],input_gradient,3e-5,'input finite differences')
            expected_grid = [reference.forward(ref['parameters'],(-.25+x*.025,-.25+y*.025))[2] for y in range(61) for x in range(61)]
            assert [(int(row[0]),int(row[1])) for row in values['grid']] == [(x,y) for y in range(61) for x in range(61)]
            errors['decision_grid'] = close([row[2] for row in values['grid']],expected_grid,2e-4,'decision grid')
            assert all(0. <= row[2] <= 1. for row in values['grid'])
            assert values['history'][-1][1] < .001
            assert all(abs(row[1]-label)<.01 for row,label in zip(values['prediction'],reference.LABELS))
            profiles.append({'seed':seed,'opt':opt,'run_seconds':elapsed,'errors':errors,'values':values,'reference':ref})
            (output/f'{seed}-{opt}.txt').write_text(run.stdout)
            print(f'{seed}/{opt}: loss {values["history"][-1][1]:.7f}, gradient error {values["gradient_max_error"]:.2g}, references pass', flush=True)
    for seed in SEEDS:
        selected = [p for p in profiles if p['seed']==seed]
        for p in selected:
            close([r[1] for r in p['values']['history']],[r[1] for r in selected[0]['values']['history']],1e-6,'optimization agreement')
    report = {'architecture':'2 -> 3 -> 1 sigmoid, full-batch binary cross-entropy', 'epochs':reference.EPOCHS,'learning_rate':reference.RATE,'profiles':profiles}
    (output/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    (output/'index.html').write_text(HTML.replace('__DATA__',json.dumps(profiles,separators=(',',':'))).replace('__SOURCE__', source_html(here/'classifier.hv', SOURCE_SECTIONS)))
    print('All 10 training profiles pass. Open',output/'index.html')

HTML = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" href="data:,"><title>Haven / Learning XOR</title>
<style>*{box-sizing:border-box}body{margin:0;background:#0b111c;color:#e3ecf5;font:15px system-ui}main{max-width:1120px;margin:48px auto;padding:0 28px}.eyebrow{color:#69dcc2;letter-spacing:.16em;font-size:12px}h1{font-size:44px;letter-spacing:-.04em;font-weight:500;margin:16px 0}p{color:#a2b3c6;line-height:1.6;max-width:780px}.controls{display:flex;gap:24px;align-items:center;margin:28px 0}select{font:inherit;background:#172638;color:#e3ecf5;border:1px solid #496076;border-radius:5px;padding:7px 14px;margin-left:8px}.figures{display:grid;grid-template-columns:1fr 1fr;gap:34px}figure{margin:0}h2{font-size:18px;font-weight:500}canvas,svg{width:100%;height:auto;background:#101c2b;border-radius:8px}figcaption{color:#9dafc3;font-size:13px;line-height:1.6;margin-top:12px}.metrics{display:flex;gap:35px;margin:28px 0;font-variant-numeric:tabular-nums}.metrics strong{display:block;font-size:24px;font-weight:500;color:#78e3c7}.metrics span{color:#a2b3c6;font-size:13px}.detail{font-size:13px}@media(max-width:760px){.figures{grid-template-columns:1fr}.metrics{flex-wrap:wrap}}
a{color:#78e3c7;text-underline-offset:3px}a:focus-visible,summary:focus-visible,pre:focus-visible{outline:2px solid #78e3c7;outline-offset:4px}.page-nav{margin:20px 0;font-size:14px}.pipeline,.source-section{margin-top:40px;scroll-margin-top:24px}.pipeline ol{color:#b9c9d9;line-height:1.7;padding-left:24px}.pipeline li{padding:4px 0}.source-nav{display:flex;flex-wrap:wrap;gap:6px 10px;line-height:1.8;font-size:14px;margin:20px 0}.source-card{border:1px solid #2c4057;border-radius:8px;background:#101927;margin:12px 0;scroll-margin-top:24px}.source-card summary{cursor:pointer;padding:16px 20px;font-weight:500;color:#dce9f6}.source-card[open] summary{border-bottom:1px solid #2c4057}.source-content{padding:0 20px 18px;min-width:0}.source-content p{font-size:14px}.source-location{font-size:12px!important;color:#92a9c1!important;margin:20px 0 8px}pre{background:#0b1320;border:1px solid #23384e;border-radius:6px;padding:18px;overflow:auto;max-height:32rem;margin:10px 0;tab-size:4}pre code{font:13px/1.7 ui-monospace,SFMono-Regular,Consolas,monospace;color:#d5e8f8;white-space:pre}.source-hash{overflow-wrap:anywhere;font-size:11px}h1{font-size:clamp(30px,5vw,44px)}@media(max-width:640px){main{padding:0 16px;margin:28px auto}.controls,.stats{flex-wrap:wrap;gap:16px}footer{flex-wrap:wrap;gap:12px;padding:12px}footer input{min-width:130px}.source-content{padding:0 12px 12px}pre{padding:12px}.source-nav{display:block}}</style><main><div class="eyebrow">HAVEN / TRAINABLE CLASSIFIER</div><h1>Learning a nonlinear boundary.</h1><nav class="page-nav" aria-label="Page sections"><a href="#pipeline">How it works</a> · <a href="#source">Haven source</a></nav><p>A tiny 2 → 3 → 1 network learns XOR using explicit backpropagation in Haven. Rectangular transposes, outer products, reductions and mutable weights use the existing vector and matrix syntax.</p><div class="controls"><label>Seed<select id="seed"><option>1337</option><option>2024</option></select></label><label>Optimization<select id="opt"><option>O2</option><option>Os</option><option>O0</option><option>O1</option><option>O3</option></select></label></div>
<div class="figures"><figure><h2>Binary cross-entropy</h2><svg id="loss" viewBox="0 0 480 420" role="img" aria-label="Loss decreases over 20000 training epochs"></svg><figcaption>Green: native Haven. Dashed white: independent scalar Python reference. The logarithmic scale makes late convergence visible.</figcaption></figure><figure><h2>Learned probability of class 1</h2><canvas id="map" width="480" height="420" aria-label="Learned XOR decision boundary and four labeled training points"></canvas><figcaption>Blue = class 0, green = class 1. The four outlined points are the XOR training examples. Every grid sample comes from the trained Haven executable.</figcaption></figure></div><div class="metrics"><div><strong id="final"></strong><span>final loss</span></div><div><strong id="gradient"></strong><span>largest finite-difference error</span></div><div><strong>10 / 10</strong><span>training profiles verified</span></div></div><p class="detail">20,000 full-batch epochs · learning rate 1 · deterministic 32-bit LCG initialization<br>All 13 parameter gradients and both input gradients checked numerically. Two seeds × Os/O0/O1/O2/O3 agree with an independent double-precision reference. This verifies this small model and these inputs; it is not a general ML runtime or a performance benchmark.</p><section id="pipeline" class="pipeline" aria-labelledby="pipeline-heading"><h2 id="pipeline-heading">How this viewer works</h2><ol><li><strong>Haven computes.</strong> The OCaml Haven compiler lowers classifier.hv through LLVM, emits a native object and links a native executable. That executable trains the model and prints losses, gradients, predictions and a 61 × 61 decision grid.</li><li><strong>Python verifies and packages.</strong> run.py executes two seeds at five optimization levels, compares their results with an independent scalar Python reference, and embeds the native output and reference data in this file.</li><li><strong>The browser draws the results.</strong> JavaScript chooses an already-computed profile and draws the loss plot with SVG and the decision grid with Canvas 2D. Training does not rerun in the browser; this file contains no WebAssembly.</li></ol></section>__SOURCE__</main>
<script>const profiles=__DATA__,seed=document.getElementById('seed'),opt=document.getElementById('opt');
function draw(){const p=profiles.find(p=>p.seed==+seed.value&&p.opt==opt.value),v=p.values;document.getElementById('final').textContent=v.history.at(-1)[1].toFixed(6);document.getElementById('gradient').textContent=v.gradient_max_error.toExponential(2);
const sx=e=>60+e/20000*390,sy=l=>35+(Math.log10(.8)-Math.log10(Math.max(l,.0002)))/(Math.log10(.8)-Math.log10(.0002))*320;
let content='';for(const l of [.1,.01,.001]){const y=sy(l);content+=`<line x1="60" x2="450" y1="${y}" y2="${y}" stroke="#28384a"/><text x="12" y="${y+4}" fill="#97abc2" font-size="12">${l}</text>`}for(const e of [0,10000,20000])content+=`<text x="${sx(e)}" y="388" text-anchor="middle" fill="#97abc2" font-size="12">${e}</text>`;
const path=points=>points.map((r,i)=>(i?'L':'M')+sx(r[0])+','+sy(r[1])).join(' ');content+=`<path d="${path(p.reference.history)}" fill="none" stroke="#edf5fb" stroke-dasharray="7 5" stroke-width="2"/><path d="${path(v.history)}" fill="none" stroke="#69dcc2" stroke-width="3"/>`;for(const r of v.history)content+=`<circle cx="${sx(r[0])}" cy="${sy(r[1])}" r="4" fill="#69dcc2"/>`;document.getElementById('loss').innerHTML=content;
const canvas=document.getElementById('map'),ctx=canvas.getContext('2d'),left=55,top=20,w=365,h=345;ctx.clearRect(0,0,480,420);for(const r of v.grid){const a=r[2];ctx.fillStyle=`rgb(${Math.round(32+40*a)},${Math.round(72+120*a)},${Math.round(156-38*a)})`;ctx.fillRect(left+r[0]/61*w,top+(60-r[1])/61*h,w/61+1,h/61+1)};
const px=x=>left+(x+.25)/1.5*w,py=y=>top+(1.25-y)/1.5*h;ctx.font='13px system-ui';ctx.textAlign='center';for(const [x,y,label] of [[0,0,0],[0,1,1],[1,0,1],[1,1,0]]){ctx.beginPath();ctx.arc(px(x),py(y),12,0,Math.PI*2);ctx.fillStyle='#0b111c';ctx.fill();ctx.strokeStyle='#edf5fb';ctx.lineWidth=2;ctx.stroke();ctx.fillStyle='#edf5fb';ctx.fillText(label,px(x),py(y)+4)}for(const t of [0,1]){ctx.fillStyle='#a2b3c6';ctx.fillText(t,px(t),388);ctx.fillText(t,35,py(t)+4)}ctx.fillText('input x',left+w/2,412);ctx.save();ctx.translate(14,top+h/2);ctx.rotate(-Math.PI/2);ctx.fillText('input y',0,0);ctx.restore()}
seed.onchange=opt.onchange=draw;draw();function revealSource(){const node=document.getElementById(decodeURIComponent(location.hash.slice(1)));if(node&&node.tagName==='DETAILS')node.open=true}window.addEventListener('hashchange',revealSource);revealSource();</script></html>'''
if __name__ == '__main__': main()
