#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include "rl_policy.h"
#include "equity.h"

// ---------------------------------------------------------------- the spot as numbers

// Deals simulated per equity estimate: fewer than the equity bot uses, since
// the network only needs a rough number and training asks for millions.
static const int EQUITY_DEALS = 400;

static int raise_target(const bot_view& v, int action){
  int after_call = v.pot + v.to_call;
  int to = v.raise_max;
  if(action == RL_RAISE_HALF) to = v.current_bet + after_call / 2;
  if(action == RL_RAISE_POT) to = v.current_bet + after_call;
  return std::max(v.raise_min, std::min(to, v.raise_max));
}

bot_move rl_move(const bot_view& v, int action){
  switch(action){
    case RL_FOLD: return check_or_fold(v);
    case RL_RAISE_HALF:
    case RL_RAISE_POT:
    case RL_ALL_IN: return raise_to(v, raise_target(v, action));
    default: return check_or_call(v);
  }
}

rl_observation rl_observe(const bot_view& v, std::mt19937& rng){
  rl_observation seen;
  seen.features.fill(0);
  int half = raise_target(v, RL_RAISE_HALF), pot = raise_target(v, RL_RAISE_POT);
  seen.allowed[RL_FOLD] = !v.can_check;
  seen.allowed[RL_CALL] = true;
  seen.allowed[RL_RAISE_HALF] = v.can_raise && half < v.raise_max;
  seen.allowed[RL_RAISE_POT] = v.can_raise && pot < v.raise_max && pot > half;
  seen.allowed[RL_ALL_IN] = v.can_raise;
  if(v.hole.size() != 2){
    return seen;
  }

  double bb = std::max(1, v.big_blind);
  int opponents = std::max(1, v.opponents);
  bool preflop = v.board.empty();
  // Chips as big blinds on a log scale: 1 bb is about 0.13, 100 bb about 0.87.
  auto chips = [&](int amount){ return (float)(std::log1p(std::max(0, amount) / bb) / 5.3); };

  // Chance of winning against any two cards, and against the hands a bettor
  // is likely to hold (the same ranges the equity bot assumes).
  double equity_any = preflop ? preflop_equity(v.hole[0], v.hole[1], opponents)
                              : estimate_equity(v.hole, v.board, opponents, EQUITY_DEALS, rng);
  double equity_range = equity_any;
  int pot_before = std::max(1, v.pot - v.to_call);
  if(!preflop && v.to_call > 0){
    opponent_range range = 2 * v.to_call >= pot_before ? opponent_range::STRONG : opponent_range::CONNECTED;
    equity_range = estimate_equity(v.hole, v.board, opponents, EQUITY_DEALS, rng, range);
  }

  int n = 0;
  auto add = [&](double value){ seen.features[n++] = (float)value; };
  // Stage.
  add(preflop);
  add(v.board.size() == 3);
  add(v.board.size() == 4);
  add(v.board.size() == 5);
  // How good the hand is.
  add(equity_any);
  add(equity_range);
  add(std::min(equity_any * (opponents + 1), 4.0) / 4);   // against an average hand
  // What it costs to go on.
  add(v.to_call == 0 ? 0 : (double)v.to_call / (v.pot + v.to_call));
  add(std::min((double)v.to_call / pot_before, 3.0) / 3);  // the bet as a share of the pot
  add(chips(v.pot));
  add(chips(v.to_call));
  add(chips(v.current_bet));
  add(chips(v.stack));
  add(chips(v.round_bet));
  add(chips(v.total_bet));
  add(chips(v.opponent_stack));
  add(std::min((double)v.stack / std::max(1, v.pot), 10.0) / 10);
  add(v.can_check);
  add(v.can_raise);
  // The others.
  add(std::min(opponents, 5) / 5.0);
  add(v.opponents_all_in > 0);
  // Position.
  add(v.on_button);
  add(v.small_blind);
  add(v.is_big_blind);
  // The two hole cards.
  int high = std::max(v.hole[0].get_number(), v.hole[1].get_number());
  int low = std::min(v.hole[0].get_number(), v.hole[1].get_number());
  add(chen_score(v.hole[0], v.hole[1]) / 20.0);
  add(high == low);
  add(v.hole[0].get_suit() == v.hole[1].get_suit());
  add(high / 14.0);
  add(low / 14.0);
  // What they make with the board, as the rules bot reads it.
  hand_read hand;
  if(!preflop) hand = read_hand(v.hole, v.board);
  add(!preflop && hand.strength == hand_strength::AIR);
  add(!preflop && hand.strength == hand_strength::DRAW);
  add(!preflop && hand.strength == hand_strength::PAIR);
  add(!preflop && hand.strength == hand_strength::STRONG);
  add(!preflop && hand.strength == hand_strength::MONSTER);
  add(hand.outs / 15.0);
  // The board itself.
  int rank_count[15] = {0}, suit_count[5] = {0};
  bool paired = false, three_of_a_suit = false;
  for(const Card& c : v.board){
    paired = paired || ++rank_count[c.get_number()] >= 2;
    three_of_a_suit = three_of_a_suit || ++suit_count[c.get_suit()] >= 3;
  }
  add(paired);
  add(three_of_a_suit);
  // n == RL_FEATURES: tests/test_rl.cpp checks that the last feature is used.
  return seen;
}

// ---------------------------------------------------------------- the network

bool rl_model::parse(const std::string& text, std::string& error){
  std::istringstream in(text);
  std::string word;
  int version = 0, features = 0, actions = 0, count = 0;
  if(!(in >> word >> version) || word != "thrl" || version != 1){
    error = "not a model file (it should start with \"thrl 1\")";
    return false;
  }
  if(!(in >> word >> features >> word >> actions >> word >> count) || count < 1 || count > 16){
    error = "the model's first line is damaged";
    return false;
  }
  if(features != RL_FEATURES || actions != RL_ACTIONS){
    error = "the model was trained for " + std::to_string(features) + " inputs and " +
            std::to_string(actions) + " actions, this program uses " + std::to_string(RL_FEATURES) +
            " and " + std::to_string(RL_ACTIONS) + ": train it again";
    return false;
  }
  std::vector<layer> read;
  int inputs = features;
  for(int i = 0; i < count; i++){
    layer l;
    if(!(in >> word >> l.inputs >> l.outputs) || word != "layer" || l.inputs != inputs ||
       l.outputs < 1 || l.outputs > 4096){
      error = "layer " + std::to_string(i + 1) + " of the model is damaged";
      return false;
    }
    l.weights.resize((size_t)l.inputs * l.outputs);
    l.bias.resize(l.outputs);
    for(float& w : l.weights) in >> w;
    for(float& b : l.bias) in >> b;
    if(!in){
      error = "the model file ends too early";
      return false;
    }
    bool finite = true;
    for(float w : l.weights) finite = finite && std::isfinite(w);
    for(float b : l.bias) finite = finite && std::isfinite(b);
    if(!finite){
      error = "the model holds a number that is not finite";
      return false;
    }
    inputs = l.outputs;
    read.push_back(std::move(l));
  }
  if(inputs != actions){
    error = "the model's last layer does not have one output per action";
    return false;
  }
  layers = std::move(read);
  return true;
}

bool rl_model::load_file(const std::string& path, std::string& error){
  std::ifstream file(path);
  if(!file){
    error = "cannot open " + path;
    return false;
  }
  std::stringstream text;
  text << file.rdbuf();
  if(!parse(text.str(), error)){
    error = path + ": " + error;
    return false;
  }
  return true;
}

std::array<float, RL_ACTIONS> rl_model::scores(const std::array<float, RL_FEATURES>& features) const{
  std::vector<float> values(features.begin(), features.end()), next;
  for(size_t i = 0; i < layers.size(); i++){
    const layer& l = layers[i];
    next.assign(l.outputs, 0);
    for(int o = 0; o < l.outputs; o++){
      const float* row = &l.weights[(size_t)o * l.inputs];
      float sum = l.bias[o];
      for(int k = 0; k < l.inputs; k++) sum += row[k] * values[k];
      next[o] = i + 1 < layers.size() ? std::tanh(sum) : sum;
    }
    values.swap(next);
  }
  std::array<float, RL_ACTIONS> out;
  out.fill(0);
  for(size_t a = 0; a < out.size() && a < values.size(); a++) out[a] = values[a];
  return out;
}

int rl_choose(const rl_model& model, const rl_observation& seen, std::mt19937& rng){
  std::array<float, RL_ACTIONS> scores = model.scores(seen.features);
  double best = -1e30;
  for(int a = 0; a < RL_ACTIONS; a++){
    if(seen.allowed[a]) best = std::max(best, (double)scores[a]);
  }
  double weight[RL_ACTIONS], total = 0;
  for(int a = 0; a < RL_ACTIONS; a++){
    weight[a] = seen.allowed[a] ? std::exp(scores[a] - best) : 0;
    total += weight[a];
  }
  double roll = rng() / 4294967296.0 * total;
  int chosen = RL_CALL;
  for(int a = 0; a < RL_ACTIONS; a++){
    if(weight[a] <= 0) continue;
    chosen = a;
    roll -= weight[a];
    if(roll < 0) break;
  }
  return chosen;
}

// ---------------------------------------------------------------- the model in use

static rl_model& model_in_use(){
  static rl_model model = []{
    rl_model built_in;
    std::string error;
    if(RL_BUILT_IN_MODEL[0] != '\0') built_in.parse(RL_BUILT_IN_MODEL, error);
    return built_in;
  }();
  return model;
}

const rl_model* rl_current_model(){
  rl_model& model = model_in_use();
  return model.ready() ? &model : nullptr;
}

bool rl_use_model_file(const std::string& path, std::string& error){
  rl_model loaded;
  if(!loaded.load_file(path, error)) return false;
  model_in_use() = std::move(loaded);
  return true;
}
