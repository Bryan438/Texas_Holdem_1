"""Poker tables for training, run by the C++ program ../thenv.

Several thenv processes run side by side (one per CPU core works well), each
with a few tables. To the trainer they look like one batch of tables: every
step takes one action per table and gives back what each learner sees next.
"""
import os
import subprocess

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
THENV = os.path.join(HERE, "..", "thenv")


class PokerVecEnv:
    def __init__(self, num_envs, num_procs=8, seed=1, opponents="equity", players="2", model=None, pool=None,
                 binary=THENV):
        if not os.path.exists(binary):
            raise SystemExit(f"{binary} not found: run `make` in the repository first")
        num_procs = max(1, min(num_procs, num_envs))
        sizes = [num_envs // num_procs + (i < num_envs % num_procs) for i in range(num_procs)]
        self.num_envs = num_envs
        self.sizes = sizes
        self.procs = []
        for i, size in enumerate(sizes):
            command = [binary, "--envs", str(size), "--seed", str(seed * 1000 + i + 1),
                       "--opponents", opponents, "--players", players]
            if model:
                command += ["--model", model]
            if pool:   # networks the "rl" opponents are drawn from, one per seat and hand
                command += ["--pool", ",".join(pool)]
            self.procs.append(subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE))
        for proc, size in zip(self.procs, sizes):
            words = proc.stdout.readline().split()
            if len(words) != 8 or words[0] != b"thenv":
                raise SystemExit("thenv did not start: " + b" ".join(words).decode(errors="replace"))
            assert int(words[3]) == size
            self.num_features, self.num_actions = int(words[5]), int(words[7])
        self.row = 4 + self.num_features + self.num_actions
        self.first = self._read()

    def _read(self):
        blocks = []
        for proc, size in zip(self.procs, self.sizes):
            want = size * self.row * 4
            data = proc.stdout.read(want)
            if len(data) != want:
                raise SystemExit("thenv stopped unexpectedly")
            blocks.append(np.frombuffer(data, dtype="<f4").reshape(size, self.row))
        rows = np.concatenate(blocks)
        obs = rows[:, 4:4 + self.num_features]
        allowed = rows[:, 4 + self.num_features:] > 0.5
        info = {"other_hands": rows[:, 2], "other_net": rows[:, 3]}
        return obs, rows[:, 0], rows[:, 1] > 0.5, allowed, info

    def reset(self):
        """What every learner sees first, and which actions it may take."""
        obs, _, _, allowed, _ = self.first
        return obs, allowed

    def step(self, actions):
        """One action per table -> (obs, reward in big blinds, hand over?, allowed actions, info)."""
        actions = np.asarray(actions, dtype=np.uint8)
        start = 0
        for proc, size in zip(self.procs, self.sizes):   # all processes work at the same time
            proc.stdin.write(actions[start:start + size].tobytes())
            proc.stdin.flush()
            start += size
        return self._read()

    def close(self):
        for proc in self.procs:
            proc.stdin.close()
        for proc in self.procs:
            proc.wait()


if __name__ == "__main__":
    # Speed and sanity check: play at random.
    import time
    envs = PokerVecEnv(64, num_procs=8, opponents="equity")
    obs, allowed = envs.reset()
    rng = np.random.default_rng(1)
    steps = hands = 0
    net = 0.0
    began = time.time()
    for _ in range(300):
        scores = rng.random(allowed.shape) * allowed
        obs, reward, done, allowed, info = envs.step(scores.argmax(axis=1))
        steps += len(done)
        hands += done.sum() + info["other_hands"].sum()
        net += reward.sum() + info["other_net"].sum()
    took = time.time() - began
    print(f"{steps} steps, {int(hands)} hands in {took:.1f}s: {steps / took:.0f} steps a second")
    print(f"a random player loses {-100 * net / hands:.0f} big blinds per 100 hands to equity")
    print("features min", obs.min(), "max", obs.max())
    envs.close()
