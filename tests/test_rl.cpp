// The learned bot: how a spot becomes numbers, how the network's choice
// becomes a legal move, reading model files, and the training tables.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include "test_framework.h"
#include "match.h"
#include "rl_env.h"
#include "rl_policy.h"

static Card rl_card(const char* code){
  Card c;
  card_from_code(code, c);
  return c;
}

// The learner's view on its turn, heads-up.
static bot_view spot(std::vector<const char*> hole, std::vector<const char*> board, int pot,
                     int current_bet, int to_call, int raise_min, int raise_max){
  bot_view v;
  static const char* stages[] = {"preflop", "", "", "flop", "turn", "river"};
  v.stage = stages[board.size()];
  v.pot = pot;
  v.current_bet = current_bet;
  v.to_call = to_call;
  v.can_check = to_call == 0;
  v.can_raise = raise_max > 0;
  v.raise_min = raise_min;
  v.raise_max = raise_max;
  v.opponents = 1;
  v.stack = 190;
  v.opponent_stack = 190;
  for(const char* code : hole) v.hole.push_back(rl_card(code));
  for(const char* code : board) v.board.push_back(rl_card(code));
  return v;
}

// A model with one layer: every action's score is its bias plus `weight`
// times one feature.
static std::string one_layer_model(std::vector<float> bias, int feature = 0, std::vector<float> weight = {}){
  std::string text = "thrl 1 features " + std::to_string(RL_FEATURES) + " actions " +
                     std::to_string(RL_ACTIONS) + " layers 1\nlayer " + std::to_string(RL_FEATURES) +
                     " " + std::to_string(RL_ACTIONS) + "\n";
  for(int a = 0; a < RL_ACTIONS; a++){
    for(int f = 0; f < RL_FEATURES; f++){
      text += (f == feature && a < (int)weight.size() ? std::to_string(weight[a]) : "0") + " ";
    }
    text += "\n";
  }
  for(float b : bias) text += std::to_string(b) + " ";
  return text + "\n";
}

TEST(rl_features_describe_the_spot){
  std::mt19937 rng(1);
  // Preflop with aces, facing a raise to 6.
  rl_observation seen = rl_observe(spot({"As", "Ah"}, {}, 9, 6, 4, 10, 200), rng);
  CHECK(seen.features[0] == 1 && seen.features[1] == 0);        // preflop
  CHECK(seen.features[4] > 0.8);                                // aces win most of the time
  CHECK(std::fabs(seen.features[7] - 4.0 / 13) < 1e-5);         // price of calling
  CHECK(seen.features[25] == 1 && seen.features[26] == 0);      // a pair, not suited
  // The river with the nut flush on a paired, three-suited board.
  seen = rl_observe(spot({"Ah", "Kh"}, {"9h", "4h", "2h", "9c", "Td"}, 20, 0, 0, 2, 190), rng);
  CHECK(seen.features[3] == 1 && seen.features[0] == 0);        // river
  CHECK(seen.features[33] == 1);                                // a monster
  CHECK(seen.features[RL_FEATURES - 2] == 1);                   // paired board
  CHECK(seen.features[RL_FEATURES - 1] == 1);                   // three of a suit: the last feature is filled
  for(float f : seen.features){
    CHECK(std::isfinite(f) && f >= -0.1 && f <= 1.0);
  }
  // Betting makes the hand look worse: the bettor is assumed to hold something.
  seen = rl_observe(spot({"Qs", "Jd"}, {"Kh", "9c", "2d"}, 30, 10, 10, 20, 190), rng);
  CHECK(seen.features[5] < seen.features[4]);
}

TEST(rl_only_offers_moves_that_mean_something){
  std::mt19937 rng(1);
  // Nobody has bet: no reason to fold.
  rl_observation seen = rl_observe(spot({"7d", "2c"}, {"Kh", "9c", "2d"}, 20, 0, 0, 2, 190), rng);
  CHECK(!seen.allowed[RL_FOLD] && seen.allowed[RL_CALL] && seen.allowed[RL_RAISE_HALF] &&
        seen.allowed[RL_RAISE_POT] && seen.allowed[RL_ALL_IN]);
  // Facing an all-in: fold or call.
  seen = rl_observe(spot({"7d", "2c"}, {"Kh", "9c", "2d"}, 220, 200, 190, 0, 0), rng);
  CHECK(seen.allowed[RL_FOLD] && seen.allowed[RL_CALL]);
  CHECK(!seen.allowed[RL_RAISE_HALF] && !seen.allowed[RL_RAISE_POT] && !seen.allowed[RL_ALL_IN]);
  // A short stack: any raise is all in, so only that one is offered.
  seen = rl_observe(spot({"7d", "2c"}, {"Kh", "9c", "2d"}, 100, 0, 0, 2, 30), rng);
  CHECK(!seen.allowed[RL_RAISE_HALF] && !seen.allowed[RL_RAISE_POT] && seen.allowed[RL_ALL_IN]);
}

TEST(rl_choices_become_legal_moves){
  bot_view v = spot({"As", "Kd"}, {"Kh", "9c", "2d"}, 20, 0, 0, 2, 190);
  CHECK(rl_move(v, RL_FOLD).action == "check");                // never folds for free
  CHECK(rl_move(v, RL_CALL).action == "check");
  CHECK(rl_move(v, RL_RAISE_HALF).action == "raise" && rl_move(v, RL_RAISE_HALF).amount == 10);
  CHECK(rl_move(v, RL_RAISE_POT).amount == 20);
  CHECK(rl_move(v, RL_ALL_IN).amount == 190);
  // Facing a bet of 10 into 20: a pot-sized raise calls 10, then adds the 40 in the pot.
  v = spot({"As", "Kd"}, {"Kh", "9c", "2d"}, 30, 10, 10, 20, 190);
  CHECK(rl_move(v, RL_FOLD).action == "fold");
  CHECK(rl_move(v, RL_CALL).action == "call");
  CHECK(rl_move(v, RL_RAISE_HALF).amount == 30);
  CHECK(rl_move(v, RL_RAISE_POT).amount == 50);
  // Raising is not allowed: the choice becomes a call.
  v = spot({"As", "Kd"}, {"Kh", "9c", "2d"}, 220, 200, 190, 0, 0);
  CHECK(rl_move(v, RL_ALL_IN).action == "call");
  CHECK(rl_move(v, 99).action == "call");
}

TEST(rl_model_reads_and_scores){
  rl_model model;
  std::string error;
  CHECK(!model.ready());
  // score = bias + weight * feature 4 (the chance of winning)
  CHECK(model.parse(one_layer_model({1, 2, 3, 4, 5}, 4, {0, 10, 0, 0, -10}), error));
  CHECK(model.ready());
  std::array<float, RL_FEATURES> features;
  features.fill(0);
  features[4] = 0.5f;
  std::array<float, RL_ACTIONS> scores = model.scores(features);
  CHECK(std::fabs(scores[0] - 1) < 1e-5 && std::fabs(scores[1] - 7) < 1e-5 && std::fabs(scores[4] - 0) < 1e-5);

  // Two layers: tanh sits between them, not after the last.
  std::string two = "thrl 1 features " + std::to_string(RL_FEATURES) + " actions 5 layers 2\nlayer " +
                    std::to_string(RL_FEATURES) + " 1\n";
  for(int f = 0; f < RL_FEATURES; f++) two += f == 4 ? "2 " : "0 ";
  two += "\n0\nlayer 1 5\n1 2 3 4 5\n0 0 0 0 100\n";
  CHECK(model.parse(two, error));
  scores = model.scores(features);
  CHECK(std::fabs(scores[1] - 2 * std::tanh(1.0f)) < 1e-5);
  CHECK(std::fabs(scores[4] - (5 * std::tanh(1.0f) + 100)) < 1e-4);
}

TEST(rl_model_refuses_bad_files){
  rl_model model;
  std::string error;
  CHECK(!model.parse("", error));
  CHECK(!model.parse("hello world", error));
  CHECK(!model.parse("thrl 2 features 37 actions 5 layers 1", error));
  // Trained for another number of inputs: say so instead of playing nonsense.
  CHECK(!model.parse("thrl 1 features 12 actions 5 layers 1\nlayer 12 5\n", error));
  CHECK(error.find("train it again") != std::string::npos);
  std::string good = one_layer_model({1, 2, 3, 4, 5});
  CHECK(!model.parse(good.substr(0, good.size() / 2), error));          // cut short
  CHECK(!model.parse(one_layer_model({1, 2, 3, 4, NAN}), error));       // a bias that is not a number
  CHECK(!model.parse(one_layer_model({0, 0, 0, 0, 0}, 0, {NAN}), error)); // and a weight
  CHECK(!model.ready());                                                // a failed read leaves it empty
  CHECK(!model.load_file("/no/such/model.txt", error));

  const char* path = "tests/rl_model_test.tmp";
  std::ofstream(path) << good;
  CHECK(model.load_file(path, error));
  CHECK(model.ready());
  remove(path);
}

TEST(rl_choose_follows_the_scores_and_the_allowed_moves){
  rl_model model;
  std::string error;
  CHECK(model.parse(one_layer_model({0, 0, 0, 50, 0}), error));       // loves the pot-sized raise
  std::mt19937 rng(4);
  rl_observation seen = rl_observe(spot({"7d", "2c"}, {"Kh", "9c", "2d"}, 20, 0, 0, 2, 190), rng);
  for(int i = 0; i < 50; i++) CHECK_EQ(rl_choose(model, seen, rng), RL_RAISE_POT);
  // When that is not allowed it picks among the rest, never a forbidden move.
  seen = rl_observe(spot({"7d", "2c"}, {"Kh", "9c", "2d"}, 220, 200, 190, 0, 0), rng);
  int folds = 0, calls = 0;
  for(int i = 0; i < 400; i++){
    int a = rl_choose(model, seen, rng);
    CHECK(a == RL_FOLD || a == RL_CALL);
    folds += a == RL_FOLD;
    calls += a == RL_CALL;
  }
  CHECK(folds > 120 && calls > 120);        // equal scores: about half each
}

TEST(rl_env_plays_legal_hands_and_repeats_with_the_same_seed){
  rl_env_options options;
  options.players = 3;
  options.seed = 11;
  options.opponents = {{bot_style::RULES, 2}, {bot_style::CALLING, 1}, {bot_style::RANDOM, 1}};
  std::vector<float> rewards[2];
  for(int run = 0; run < 2; run++){
    rl_env env(options);
    std::mt19937 rng(3);
    int finished = 0;
    for(int i = 0; i < 400; i++){
      const rl_observation& seen = env.observation();
      CHECK(seen.allowed[RL_CALL]);
      int action = rng() % RL_ACTIONS;       // forbidden picks are played as a call
      rl_step step = env.step(action);
      CHECK(std::isfinite(step.reward) && std::fabs(step.reward) <= 200);   // 100 bb from each of two opponents
      CHECK(step.done || step.reward == 0);
      finished += step.done + step.other_hands;
      rewards[run].push_back(step.reward + step.other_net);
    }
    CHECK_EQ(env.illegal_moves(), 0);
    CHECK(finished > 100);
    CHECK(env.hands_played() >= finished);
  }
  CHECK(rewards[0] == rewards[1]);
}

// The network built into the program, when there is one.
TEST(rl_built_in_model_plays_legal_moves_and_beats_the_baselines){
  if(rl_current_model() == nullptr){
    printf("    (no built-in model: skipped)\n");
    return;
  }
  match_options options;
  options.hands = 300;
  match_result r = run_match({bot_style::RL, bot_style::EQUITY, bot_style::RL, bot_style::CALLING,
                              bot_style::RANDOM}, options);
  CHECK_EQ(r.hands, 300);
  CHECK_EQ(r.illegal_moves, 0);
  options.hands = 1500;
  r = run_match({bot_style::RL, bot_style::RANDOM}, options);
  CHECK(bb_per_100(r.seats[0], r) - bb_per_100_margin(r.seats[0], r) > 0);
  r = run_match({bot_style::RL, bot_style::CALLING}, options);
  CHECK(bb_per_100(r.seats[0], r) - bb_per_100_margin(r.seats[0], r) > 0);
}
