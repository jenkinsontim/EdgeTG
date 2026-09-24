"""
Controlled synthetic leaf benchmark for EdgeTG.
Identical features, order, seeds, predict→score→update protocol.
Tests: stationary, gradual sensor drift, abrupt concept reversal.
"""

import numpy as np
import time

def make_prototypes(n_features=8, n_classes=2, seed=0, separation=0.7):
    r = np.random.RandomState(seed)
    protos = []
    for c in range(n_classes):
        p = (r.rand(n_features) > (0.5 - separation/2 + c*0.1)).astype(np.uint8)
        protos.append(p)
    return np.array(protos)

def generate_stream(protos, n_steps, noise=0.08, seed=0,
                    drift='none', drift_start=None):
    r = np.random.RandomState(seed)
    n_classes, n_features = protos.shape
    if drift_start is None:
        drift_start = n_steps // 2
    cur = protos.astype(float).copy()
    stream = []
    for t in range(n_steps):
        y = r.randint(0, n_classes)
        x = cur[y].copy()
        flips = r.rand(n_features) < noise
        x[flips] = 1 - x[flips]
        x = np.clip(x, 0, 1).astype(np.uint8)

        if drift == 'gradual' and t >= drift_start // 4:
            progress = min(1.0, (t - drift_start // 4) / (n_steps * 0.6))
            target = 1.0 - protos[y]
            cur[y] = (1 - progress * 0.15) * cur[y] + (progress * 0.15) * target

        label = y
        if drift == 'abrupt' and t >= drift_start:
            label = (n_classes - 1 - y) if n_classes == 2 else (y + 1) % n_classes
        stream.append((x, label))
    return stream

class NearestCentroid:
    def __init__(self, n_features, n_classes=2):
        self.cent = np.zeros((n_classes, n_features), dtype=np.float64)
        self.cnt = np.zeros(n_classes, dtype=np.int32)
        self.memory = n_classes * n_features + n_classes * 2
        self.name = "NearestCentroid"
    def predict(self, x):
        if self.cnt.sum() == 0: return 0
        d = np.sum(np.abs(self.cent - x[None,:].astype(float)), axis=1)
        d[self.cnt == 0] = 1e9
        return int(np.argmin(d))
    def update(self, x, y):
        self.cnt[y] += 1
        self.cent[y] += (x.astype(float) - self.cent[y]) / self.cnt[y]

class Winnow:
    def __init__(self, n_features, n_classes=2):
        self.w = np.ones((n_classes, n_features), dtype=np.float64)
        self.memory = n_classes * n_features * 2
        self.name = "Winnow"
    def predict(self, x):
        return int(np.argmax(self.w @ x.astype(float)))
    def update(self, x, y):
        pred = self.predict(x)
        if pred != y:
            mask = x.astype(bool)
            self.w[y, mask] = np.minimum(self.w[y, mask] * 2.0, 1e6)
            self.w[pred, mask] = np.maximum(self.w[pred, mask] * 0.5, 1e-6)

class NaiveBayes:
    def __init__(self, n_features, n_classes=2):
        self.class_c = np.ones(n_classes, dtype=np.float64)
        self.feat_c = np.ones((n_classes, n_features), dtype=np.float64)
        self.memory = n_classes * 2 + n_classes * n_features
        self.name = "NaiveBayes"
    def predict(self, x):
        scores = np.log(self.class_c)
        for c in range(len(self.class_c)):
            for f in range(len(x)):
                if x[f]:
                    scores[c] += np.log(self.feat_c[c, f])
                else:
                    total = self.feat_c[c].sum()
                    scores[c] += np.log(max(1e-9, total - self.feat_c[c, f]))
        return int(np.argmax(scores))
    def update(self, x, y):
        self.class_c[y] += 1
        self.feat_c[y] += x.astype(float)

class MinimalTsetlin:
    def __init__(self, n_features, n_classes=2, n_clauses=2, n_states=8):
        self.n_features = n_features
        self.n_classes = n_classes
        self.n_clauses = n_clauses
        self.n_states = n_states
        total = n_classes * n_clauses * n_features * 2
        self.ta = np.full(total, n_states // 2, dtype=np.int8)
        self.memory = total
        self.name = "MinimalTsetlin"
    def _clause(self, cidx, x):
        base = cidx * self.n_features * 2
        thr = self.n_states // 2
        for f in range(self.n_features):
            if self.ta[base + f*2] > thr and not x[f]: return 0
            if self.ta[base + f*2 + 1] > thr and x[f]: return 0
        return 1
    def predict(self, x):
        scores = np.zeros(self.n_classes)
        for c in range(self.n_classes):
            for cl in range(self.n_clauses):
                scores[c] += self._clause(c * self.n_clauses + cl, x)
        return int(np.argmax(scores))
    def update(self, x, y):
        pred = self.predict(x)
        if pred == y: return
        for cl in range(self.n_clauses):
            base = (y * self.n_clauses + cl) * self.n_features * 2
            for f in range(self.n_features):
                if x[f]: self.ta[base+f*2] = min(self.ta[base+f*2]+1, self.n_states)
                else:    self.ta[base+f*2+1] = min(self.ta[base+f*2+1]+1, self.n_states)
        for cl in range(self.n_clauses):
            base = (pred * self.n_clauses + cl) * self.n_features * 2
            for f in range(self.n_features):
                if x[f]: self.ta[base+f*2] = max(self.ta[base+f*2]-1, 0)
                else:    self.ta[base+f*2+1] = max(self.ta[base+f*2+1]-1, 0)

class EWMA:
    def __init__(self, n_features, n_classes=2, alpha=0.20):
        self.alpha = alpha
        self.proto = np.full((n_classes, n_features), 0.5, dtype=np.float64)
        self.memory = n_classes * n_features
        self.name = f"EWMA(α={alpha:.2f})"
    def predict(self, x):
        d = np.sum(np.abs(self.proto - x.astype(float)[None,:]), axis=1)
        return int(np.argmin(d))
    def update(self, x, y):
        a = self.alpha
        self.proto[y] = (1-a)*self.proto[y] + a*x.astype(float)
        self.proto[y] = np.clip(self.proto[y], 0, 1)

def run_one(clf_factory, stream, n_classes):
    clf = clf_factory()
    n = len(stream)
    correct = 0
    mid = n // 2
    pre_accs, post_rolling = [], []
    recovered_90 = None
    for t, (x, y) in enumerate(stream):
        pred = clf.predict(x)
        hit = int(pred == y)
        correct += hit
        if t < mid:
            pre_accs.append(hit)
        else:
            post_rolling.append(hit)
            if len(post_rolling) > 40: post_rolling.pop(0)
            if recovered_90 is None and len(pre_accs) >= 30:
                target = 0.90 * (sum(pre_accs[-40:]) / min(40, len(pre_accs)))
                if sum(post_rolling)/len(post_rolling) >= target:
                    recovered_90 = t - mid
        clf.update(x, y)
    return correct/n, recovered_90, clf.memory

def battle(n_classes=2, n_features=8, n_steps=1500, seeds=range(8)):
    classifiers = {
        'NearestCentroid': lambda: NearestCentroid(n_features, n_classes),
        'Winnow':          lambda: Winnow(n_features, n_classes),
        'NaiveBayes':      lambda: NaiveBayes(n_features, n_classes),
        'MinimalTsetlin':  lambda: MinimalTsetlin(n_features, n_classes),
        'EWMA(0.20)':      lambda: EWMA(n_features, n_classes, alpha=0.20),
        'EWMA(0.25)':      lambda: EWMA(n_features, n_classes, alpha=0.25),
    }
    scenarios = ['none', 'gradual', 'abrupt']
    results = {name: {s: [] for s in scenarios} for name in classifiers}
    memories = {}
    print(f"Synthetic battle: {n_classes}-class, {n_features} feats, {n_steps} steps, {len(seeds)} seeds")
    print("=" * 100)
    t0 = time.time()
    for seed in seeds:
        protos = make_prototypes(n_features, n_classes, seed=seed, separation=0.75)
        for scen in scenarios:
            stream = generate_stream(protos, n_steps, noise=0.08, seed=seed+100,
                                     drift=scen if scen != 'none' else 'none')
            for name, factory in classifiers.items():
                acc, rec, mem = run_one(factory, stream, n_classes)
                results[name][scen].append((acc, rec))
                memories[name] = mem
    print(f"\n{'Classifier':<16} {'Mem':>4} | {'Stationary':>12} {'Gradual':>12} {'Abrupt':>18}")
    print("-" * 100)
    summary = {}
    for name in classifiers:
        mem = memories[name]
        row = f"{name:<16} {mem:>3}B |"
        avgs = []
        for scen in scenarios:
            accs = [a for a,r in results[name][scen]]
            mean, std = np.mean(accs), np.std(accs)
            avgs.append(mean)
            if scen == 'abrupt':
                recs = [r for a,r in results[name][scen] if r is not None]
                rec_str = f"{np.mean(recs):.0f}" if recs else "NR"
                row += f" {mean:.3f}±{std:.3f}({rec_str})"
            else:
                row += f" {mean:.3f}±{std:.3f}"
        print(row)
        summary[name] = {'avg': np.mean(avgs), 'mem': mem}
    print("\nRanking by average accuracy across scenarios:")
    for i,(n,d) in enumerate(sorted(summary.items(), key=lambda x: x[1]['avg'], reverse=True),1):
        print(f"  {i}. {n:<16} avg={d['avg']:.3f}  mem={d['mem']}B")
    print(f"\nFinished in {time.time()-t0:.1f}s")
    return results, summary

if __name__ == '__main__':
    print("=== 2-CLASS ===")
    battle(n_classes=2)
    print("\n\n=== 3-CLASS ===")
    battle(n_classes=3)
