"""Independent scalar, double-precision reference for the Haven XOR classifier."""
import math
INPUTS = [(0., 0.), (0., 1.), (1., 0.), (1., 1.)]
LABELS = [0., 1., 1., 0.]
SEED = 1337
EPOCHS = 20000
RATE = 1.

def initialize(seed=SEED):
    values = []
    for _ in range(13):
        seed = (seed * 1664525 + 1013904223) & 0xffffffff
        values.append((seed % 65536) / 32768. - 1.)
    return values

def sigmoid(x):
    return 1. / (1. + math.exp(-x))

def forward(p, x):
    hidden = [sigmoid(sum(x[r]*p[r*3+c] for r in range(2)) + p[6+c]) for c in range(3)]
    logit = sum(hidden[c]*p[9+c] for c in range(3)) + p[12]
    return hidden, logit, sigmoid(logit)

def loss(p):
    total = 0.
    for x, y in zip(INPUTS, LABELS):
        _, z, _ = forward(p, x)
        total += max(z, 0.) + math.log1p(math.exp(-abs(z))) - y*z
    return total / 4.

def gradient(p):
    g = [0.] * 13
    for x, target in zip(INPUTS, LABELS):
        hidden, _, output = forward(p, x)
        d2 = output-target
        for c in range(3):
            d1 = d2*p[9+c]*hidden[c]*(1.-hidden[c])
            for r in range(2): g[r*3+c] += x[r]*d1/4.
            g[6+c] += d1/4.
            g[9+c] += hidden[c]*d2/4.
        g[12] += d2/4.
    return g

def finite_difference(p, step=1e-5):
    values = []
    for i in range(13):
        plus = p.copy(); minus = p.copy()
        plus[i] += step; minus[i] -= step
        values.append((loss(plus)-loss(minus))/(2.*step))
    return values

def train(seed=SEED):
    p = initialize(seed)
    g = gradient(p); finite = finite_difference(p)
    assert max(abs(a-b) for a,b in zip(g, finite)) < 1e-8
    history = [(0, loss(p))]
    for epoch in range(1, EPOCHS+1):
        p = [v-RATE*d for v,d in zip(p, gradient(p))]
        if epoch in (1000, 5000, 10000, 20000): history.append((epoch, loss(p)))
    return {'seed':seed, 'initial_gradient':g, 'finite_difference':finite,
            'history':history, 'parameters':p,
            'predictions':[forward(p,x)[2] for x in INPUTS]}
if __name__ == '__main__':
    import json
    for seed in (1337, 2024): print(json.dumps(train(seed), indent=2))
