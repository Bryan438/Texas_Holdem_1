#include <algorithm>
#include <cmath>
#include <set>
#include "bot.h"
#include "equity.h"
#include "hand_evaluator.h"

bool parse_bot_style(const std::string& name, bot_style& out){
  if(name == "random") out = bot_style::RANDOM;
  else if(name == "calling") out = bot_style::CALLING;
  else if(name == "rules") out = bot_style::RULES;
  else if(name == "equity") out = bot_style::EQUITY;
  else return false;
  return true;
}

const char* bot_style_name(bot_style style){
  switch(style){
    case bot_style::RANDOM: return "random";
    case bot_style::CALLING: return "calling";
    case bot_style::RULES: return "rules";
    case bot_style::EQUITY: return "equity";
  }
  return "?";
}

const char* bot_style_list(){
  return "random, calling, rules, equity";
}

json bot_move::to_message() const{
  json msg = {{"type", "action"}, {"action", action}};
  if(action == "raise"){
    msg["amount"] = amount;
  }
  return msg;
}

const char* hand_strength_name(hand_strength s){
  switch(s){
    case hand_strength::AIR: return "air";
    case hand_strength::DRAW: return "draw";
    case hand_strength::PAIR: return "pair";
    case hand_strength::STRONG: return "strong";
    case hand_strength::MONSTER: return "monster";
  }
  return "?";
}

// ---------------------------------------------------------------- hand reading

int chen_score(const Card& a, const Card& b){
  int hi = std::max(a.get_number(), b.get_number());
  int lo = std::min(a.get_number(), b.get_number());
  // Points for the high card, in half points so 10-2 (rank / 2) stay whole.
  auto half_points = [](int rank){
    switch(rank){
      case 14: return 20;
      case 13: return 16;
      case 12: return 14;
      case 11: return 12;
      default: return rank;
    }
  };
  int score = half_points(hi);
  if(hi == lo){
    score = std::max(score * 2, 10);          // pairs: double, at least 5
  }
  else{
    if(a.get_suit() == b.get_suit()) score += 4;
    int gap = hi - lo - 1;
    static const int gap_penalty[] = {0, 2, 4, 8, 10};
    score -= gap_penalty[std::min(gap, 4)];
    if(gap <= 1 && hi < 12) score += 2;       // connected low cards make straights
  }
  return (int)std::ceil(score / 2.0);
}

// Pair / trips / quads made by the board alone (3 or 4 cards: no straights or flushes yet).
static int board_category(const std::vector<Card>& board){
  if(board.size() >= 5){
    const Card* cards[5];
    for(int i = 0; i < 5; i++) cards[i] = &board[i];
    return hand_category_of(evaluate_best(cards, 5));
  }
  int count[15] = {0};
  for(const Card& c : board) count[c.get_number()]++;
  int pairs = 0, most = 0;
  for(int r = 2; r <= 14; r++){
    pairs += count[r] == 2;
    most = std::max(most, count[r]);
  }
  if(most == 4) return FOUR_OF_A_KIND;
  if(most == 3) return THREE_OF_A_KIND;
  if(pairs == 2) return TWO_PAIR;
  if(pairs == 1) return ONE_PAIR;
  return HIGH_CARD;
}

static bool has_straight(const std::set<int>& ranks){
  for(int low = 1; low <= 10; low++){
    bool all = true;
    for(int r = low; r < low + 5 && all; r++){
      all = ranks.count(r == 1 ? 14 : r) > 0;
    }
    if(all) return true;
  }
  return false;
}

// Ranks that would give these ranks a straight.
static std::set<int> straight_makers(const std::set<int>& ranks){
  std::set<int> makers;
  if(has_straight(ranks)) return makers;
  for(int r = 2; r <= 14; r++){
    if(ranks.count(r)) continue;
    std::set<int> with = ranks;
    with.insert(r);
    if(has_straight(with)) makers.insert(r);
  }
  return makers;
}

static int draw_outs(const std::vector<Card>& hole, const std::vector<Card>& board){
  int outs = 0;
  for(card_suit suit : {Diamond, Spade, Heart, Club}){
    int in_hole = 0, total = 0;
    for(const Card& c : hole) in_hole += c.get_suit() == suit;
    total = in_hole;
    for(const Card& c : board) total += c.get_suit() == suit;
    if(total == 4 && in_hole > 0) outs += 9;   // 13 of the suit minus the 4 we see
  }

  std::set<int> all, board_only;
  for(const Card& c : board){
    all.insert(c.get_number());
    board_only.insert(c.get_number());
  }
  for(const Card& c : hole) all.insert(c.get_number());
  // Only count straights the hole cards help make.
  std::set<int> mine = straight_makers(all), shared = straight_makers(board_only);
  int makers = 0;
  for(int r : mine) makers += shared.count(r) == 0;
  outs += 4 * std::min(makers, 2);             // open-ended: 8 outs, gutshot: 4
  return std::min(outs, 15);
}

hand_read read_hand(const std::vector<Card>& hole, const std::vector<Card>& board){
  hand_read read;
  std::vector<const Card*> cards;
  for(const Card& c : hole) cards.push_back(&c);
  for(const Card& c : board) cards.push_back(&c);
  int category = hand_category_of(evaluate_best(cards.data(), cards.size()));
  int on_board = board_category(board);

  int top = 0;
  for(const Card& c : board) top = std::max(top, c.get_number());
  int h0 = hole[0].get_number(), h1 = hole[1].get_number();
  int my_pair = 0;   // rank of a pair that uses a hole card
  if(h0 == h1){
    my_pair = h0;
  }
  else{
    for(const Card& c : board){
      if(c.get_number() == h0 || c.get_number() == h1){
        my_pair = std::max(my_pair, c.get_number());
      }
    }
  }

  if((category >= STRAIGHT && category > on_board) ||
     (category == THREE_OF_A_KIND && on_board < THREE_OF_A_KIND)){
    read.strength = hand_strength::MONSTER;
  }
  else if(category == TWO_PAIR && on_board == HIGH_CARD){
    read.strength = hand_strength::STRONG;
  }
  else if(my_pair > 0){
    read.strength = my_pair >= top ? hand_strength::STRONG : hand_strength::PAIR;
  }
  if(board.size() < 5 && read.strength < hand_strength::STRONG){
    read.outs = draw_outs(hole, board);
    if(read.strength == hand_strength::AIR && read.outs > 0){
      read.strength = hand_strength::DRAW;
    }
  }
  return read;
}

// ---------------------------------------------------------------- decisions

namespace {

// The parts of a state message a bot decides from.
struct view{
  std::string stage;
  int pot = 0;
  int current_bet = 0;
  int big_blind = 2;
  int to_call = 0;         // chips needed to call (0: can check)
  bool can_check = false;
  bool can_raise = false;
  int raise_min = 0, raise_max = 0;
  int opponents = 0;       // others still in the hand
  std::vector<Card> hole, board;
};

view read_view(const json& st){
  view v;
  v.stage = st.value("stage", "");
  v.pot = st.value("pot", 0);
  v.current_bet = st.value("current_bet", 0);
  if(st.contains("blinds") && st["blinds"].size() == 2){
    v.big_blind = st["blinds"][1].get<int>();
  }
  const json& actions = st["actions"];
  v.can_check = actions.value("check", false);
  v.to_call = actions.value("call", 0);
  if(actions.contains("raise") && actions["raise"].is_object()){
    v.can_raise = true;
    v.raise_min = actions["raise"]["min"].get<int>();
    v.raise_max = actions["raise"]["max"].get<int>();
  }
  int you = st.value("you", -1);
  for(const json& s : st.value("seats", json::array())){
    std::string status = s.value("status", "");
    if(s.value("seat", -1) == you){
      if(s.contains("cards") && s["cards"].is_array()){
        for(const json& code : s["cards"]){
          Card c;
          if(card_from_code(code.get<std::string>(), c)) v.hole.push_back(c);
        }
      }
    }
    else if(status == "active" || status == "allin"){
      v.opponents++;
    }
  }
  for(const json& code : st.value("board", json::array())){
    Card c;
    if(card_from_code(code.get<std::string>(), c)) v.board.push_back(c);
  }
  return v;
}

bot_move fold(){ return {"fold", 0}; }
bot_move check_or_call(const view& v){ return v.can_check ? bot_move{"check", 0} : bot_move{"call", 0}; }
bot_move check_or_fold(const view& v){ return v.can_check ? bot_move{"check", 0} : fold(); }

// Raise to `to`, kept inside the allowed range (the top of it is all in).
bot_move raise_to(const view& v, int to){
  if(!v.can_raise){
    return check_or_call(v);
  }
  return {"raise", std::max(v.raise_min, std::min(to, v.raise_max))};
}

// Bet or raise by `chips` on top of the current bet.
bot_move bet(const view& v, int chips){
  return raise_to(v, v.current_bet + std::max(chips, v.big_blind));
}

bool chance(std::mt19937& rng, int percent){
  return (int)(rng() % 100) < percent;
}

bot_move decide_random(const view& v, std::mt19937& rng){
  int roll = rng() % 10;
  if(!v.can_check && roll < 3){
    return fold();
  }
  if(v.can_raise && roll >= 8){
    if(roll == 9 && chance(rng, 30)){
      return raise_to(v, v.raise_max);
    }
    int top = std::max(v.raise_min, std::min(v.raise_max, v.raise_min * 3));
    return raise_to(v, v.raise_min + (int)(rng() % (top - v.raise_min + 1)));
  }
  return check_or_call(v);
}

bot_move decide_preflop(const view& v){
  int chen = chen_score(v.hole[0], v.hole[1]);
  int bb = v.big_blind;
  bool unraised = v.current_bet <= bb;
  if(chen >= 12){                                  // AA-JJ, AK suited
    if(unraised) return raise_to(v, 3 * bb);
    if(chen >= 14 || v.current_bet <= 4 * bb) return raise_to(v, 3 * v.current_bet);
    return check_or_call(v);
  }
  if(chen >= 9){                                   // TT-99, AK, AQ suited, JT suited
    if(unraised) return raise_to(v, 3 * bb);
    if(v.current_bet <= 4 * bb) return check_or_call(v);
    return check_or_fold(v);
  }
  if(chen >= 7){                                   // 77-88, suited aces, suited connectors, KQ
    if(v.current_bet <= 3 * bb) return check_or_call(v);
    return check_or_fold(v);
  }
  if(chen >= 5 && unraised){
    return check_or_call(v);
  }
  return check_or_fold(v);
}

bot_move decide_postflop(const view& v, std::mt19937& rng){
  hand_read hand = read_hand(v.hole, v.board);
  // The share of the final pot a call costs: calling is worth it when the
  // chance of winning is at least this.
  double price = v.to_call == 0 ? 0 : (double)v.to_call / (v.pot + v.to_call);
  switch(hand.strength){
    case hand_strength::MONSTER:
      if(v.to_call == 0) return bet(v, v.pot * 2 / 3);
      return raise_to(v, 3 * v.current_bet);
    case hand_strength::STRONG:
      if(v.to_call == 0) return bet(v, v.pot / 2);
      return price <= 0.34 ? check_or_call(v) : fold();      // up to a pot-sized bet
    case hand_strength::PAIR:
      if(v.to_call == 0) return check_or_call(v);
      return price <= 0.25 ? check_or_call(v) : fold();      // up to a half-pot bet
    case hand_strength::DRAW:{
      // Rule of 4 and 2: each out is worth about 4% with two cards to come, 2% with one.
      double equity = hand.outs * (v.stage == "flop" ? 0.04 : 0.02);
      if(v.to_call == 0){
        if(hand.outs >= 8 && chance(rng, 30)) return bet(v, v.pot / 2);   // semi-bluff
        return check_or_call(v);
      }
      return equity >= price ? check_or_call(v) : fold();
    }
    case hand_strength::AIR:
      if(v.to_call == 0 && v.opponents == 1 && chance(rng, 15)) return bet(v, v.pot / 2);   // bluff
      return check_or_fold(v);
  }
  return check_or_fold(v);
}

bot_move decide_equity(const view& v, std::mt19937& rng){
  int opponents = std::max(1, v.opponents);
  // Facing a bet after the flop, assume the bettor holds something, and
  // something good when the bet is half the pot or more.
  opponent_range range = opponent_range::ANY;
  if(!v.board.empty() && v.to_call > 0){
    int pot_before = std::max(1, v.pot - v.to_call);
    range = 2 * v.to_call >= pot_before ? opponent_range::STRONG : opponent_range::CONNECTED;
  }
  double equity = v.board.empty() ? preflop_equity(v.hole[0], v.hole[1], opponents)
                                  : estimate_equity(v.hole, v.board, opponents, 1000, rng, range);
  // With n opponents an average hand wins 1 / (n + 1) of the time; edge says
  // how much better than average this hand is.
  double edge = equity * (opponents + 1);
  double price = v.to_call == 0 ? 0 : (double)v.to_call / (v.pot + v.to_call);
  int bb = v.big_blind;

  if(v.board.empty()){
    if(v.current_bet <= bb){
      if(edge >= 1.3) return raise_to(v, 3 * bb);
      if(edge >= 0.9) return check_or_call(v);
      return check_or_fold(v);
    }
    if(edge >= 1.5) return raise_to(v, 3 * v.current_bet);
    if(edge >= 1.15 && equity >= price && v.current_bet <= 5 * bb) return check_or_call(v);
    if(edge >= 1.3 && equity >= price) return check_or_call(v);
    return check_or_fold(v);
  }

  if(v.to_call == 0){
    if(edge >= 1.6) return bet(v, v.pot * 3 / 4);
    if(edge >= 1.2) return bet(v, v.pot / 2);
    if(opponents == 1 && chance(rng, 10)) return bet(v, v.pot / 2);   // bluff
    return check_or_call(v);
  }
  if(edge >= 1.7) return raise_to(v, 3 * v.current_bet);
  if(equity >= price) return check_or_call(v);
  return fold();
}

}  // namespace

bot_move decide(const json& state, bot_style style, std::mt19937& rng){
  if(!state.contains("actions") || !state["actions"].is_object()){
    return {"check", 0};
  }
  view v = read_view(state);
  switch(style){
    case bot_style::RANDOM:
      return decide_random(v, rng);
    case bot_style::CALLING:
      return check_or_call(v);
    case bot_style::RULES:
      if(v.hole.size() != 2) return check_or_fold(v);
      if(v.board.empty()) return decide_preflop(v);
      return decide_postflop(v, rng);
    case bot_style::EQUITY:
      if(v.hole.size() != 2) return check_or_fold(v);
      return decide_equity(v, rng);
  }
  return check_or_fold(v);
}
