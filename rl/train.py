"""Train the learned poker bot with PPO.

Adapted from CleanRL's ppo.py (https://docs.cleanrl.dev/rl-algorithms/ppo/#ppopy,
MIT License, Copyright (c) 2019 CleanRL developers). What changed for poker:

  * the environment is the real table (../thenv) instead of a Gymnasium one;
  * actions that make no sense in a spot (folding when checking is free,
    raising when that is not allowed) are masked out;
  * the reward is the big blinds won when a hand ends, so nothing is discounted;
  * at the end the network is saved in the text format the C++ bot reads.

Run it from this folder, after `make` in the repository:

    python train.py                              # about 3 million decisions
    python train.py --total-timesteps 500000     # a quick try
    ../thmatch --model models/rl.txt --hands 20000 rl equity

Opponent statistics: with --session N the same opponents stay for about N
hands, so the learner can read their habits (the last 10 numbers it sees, see
opponent_stats.h); --switch P makes them change style halfway through P% of
the sessions. --start-from accepts a network trained before those numbers
existed (37 inputs): it is widened with zero weights for the new inputs, so it
starts out playing exactly as before.

Self-play: with --self-play, "rl" opponents play earlier versions of the
learner itself. Every --snapshot-every updates the network is saved to
--pool-dir, and the opponents are drawn from the latest --pool-size of those
(plus any --pool-anchor networks, which always stay in).

    python train.py --self-play --start-from models/mix2.pt --pool-anchor models/mix2.txt \
        --opponents equity:4,rules:2,calling:1,random:1,rl:8 --out models/sp
"""
import os
import time
from dataclasses import dataclass

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
import tyro
from torch.distributions.categorical import Categorical

from poker_env import HERE, PokerVecEnv


@dataclass
class Args:
    seed: int = 1
    """seed of the experiment"""
    opponents: str = "equity:6,rules:2,calling:1,random:1"
    """styles the opponents are drawn from before every hand, with weights"""
    players: str = "2"
    """seats per table, learner included; a list such as 2,2,3 is used in turn"""
    opponent_model: str = ""
    """the network that "rl" opponents use (to train against an earlier version)"""
    start_from: str = ""
    """a .pt checkpoint to continue training from"""
    out: str = os.path.join(HERE, "models", "rl")
    """where to save: OUT.txt for the C++ bot, OUT.pt to continue training"""
    embed: bool = False
    """also write ../rl_model_data.cpp, so the next `make` builds this network into thbot and thmatch"""
    num_procs: int = 8
    """thenv processes to run side by side"""
    self_play: bool = False
    """"rl" opponents play earlier versions of the learner (add rl:WEIGHT to --opponents)"""
    pool_dir: str = os.path.join(HERE, "models", "pool")
    """where the earlier versions are saved"""
    snapshot_every: int = 20
    """updates between two saved versions"""
    pool_size: int = 8
    """how many of the latest versions the opponents are drawn from"""
    pool_anchor: str = ""
    """networks (.txt, comma separated) that always stay in the pool"""
    hidden: int = 64
    """neurons per hidden layer"""
    session: int = 1
    """hands the same opponents stay for, so their statistics mean something (1: new ones every hand)"""
    switch: int = 0
    """percent of sessions in which the opponents change style halfway"""

    # Algorithm specific arguments
    total_timesteps: int = 3000000
    """total decisions the learner makes"""
    learning_rate: float = 3e-4
    """the learning rate of the optimizer"""
    num_envs: int = 64
    """the number of tables played at once"""
    num_steps: int = 256
    """decisions per table between two updates of the network"""
    anneal_lr: bool = True
    """lower the learning rate to zero over the run"""
    gamma: float = 1.0
    """the discount factor (1: a hand is short and only its result counts)"""
    gae_lambda: float = 0.95
    """the lambda for the general advantage estimation"""
    num_minibatches: int = 8
    """the number of mini-batches"""
    update_epochs: int = 4
    """the K epochs to update the policy"""
    norm_adv: bool = True
    """Toggles advantages normalization"""
    clip_coef: float = 0.2
    """the surrogate clipping coefficient"""
    ent_coef: float = 0.01
    """coefficient of the entropy: higher keeps the bot trying things for longer"""
    vf_coef: float = 0.5
    """coefficient of the value function"""
    max_grad_norm: float = 0.5
    """the maximum norm for the gradient clipping"""
    reward_scale: float = 20.0
    """big blinds that count as a reward of 1 (a 100 bb all-in is then 5)"""


def layer_init(layer, std=np.sqrt(2), bias_const=0.0):
    torch.nn.init.orthogonal_(layer.weight, std)
    torch.nn.init.constant_(layer.bias, bias_const)
    return layer


class Agent(nn.Module):
    def __init__(self, num_features, num_actions, hidden=64):
        super().__init__()
        # Guesses how many big blinds the hand will win: only used while training.
        self.critic = nn.Sequential(
            layer_init(nn.Linear(num_features, hidden)),
            nn.Tanh(),
            layer_init(nn.Linear(hidden, hidden)),
            nn.Tanh(),
            layer_init(nn.Linear(hidden, 1), std=1.0),
        )
        # Scores every action: this is the part the C++ bot plays with.
        self.actor = nn.Sequential(
            layer_init(nn.Linear(num_features, hidden)),
            nn.Tanh(),
            layer_init(nn.Linear(hidden, hidden)),
            nn.Tanh(),
            layer_init(nn.Linear(hidden, num_actions), std=0.01),
        )

    def get_value(self, x):
        return self.critic(x)

    def get_action_and_value(self, x, allowed, action=None):
        logits = self.actor(x).masked_fill(~allowed, -1e8)
        probs = Categorical(logits=logits)
        if action is None:
            action = probs.sample()
        return action, probs.log_prob(action), probs.entropy(), self.critic(x)


def load_start(agent, path):
    """Loads a checkpoint; one with fewer inputs gets zero weights for the new ones,
    so it plays exactly as it did until training finds a use for them."""
    saved = torch.load(path)
    own = agent.state_dict()
    for key, value in saved.items():
        if key in own and value.dim() == 2 and value.shape[0] == own[key].shape[0] \
                and value.shape[1] < own[key].shape[1]:
            wider = torch.zeros_like(own[key])
            wider[:, :value.shape[1]] = value
            print(f"{key}: {value.shape[1]} inputs widened to {own[key].shape[1]}")
            saved[key] = wider
    agent.load_state_dict(saved)


def model_text(agent, num_features, num_actions):
    """The actor in the text format rl_policy.h describes."""
    layers = [m for m in agent.actor if isinstance(m, nn.Linear)]
    lines = [f"thrl 1 features {num_features} actions {num_actions} layers {len(layers)}"]
    for layer in layers:
        lines.append(f"layer {layer.in_features} {layer.out_features}")
        for row in layer.weight.detach().cpu().numpy():
            lines.append(" ".join(f"{w:.6g}" for w in row))
        lines.append(" ".join(f"{b:.6g}" for b in layer.bias.detach().cpu().numpy()))
    return "\n".join(lines) + "\n"


class Pool:
    """Earlier versions of the learner, for "rl" opponents to play with."""

    def __init__(self, args):
        self.dir = args.pool_dir
        self.size = args.pool_size
        self.anchors = [a for a in args.pool_anchor.split(",") if a]
        self.versions = []
        os.makedirs(self.dir, exist_ok=True)

    def add(self, agent, num_features, num_actions):
        path = os.path.join(self.dir, f"gen{len(self.versions):03d}.txt")
        with open(path, "w") as f:
            f.write(model_text(agent, num_features, num_actions))
        self.versions.append(path)

    def files(self):
        return self.anchors + self.versions[-self.size:]


def save(agent, args, num_features, num_actions):
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    text = model_text(agent, num_features, num_actions)
    with open(args.out + ".txt", "w") as f:
        f.write(text)
    torch.save(agent.state_dict(), args.out + ".pt")
    if args.embed:
        with open(os.path.join(HERE, "..", "rl_model_data.cpp"), "w") as f:
            f.write("// The trained network built into thbot and thmatch, in the text format\n"
                    "// rl_policy.h describes. Written by rl/train.py --embed: do not edit by hand.\n"
                    '#include "rl_policy.h"\n\n'
                    'const char* const RL_BUILT_IN_MODEL = R"THRL(\n' + text + ')THRL";\n')


if __name__ == "__main__":
    args = tyro.cli(Args)
    batch_size = args.num_envs * args.num_steps
    minibatch_size = batch_size // args.num_minibatches
    num_iterations = args.total_timesteps // batch_size

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.set_num_threads(4)
    device = torch.device("cpu")   # the network is tiny: the tables are the slow part

    def open_tables(round_number, pool_files):
        # A new seed each time, or the tables would deal the same cards again.
        return PokerVecEnv(args.num_envs, args.num_procs, args.seed * 1000 + round_number, args.opponents,
                           args.players, args.opponent_model or None, pool_files, args.session, args.switch)

    pool = Pool(args) if args.self_play else None
    if args.self_play and "rl" not in [part.split(":")[0] for part in args.opponents.split(",")]:
        raise SystemExit("--self-play needs rl opponents: add for example rl:8 to --opponents")
    # The tables are opened once to learn the sizes; with self-play they are
    # opened again below, once the first version is in the pool.
    envs = open_tables(0, args.pool_anchor.split(",") if args.pool_anchor else None)
    num_features, num_actions = envs.num_features, envs.num_actions
    agent = Agent(num_features, num_actions, args.hidden).to(device)
    if args.start_from:
        load_start(agent, args.start_from)
    if pool:
        pool.add(agent, num_features, num_actions)
        envs.close()
        envs = open_tables(1, pool.files())
    optimizer = optim.Adam(agent.parameters(), lr=args.learning_rate, eps=1e-5)

    # ALGO Logic: Storage setup
    obs = torch.zeros((args.num_steps, args.num_envs, num_features))
    allowed = torch.zeros((args.num_steps, args.num_envs, num_actions), dtype=torch.bool)
    actions = torch.zeros((args.num_steps, args.num_envs), dtype=torch.long)
    logprobs = torch.zeros((args.num_steps, args.num_envs))
    rewards = torch.zeros((args.num_steps, args.num_envs))
    dones = torch.zeros((args.num_steps, args.num_envs))
    values = torch.zeros((args.num_steps, args.num_envs))

    global_step = 0
    start_time = time.time()
    first_obs, first_allowed = envs.reset()
    next_obs = torch.from_numpy(first_obs.copy())
    next_allowed = torch.from_numpy(first_allowed.copy())
    next_done = torch.zeros(args.num_envs)
    recent = []   # (big blinds won, hands) of the last iterations, for a steadier score
    print(f"{num_iterations} updates of {batch_size} decisions, against {args.opponents}, {args.players} players"
          + (f", sessions of about {args.session} hands ({args.switch}% switch style)" if args.session > 1 else "")
          + (f", self-play: a new version every {args.snapshot_every} updates" if pool else ""))

    for iteration in range(1, num_iterations + 1):
        if args.anneal_lr:
            frac = 1.0 - (iteration - 1.0) / num_iterations
            optimizer.param_groups[0]["lr"] = frac * args.learning_rate

        won, hands = 0.0, 0
        for step in range(0, args.num_steps):
            global_step += args.num_envs
            obs[step] = next_obs
            allowed[step] = next_allowed
            dones[step] = next_done

            with torch.no_grad():
                action, logprob, _, value = agent.get_action_and_value(next_obs, next_allowed)
                values[step] = value.flatten()
            actions[step] = action
            logprobs[step] = logprob

            o, reward, done, a, info = envs.step(action.numpy())
            won += float(reward.sum() + info["other_net"].sum())
            hands += int(done.sum() + info["other_hands"].sum())
            rewards[step] = torch.from_numpy(reward / args.reward_scale)
            next_obs, next_allowed = torch.from_numpy(o.copy()), torch.from_numpy(a.copy())
            next_done = torch.from_numpy(done.astype(np.float32))

        # bootstrap value if not done
        with torch.no_grad():
            next_value = agent.get_value(next_obs).reshape(1, -1)
            advantages = torch.zeros_like(rewards)
            lastgaelam = 0
            for t in reversed(range(args.num_steps)):
                if t == args.num_steps - 1:
                    nextnonterminal = 1.0 - next_done
                    nextvalues = next_value
                else:
                    nextnonterminal = 1.0 - dones[t + 1]
                    nextvalues = values[t + 1]
                delta = rewards[t] + args.gamma * nextvalues * nextnonterminal - values[t]
                advantages[t] = lastgaelam = delta + args.gamma * args.gae_lambda * nextnonterminal * lastgaelam
            returns = advantages + values

        # flatten the batch
        b_obs = obs.reshape((-1, num_features))
        b_allowed = allowed.reshape((-1, num_actions))
        b_logprobs = logprobs.reshape(-1)
        b_actions = actions.reshape(-1)
        b_advantages = advantages.reshape(-1)
        b_returns = returns.reshape(-1)
        b_values = values.reshape(-1)

        # Optimizing the policy and value network
        b_inds = np.arange(batch_size)
        for epoch in range(args.update_epochs):
            np.random.shuffle(b_inds)
            for start in range(0, batch_size, minibatch_size):
                mb_inds = b_inds[start:start + minibatch_size]

                _, newlogprob, entropy, newvalue = agent.get_action_and_value(
                    b_obs[mb_inds], b_allowed[mb_inds], b_actions[mb_inds])
                logratio = newlogprob - b_logprobs[mb_inds]
                ratio = logratio.exp()

                with torch.no_grad():
                    approx_kl = ((ratio - 1) - logratio).mean()

                mb_advantages = b_advantages[mb_inds]
                if args.norm_adv:
                    mb_advantages = (mb_advantages - mb_advantages.mean()) / (mb_advantages.std() + 1e-8)

                # Policy loss
                pg_loss1 = -mb_advantages * ratio
                pg_loss2 = -mb_advantages * torch.clamp(ratio, 1 - args.clip_coef, 1 + args.clip_coef)
                pg_loss = torch.max(pg_loss1, pg_loss2).mean()

                # Value loss (not clipped: results swing too much for that to help)
                v_loss = 0.5 * ((newvalue.view(-1) - b_returns[mb_inds]) ** 2).mean()

                entropy_loss = entropy.mean()
                loss = pg_loss - args.ent_coef * entropy_loss + v_loss * args.vf_coef

                optimizer.zero_grad()
                loss.backward()
                nn.utils.clip_grad_norm_(agent.parameters(), args.max_grad_norm)
                optimizer.step()

        recent = (recent + [(won, hands)])[-10:]
        if iteration % 5 == 0 or iteration == num_iterations:
            share = np.bincount(b_actions.numpy(), minlength=num_actions) / batch_size
            smooth = 100 * sum(w for w, _ in recent) / max(1, sum(h for _, h in recent))
            print(f"update {iteration:4d}/{num_iterations}  decisions {global_step:8d}  "
                  f"bb/100 {100 * won / max(1, hands):+7.1f} (last 10 updates {smooth:+7.1f})  "
                  f"entropy {entropy_loss.item():.2f}  kl {approx_kl.item():.3f}  "
                  f"fold/call/half/pot/allin {' '.join(f'{100 * s:.0f}%' for s in share)}  "
                  f"{int(global_step / (time.time() - start_time))} a second", flush=True)
        if iteration % 25 == 0 or iteration == num_iterations:
            save(agent, args, num_features, num_actions)

        # Self-play: the learner as it is now joins the pool, and the tables are
        # opened again so the opponents draw from the new pool. Hands in progress
        # are dropped; the next rollout starts on fresh tables.
        if pool and iteration % args.snapshot_every == 0 and iteration < num_iterations:
            pool.add(agent, num_features, num_actions)
            envs.close()
            envs = open_tables(len(pool.versions), pool.files())
            first_obs, first_allowed = envs.reset()
            next_obs = torch.from_numpy(first_obs.copy())
            next_allowed = torch.from_numpy(first_allowed.copy())
            next_done = torch.zeros(args.num_envs)
            print(f"           pool: {', '.join(os.path.basename(f) for f in pool.files())}", flush=True)

    envs.close()
    print(f"saved {args.out}.txt" + (" and ../rl_model_data.cpp (run make to build it in)" if args.embed else ""))
