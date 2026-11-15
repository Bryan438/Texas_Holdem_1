#ifndef _RL_ENV_H_
#define _RL_ENV_H_

#include <memory>
#include <random>
#include <utility>
#include <vector>
#include "match.h"
#include "rl_policy.h"
#include "table.h"

// One table for training the learned bot: the learner sits in seat 0, the
// other seats are played by the existing styles. Every hand starts with the
// same chips for everyone, as in a bot match.
struct rl_env_options{
  int players = 2;        // seats at the table, learner included (2 to 12)
  int chips = 200;
  unsigned seed = 1;      // same seed, same cards and same opponent choices
  // Styles the opponents are drawn from before every hand, with how often
  // each is picked (equity 3, rules 1: three hands in four against equity).
  std::vector<std::pair<bot_style, int>> opponents = {{bot_style::EQUITY, 1}};
};

struct rl_step{
  float reward = 0;       // big blinds the learner won in the hand that just ended
  bool done = false;      // a hand in which the learner made a choice ended
  // Hands that ended before the learner had any choice to make (everyone
  // folded to its big blind): nothing to learn from, but they count for the score.
  int other_hands = 0;
  float other_net = 0;
};

class rl_env{
  public:
    explicit rl_env(const rl_env_options& options);

    // The learner's spot: it is always the learner's turn between steps.
    const rl_observation& observation() const { return seen; }
    // Play the learner's choice, then the others, up to its next turn
    // (in the same hand or, once that hand is over, in the next one).
    rl_step step(int action);

    int illegal_moves() const { return out.errors; }   // should stay 0
    int hands_played() const;

  private:
    rl_env_options options;
    capture_sender out;
    std::unique_ptr<table> game;
    std::mt19937 rng;
    std::vector<bot_style> styles;   // who plays each seat this hand (seat 0: unused)
    int total_weight = 0;
    bot_view view;
    rl_observation seen;
    bool hand_running = false;
    bool learner_chose = false;      // in the current hand

    void start_hand();
    void play(int seat, const bot_move& move, bool can_check);
    void play_to_learner(rl_step& result);
};

#endif
