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
  CHECK(seen.features[RL_BASE_FEATURES - 2] == 1);              // paired board
  CHECK(seen.features[RL_BASE_FEATURES - 1] == 1);              // three of a suit
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
  CHECK(!model.parse("thrl 2 features 47 actions 5 layers 1", error));
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

// ---------------------------------------------------------------- several versions at one table

TEST(rl_seats_can_play_different_networks){
  auto calls = std::make_shared<rl_model>(), shoves = std::make_shared<rl_model>();
  std::string error;
  CHECK(calls->parse(one_layer_model({0, 50, 0, 0, 0}), error));     // always check or call
  CHECK(shoves->parse(one_layer_model({0, 0, 0, 0, 50}), error));    // all in whenever it may

  // decide() plays the network it is given.
  std::mt19937 rng(5);
  json st = {{"type", "state"}, {"you", 0}, {"stage", "flop"}, {"pot", 20}, {"current_bet", 0},
             {"blinds", {1, 2}}, {"board", {"Kh", "9c", "2d"}}, {"dealer", 1}, {"small_blind", 1}, {"big_blind", 0},
             {"seats", {{{"seat", 0}, {"stack", 190}, {"bet", 0}, {"total_bet", 10}, {"status", "active"}, {"cards", {"7d", "2c"}}},
                        {{"seat", 1}, {"stack", 190}, {"bet", 0}, {"total_bet", 10}, {"status", "active"}, {"cards", nullptr}}}},
             {"actions", {{"fold", true}, {"check", true}, {"call", 0}, {"raise", {{"min", 2}, {"max", 190}}}}}};
  CHECK(decide(st, bot_style::RL, rng, calls.get()).action == "check");
  bot_move shove = decide(st, bot_style::RL, rng, shoves.get());
  CHECK(shove.action == "raise" && shove.amount == 190);

  // In a match each seat keeps its own network.
  match_options options;
  options.hands = 200;
  options.models = {calls.get(), shoves.get()};
  options.names = {"caller", ""};
  match_result r = run_match({bot_style::RL, bot_style::RL}, options);
  CHECK_EQ(r.illegal_moves, 0);
  CHECK(r.seats[0].name == "caller" && r.seats[1].name == "rl-2");
  int caller_raises = 0, shover_all_ins = 0;
  for(const json& hand : r.records){
    for(const json& a : hand["actions"]){
      if(a["seat"] == 0 && (a["action"] == "raise" || a["action"] == "bet")) caller_raises++;
      if(a["seat"] == 1 && a.value("allin", false)) shover_all_ins++;
    }
  }
  CHECK_EQ(caller_raises, 0);
  CHECK(shover_all_ins > 100);

  // Training tables draw a network from the pool for each "rl" opponent, every hand.
  rl_env_options env_options;
  env_options.seed = 3;
  env_options.opponents = {{bot_style::RL, 1}};
  env_options.pool = {calls, shoves};
  rl_env env(env_options);
  std::mt19937 pick(9);
  int learner_folds = 0;
  for(int i = 0; i < 600; i++){
    const rl_observation& seen = env.observation();
    // Facing an all-in (only fold or call allowed) happens only against the shover.
    if(seen.allowed[RL_FOLD] && !seen.allowed[RL_RAISE_HALF] && !seen.allowed[RL_ALL_IN]) learner_folds++;
    env.step(seen.allowed[RL_CALL] && pick() % 2 ? RL_CALL : RL_FOLD);
  }
  CHECK_EQ(env.illegal_moves(), 0);
  CHECK(learner_folds > 20);                     // met the shover
  CHECK(env.hands_played() > 150);               // and hands went on: not every one was a shove
}

// ---------------------------------------------------------------- what the bot learns about the others

// A finished hand as the table records it: who was dealt in, then the actions.
static json hand_of(std::vector<std::string> names, std::vector<std::vector<std::string>> actions){
  json hand = {{"players", json::array()}, {"actions", json::array()}};
  for(size_t i = 0; i < names.size(); i++) hand["players"].push_back({{"seat", (int)i}, {"name", names[i]}});
  for(const auto& a : actions){   // street, name, action[, reason]
    json action = {{"street", a[0]}, {"name", a[1]}, {"action", a[2]}, {"amount", 0}};
    if(a.size() > 3) action["reason"] = a[3];
    hand["actions"].push_back(action);
  }
  return hand;
}

TEST(opponent_tracker_tells_a_maniac_from_a_rock){
  opponent_tracker reads(1.0);
  float stranger[OPPONENT_FEATURES], maniac[OPPONENT_FEATURES], rock[OPPONENT_FEATURES];
  reads.describe("Max", stranger);
  CHECK(stranger[4] == 0);                                   // nothing seen yet
  CHECK(stranger[0] > 0.3 && stranger[0] < 0.5);             // an average player
  for(int i = 0; i < 40; i++){
    reads.add_hand(hand_of({"Max", "Rocky"}, {
      {"preflop", "Max", "small_blind"}, {"preflop", "Rocky", "big_blind"},
      {"preflop", "Max", "raise"}, {"preflop", "Rocky", "call"},
      {"flop", "Rocky", "check"}, {"flop", "Max", "bet"}, {"flop", "Rocky", "fold"}}));
  }
  reads.describe("Max", maniac);
  reads.describe("Rocky", rock);
  CHECK(maniac[0] > 0.85 && maniac[1] > 0.85 && maniac[2] > 0.85);   // in, raising, betting
  CHECK(rock[1] < 0.1 && rock[3] > 0.85);                             // never raises, folds to a bet
  CHECK(maniac[4] > 0.6 && maniac[4] < 1);
  CHECK_EQ((int)reads.hands_seen("Rocky"), 40);
  for(int f = 0; f < OPPONENT_FEATURES; f++) CHECK(maniac[f] >= 0 && maniac[f] <= 1);

  // A few hands say little: the rates stay near the average.
  opponent_tracker few;
  few.add_hand(hand_of({"Max", "Rocky"}, {{"preflop", "Max", "raise"}, {"preflop", "Rocky", "fold"}}));
  few.describe("Max", maniac);
  CHECK(maniac[1] > stranger[1] && maniac[1] < 0.5);
}

TEST(opponent_tracker_counts_only_choices){
  opponent_tracker reads(1.0);
  for(int i = 0; i < 30; i++){
    // Blinds are forced, a timeout was nobody's choice, a check is not a call.
    reads.add_hand(hand_of({"A", "B"}, {
      {"preflop", "A", "small_blind"}, {"preflop", "B", "big_blind"},
      {"preflop", "A", "fold", "timeout"}}));
  }
  float a[OPPONENT_FEATURES];
  reads.describe("A", a);
  CHECK(a[0] < 0.15 && a[1] < 0.15);
  CHECK(std::fabs(a[3] - 0.40f) < 1e-5);            // never bet into after the flop: still the average
  // Someone who was not dealt in is not counted, nor is a damaged hand.
  reads.add_hand(json::parse(R"({"players":"nonsense"})"));
  reads.add_hand(hand_of({"A"}, {{"preflop", "Ghost", "raise"}}));
  CHECK_EQ(reads.hands_seen("Ghost"), 0.0);
}

TEST(opponent_tracker_notices_a_change_of_style){
  opponent_tracker reads;   // the default memory forgets slowly
  json tight = hand_of({"P", "Q"}, {{"preflop", "P", "fold"}});
  json loose = hand_of({"P", "Q"}, {{"preflop", "P", "raise"}, {"preflop", "Q", "fold"}});
  for(int i = 0; i < 300; i++) reads.add_hand(tight);
  for(int i = 0; i < 150; i++) reads.add_hand(loose);
  float p[OPPONENT_FEATURES];
  reads.describe("P", p);
  CHECK(p[1] > 0.7);        // 150 hands of raising outweigh 300 older folds
  CHECK(reads.hands_seen("P") < 101);
}

TEST(rl_features_include_the_main_opponent){
  opponent_tracker reads(1.0);
  for(int i = 0; i < 60; i++){
    reads.add_hand(hand_of({"me", "Max", "Rocky"}, {{"preflop", "Max", "raise"}, {"preflop", "Rocky", "fold"},
                                                    {"preflop", "me", "fold"}}));
  }
  std::mt19937 rng(1);
  bot_view v = spot({"Qs", "Jd"}, {"Kh", "9c", "2d"}, 40, 10, 10, 20, 190);
  v.opponents = 2;
  opponent_view rocky, max;
  rocky.name = "Rocky";
  max.name = "Max";
  max.bet = 10;                           // Max bet: he is the one to read
  v.in_hand = {rocky, max};
  rl_observation seen = rl_observe(v, rng, &reads);
  const int M = RL_BASE_FEATURES, O = RL_BASE_FEATURES + OPPONENT_FEATURES;
  CHECK(seen.features[M + 1] > 0.85);                     // Max raises
  CHECK(seen.features[O + 1] < 0.1);                      // Rocky, the other, does not
  CHECK(seen.features[RL_FEATURES - 1] > 0.6);            // and he has been seen: the last feature is filled
  for(float f : seen.features) CHECK(std::isfinite(f) && f >= -0.1 && f <= 1.0);

  // Knowing nothing, both look like an average stranger.
  rl_observation blind = rl_observe(v, rng);
  CHECK(blind.features[M + 4] == 0 && blind.features[O + 4] == 0);
  CHECK(std::fabs(blind.features[M] - 0.40f) < 1e-5 && std::fabs(blind.features[O] - 0.40f) < 1e-5);
  for(int f = 0; f < RL_BASE_FEATURES; f++){
    if(f != 4 && f != 5 && f != 6) CHECK(blind.features[f] == seen.features[f]);   // the rest is unchanged
  }
}

TEST(rl_reads_the_other_seats_from_the_state){
  json st = {{"type", "state"}, {"you", 1}, {"stage", "flop"}, {"pot", 40}, {"current_bet", 10},
             {"blinds", {1, 2}}, {"board", {"Kh", "9c", "2d"}}, {"dealer", 0},
             {"seats", {{{"seat", 0}, {"name", "A"}, {"stack", 190}, {"bet", 10}, {"total_bet", 20}, {"status", "active"}},
                        {{"seat", 1}, {"name", "me"}, {"stack", 190}, {"bet", 0}, {"total_bet", 10}, {"status", "active"},
                         {"cards", {"7d", "2c"}}},
                        {{"seat", 2}, {"name", "B"}, {"stack", 190}, {"bet", 0}, {"total_bet", 10}, {"status", "folded"}},
                        {{"seat", 3}, {"name", "C"}, {"stack", 0}, {"bet", 0}, {"total_bet", 200}, {"status", "allin"}}}},
             {"actions", {{"fold", true}, {"check", false}, {"call", 10}, {"raise", {{"min", 20}, {"max", 190}}}}}};
  bot_view v = read_bot_view(st);
  CHECK_EQ((int)v.in_hand.size(), 2);                     // the folded seat is out
  CHECK(v.in_hand[0].name == "C" && v.in_hand[1].name == "A");   // turn order after us
  CHECK(v.in_hand[1].bet == 10 && v.in_hand[0].total_bet == 200);
}

TEST(rl_old_models_with_37_inputs_still_play){
  std::string text = "thrl 1 features " + std::to_string(RL_BASE_FEATURES) + " actions 5 layers 1\nlayer " +
                     std::to_string(RL_BASE_FEATURES) + " 5\n";
  for(int a = 0; a < RL_ACTIONS; a++){
    for(int f = 0; f < RL_BASE_FEATURES; f++) text += f == 4 && a == 1 ? "10 " : "0 ";
    text += "\n";
  }
  text += "1 2 3 4 5\n";
  rl_model model;
  std::string error;
  CHECK(model.parse(text, error));
  CHECK_EQ(model.inputs(), RL_BASE_FEATURES);
  std::array<float, RL_FEATURES> features;
  features.fill(1);                                       // the opponent numbers are ignored
  features[4] = 0.5f;
  std::array<float, RL_ACTIONS> scores = model.scores(features);
  CHECK(std::fabs(scores[1] - 7) < 1e-5 && std::fabs(scores[4] - 5) < 1e-5);
  // The built-in network keeps playing whatever its size.
  if(rl_current_model()) CHECK(rl_current_model()->inputs() == RL_BASE_FEATURES || rl_current_model()->inputs() == RL_FEATURES);
}

TEST(rl_env_sessions_keep_the_opponents_and_learn_them){
  rl_env_options options;
  options.seed = 5;
  options.session_hands = 200;
  options.opponents = {{bot_style::CALLING, 1}};
  rl_env env(options);
  std::mt19937 rng(2);
  const int M = RL_BASE_FEATURES;
  float last_seen = 0, aggression = 1;
  for(int i = 0; i < 300; i++){
    const rl_observation& seen = env.observation();
    last_seen = seen.features[M + 4];
    aggression = seen.features[M + 2];
    env.step(seen.allowed[RL_RAISE_HALF] && rng() % 2 ? RL_RAISE_HALF : RL_CALL);
  }
  CHECK_EQ(env.illegal_moves(), 0);
  CHECK(last_seen > 0.3);                                 // the caller has been watched for a while
  CHECK(aggression < 0.2);                                // and never bets

  // Sessions that switch style still play legal hands and repeat with the seed.
  options.opponents = {{bot_style::CALLING, 1}, {bot_style::RULES, 1}, {bot_style::RANDOM, 1}};
  options.players = 3;
  options.session_hands = 20;
  options.switch_percent = 100;
  std::vector<float> runs[2];
  for(int run = 0; run < 2; run++){
    rl_env again(options);
    for(int i = 0; i < 400; i++){
      rl_step step = again.step(i % RL_ACTIONS);
      runs[run].push_back(step.reward + step.other_net + again.observation().features[M + 4]);
    }
    CHECK_EQ(again.illegal_moves(), 0);
  }
  CHECK(runs[0] == runs[1]);
}

TEST(match_seat_can_change_style_without_warning){
  match_options options;
  options.hands = 300;
  options.switch_after = 150;
  options.styles_after = {bot_style::RULES, bot_style::CALLING};
  match_result r = run_match({bot_style::RULES, bot_style::RANDOM}, options);
  CHECK_EQ(r.illegal_moves, 0);
  int raises_before = 0, raises_after = 0;
  for(size_t h = 0; h < r.records.size(); h++){
    for(const json& a : r.records[h]["actions"]){
      bool raise = a["seat"] == 1 && (a["action"] == "raise" || a["action"] == "bet");
      (h < 150 ? raises_before : raises_after) += raise;
    }
  }
  CHECK(raises_before > 10);      // random raises now and then
  CHECK_EQ(raises_after, 0);      // a calling station never does
  CHECK(r.seats[1].name == "random-2");   // same name: nobody was told
}
