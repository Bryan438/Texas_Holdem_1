#ifndef _RL_POLICY_H_
#define _RL_POLICY_H_

#include <array>
#include <random>
#include <string>
#include <vector>
#include "bot.h"

// The learned bot. Nobody wrote its rules: a small neural network was trained
// by playing the other styles (rl/train.py) and now picks one of a few moves
// from a list of numbers that describe the spot.
//
// The same code turns a spot into numbers while training (thenv) and while
// playing (thbot, thmatch), so the network always sees what it learned from.

const int RL_FEATURES = 37;   // numbers describing the spot (see rl_observe)
const int RL_ACTIONS = 5;

enum rl_action{
  RL_FOLD,         // fold (not offered when checking is free)
  RL_CALL,         // check or call
  RL_RAISE_HALF,   // bet half the pot, or raise by half the pot after calling
  RL_RAISE_POT,    // bet the pot, or raise by the pot after calling
  RL_ALL_IN,
};

struct rl_observation{
  std::array<float, RL_FEATURES> features;
  std::array<bool, RL_ACTIONS> allowed;   // moves that mean something here
};

// The spot as the network sees it. Uses rng to simulate deals for the equity.
rl_observation rl_observe(const bot_view& v, std::mt19937& rng);

// The table move for one of the network's choices; always a legal move.
bot_move rl_move(const bot_view& v, int action);

// The network: a few fully connected layers with tanh between them.
class rl_model{
  public:
    // Text format written by rl/train.py:
    //   thrl 1 features 37 actions 5 layers 3
    //   layer <inputs> <outputs>, then outputs x inputs weights, then outputs biases
    bool parse(const std::string& text, std::string& error);
    bool load_file(const std::string& path, std::string& error);
    bool ready() const { return !layers.empty(); }
    // One score per action; a higher score is picked more often.
    std::array<float, RL_ACTIONS> scores(const std::array<float, RL_FEATURES>& features) const;

  private:
    struct layer{
      int inputs = 0, outputs = 0;
      std::vector<float> weights, bias;
    };
    std::vector<layer> layers;
};

// Picks among the allowed actions at random, in proportion to exp(score):
// a poker player who always does the same thing with the same hand is easy to read.
int rl_choose(const rl_model& model, const rl_observation& seen, std::mt19937& rng);

// The model `decide` uses for bot_style::RL: the one built into the program
// (rl_model_data.cpp) until another file is loaded. Null when there is none.
// Load a file before any bot plays, not while they play.
const rl_model* rl_current_model();
bool rl_use_model_file(const std::string& path, std::string& error);

// The built-in model's text ("" when the program was built without one).
extern const char* const RL_BUILT_IN_MODEL;

#endif
