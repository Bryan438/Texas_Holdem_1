// Bots: reading cards and hands, the moves each style picks, and whole
// matches of bots against each other on a real table.
#include <string>
#include <vector>
#include "test_framework.h"
#include "bot.h"
#include "match.h"
#include "stats.h"

static Card card(const char* code){
  Card c;
  card_from_code(code, c);
  return c;
}

static std::vector<Card> cards(std::initializer_list<const char*> codes){
  std::vector<Card> out;
  for(const char* code : codes) out.push_back(card(code));
  return out;
}

// A state message for the bot in seat 0 on its turn, with `opponents` others in the hand.
static json turn_state(std::vector<std::string> hole, std::vector<std::string> board, int pot,
                       int current_bet, int to_call, int raise_min, int raise_max, int opponents = 1){
  static const char* stages[] = {"preflop", "", "", "flop", "turn", "river"};
  json seats = json::array();
  seats.push_back({{"seat", 0}, {"status", "active"}, {"cards", hole}});
  for(int i = 1; i <= opponents; i++){
    seats.push_back({{"seat", i}, {"status", "active"}, {"cards", nullptr}});
  }
  json actions = {{"fold", true}, {"check", to_call == 0}, {"call", to_call}};
  if(raise_max > 0) actions["raise"] = {{"min", raise_min}, {"max", raise_max}};
  else actions["raise"] = nullptr;
  return {{"type", "state"}, {"stage", stages[board.size()]}, {"you", 0}, {"turn", 0},
          {"pot", pot}, {"current_bet", current_bet}, {"blinds", {1, 2}}, {"board", board},
          {"seats", seats}, {"actions", actions}};
}

static bot_move rules(const json& state, unsigned seed = 1){
  std::mt19937 rng(seed);
  return decide(state, bot_style::RULES, rng);
}

TEST(bot_card_codes_round_trip){
  for(card_suit suit : {Diamond, Spade, Heart, Club}){
    for(int rank = 2; rank <= 14; rank++){
      Card c;
      CHECK(card_from_code(Card(suit, rank).code(), c));
      CHECK(c.get_suit() == suit && c.get_number() == rank);
    }
  }
  Card c;
  CHECK(!card_from_code("", c));
  CHECK(!card_from_code("1h", c));
  CHECK(!card_from_code("Tx", c));
  CHECK(!card_from_code("Tdd", c));
}

TEST(bot_chen_scores){
  CHECK_EQ(chen_score(card("As"), card("Ah")), 20);
  CHECK_EQ(chen_score(card("Ks"), card("Kh")), 16);
  CHECK_EQ(chen_score(card("2s"), card("2h")), 5);    // pairs score at least 5
  CHECK_EQ(chen_score(card("As"), card("Ks")), 12);   // 10 + 2 suited
  CHECK_EQ(chen_score(card("Ad"), card("Ks")), 10);
  CHECK_EQ(chen_score(card("Jh"), card("Th")), 9);    // 6 + 2 suited + 1 connected below Q
  CHECK_EQ(chen_score(card("7d"), card("2c")), -1);   // 3.5 - 5 for the gap, rounded up
}

TEST(bot_reads_made_hands){
  CHECK(read_hand(cards({"Ah", "Kd"}), cards({"Ks", "7c", "2d"})).strength == hand_strength::STRONG);   // top pair
  CHECK(read_hand(cards({"Qh", "Qd"}), cards({"Js", "7c", "2d"})).strength == hand_strength::STRONG);   // overpair
  CHECK(read_hand(cards({"7h", "7d"}), cards({"Ks", "7c", "2d"})).strength == hand_strength::MONSTER);  // set
  CHECK(read_hand(cards({"Kh", "7d"}), cards({"Ks", "7c", "2d"})).strength == hand_strength::STRONG);   // two pair
  CHECK(read_hand(cards({"9h", "8d"}), cards({"Ks", "8c", "2d"})).strength == hand_strength::PAIR);     // second pair
  CHECK(read_hand(cards({"5h", "4d"}), cards({"6s", "7c", "8d"})).strength == hand_strength::MONSTER);  // straight
  // The pair is on the board, so everyone has it: it is not worth anything to us.
  CHECK(read_hand(cards({"Ah", "4d"}), cards({"Ks", "Kc", "8d", "9h", "Js"})).strength == hand_strength::AIR);
  // The straight is on the board: same.
  CHECK(read_hand(cards({"2h", "2d"}), cards({"9s", "Tc", "Jd", "Qh", "Ks"})).strength != hand_strength::MONSTER);
}

TEST(bot_counts_draw_outs){
  hand_read flush = read_hand(cards({"Ah", "4h"}), cards({"Kh", "9h", "2c"}));
  CHECK(flush.strength == hand_strength::DRAW);
  CHECK_EQ(flush.outs, 9);
  hand_read open_ended = read_hand(cards({"9s", "8d"}), cards({"7h", "6c", "2d"}));
  CHECK_EQ(open_ended.outs, 8);
  hand_read gutshot = read_hand(cards({"9s", "8d"}), cards({"6h", "5c", "Kd"}));
  CHECK_EQ(gutshot.outs, 4);
  hand_read both = read_hand(cards({"9h", "8h"}), cards({"7h", "6c", "2h"}));
  CHECK_EQ(both.outs, 15);   // 9 + 8, minus the two cards counted twice (capped)
  CHECK_EQ(read_hand(cards({"Ah", "4h"}), cards({"Kh", "9h", "2c", "3s", "Jd"})).outs, 0);   // river: no more cards
}

TEST(bot_rules_preflop){
  // Aces, nobody has raised: raise to 3 big blinds.
  bot_move m = rules(turn_state({"As", "Ah"}, {}, 3, 2, 2, 4, 200));
  CHECK(m.action == "raise");
  CHECK_EQ(m.amount, 6);
  // Aces against a raise to 8: re-raise to 24.
  m = rules(turn_state({"As", "Ah"}, {}, 11, 8, 8, 14, 200));
  CHECK(m.action == "raise");
  CHECK_EQ(m.amount, 24);
  // 7-2 offsuit against a raise: fold. In the big blind with a free check: check.
  CHECK(rules(turn_state({"7d", "2c"}, {}, 11, 8, 6, 14, 198)).action == "fold");
  CHECK(rules(turn_state({"7d", "2c"}, {}, 4, 2, 0, 4, 198)).action == "check");
  // A middle pair (Chen 8) calls a small raise but not a big one.
  CHECK(rules(turn_state({"8d", "8c"}, {}, 9, 6, 6, 10, 200)).action == "call");
  CHECK(rules(turn_state({"8d", "8c"}, {}, 43, 40, 40, 78, 200)).action == "fold");
}

TEST(bot_rules_after_the_flop){
  // A set with nobody betting: bet two thirds of the pot.
  bot_move m = rules(turn_state({"7h", "7d"}, {"Ks", "7c", "2d"}, 12, 0, 0, 2, 190));
  CHECK(m.action == "raise");
  CHECK_EQ(m.amount, 8);
  // A set facing a bet of 10: raise to 30.
  m = rules(turn_state({"7h", "7d"}, {"Ks", "7c", "2d"}, 22, 10, 10, 20, 190));
  CHECK(m.action == "raise" && m.amount == 30);
  // Top pair calls a half-pot bet, folds to a bet three times the pot.
  CHECK(rules(turn_state({"Ah", "Kd"}, {"Ks", "7c", "2d"}, 18, 6, 6, 12, 190)).action == "call");
  CHECK(rules(turn_state({"Ah", "Kd"}, {"Ks", "7c", "2d"}, 48, 36, 36, 72, 190)).action == "fold");
  // Nothing at all, facing a bet: fold.
  CHECK(rules(turn_state({"4h", "3d"}, {"Ks", "Jc", "9d"}, 18, 6, 6, 12, 190)).action == "fold");
  // A flush draw on the flop (9 outs, about 36%): call a small bet, fold to a huge one.
  CHECK(rules(turn_state({"Ah", "4h"}, {"Kh", "9h", "2c"}, 15, 3, 3, 6, 190)).action == "call");
  CHECK(rules(turn_state({"Ah", "4h"}, {"Kh", "9h", "2c"}, 112, 100, 100, 190, 190)).action == "fold");
}

TEST(bot_raises_stay_in_the_allowed_range){
  // The set wants to raise to 30, but only 25 is left: it goes all in instead.
  bot_move m = rules(turn_state({"7h", "7d"}, {"Ks", "7c", "2d"}, 22, 10, 10, 20, 25));
  CHECK(m.action == "raise");
  CHECK_EQ(m.amount, 25);
  // No raise allowed (the bet covers the stack): call instead.
  CHECK(rules(turn_state({"7h", "7d"}, {"Ks", "7c", "2d"}, 70, 60, 50, 0, 0)).action == "call");
}

TEST(bot_calling_station_never_folds_or_raises){
  std::mt19937 rng(3);
  CHECK(decide(turn_state({"7d", "2c"}, {}, 403, 400, 200, 0, 0), bot_style::CALLING, rng).action == "call");
  CHECK(decide(turn_state({"As", "Ah"}, {}, 4, 2, 0, 4, 200), bot_style::CALLING, rng).action == "check");
}

TEST(bot_match_every_style_plays_legal_moves){
  match_options options;
  options.hands = 300;
  options.seed = 5;
  match_result r = run_match({bot_style::RULES, bot_style::CALLING, bot_style::RANDOM,
                              bot_style::EQUITY, bot_style::RANDOM, bot_style::EQUITY}, options);
  CHECK_EQ(r.hands, 300);
  CHECK_EQ(r.illegal_moves, 0);
  long long total = 0;
  for(const match_seat& s : r.seats) total += s.net;
  CHECK_EQ(total, 0);                       // chips only move between bots
  int bad = 0;
  for(const json& hand : r.records){
    bad += !check_hand(hand).empty();
    for(const json& p : hand["players"]) bad += p["stack"] != 200;   // everyone starts every hand at 200
  }
  CHECK_EQ(bad, 0);
}

TEST(bot_match_same_seed_same_result){
  match_options options;
  options.hands = 100;
  options.seed = 9;
  match_result a = run_match({bot_style::RULES, bot_style::RANDOM}, options);
  match_result b = run_match({bot_style::RULES, bot_style::RANDOM}, options);
  CHECK_EQ(a.seats[0].net, b.seats[0].net);
  CHECK(a.records.back()["players"][0]["cards"] == b.records.back()["players"][0]["cards"]);
  options.seed = 10;
  match_result c = run_match({bot_style::RULES, bot_style::RANDOM}, options);
  CHECK(a.records.back()["players"][0]["cards"] != c.records.back()["players"][0]["cards"]);
}

// The point of the rules bot: it should beat the baselines.
TEST(bot_rules_beats_calling_station_and_random){
  match_options options;
  options.hands = 1500;
  match_result r = run_match({bot_style::RULES, bot_style::CALLING}, options);
  CHECK(bb_per_100(r.seats[0], r) - bb_per_100_margin(r.seats[0], r) > 0);
  r = run_match({bot_style::RULES, bot_style::RANDOM}, options);
  CHECK(bb_per_100(r.seats[0], r) - bb_per_100_margin(r.seats[0], r) > 0);
}

static bot_move equity_bot(const json& state){
  std::mt19937 rng(1);
  return decide(state, bot_style::EQUITY, rng);
}

TEST(bot_equity_decisions){
  // Aces preflop: raise to 3 big blinds.
  bot_move m = equity_bot(turn_state({"As", "Ah"}, {}, 3, 2, 2, 4, 200));
  CHECK(m.action == "raise" && m.amount == 6);
  // 7-2 offsuit against a raise: fold.
  CHECK(equity_bot(turn_state({"7d", "2c"}, {}, 11, 8, 6, 14, 198)).action == "fold");
  // The nut flush on the river: bet three quarters of the pot, raise a bet.
  m = equity_bot(turn_state({"Ah", "Kh"}, {"9h", "4h", "2h", "3c", "Td"}, 20, 0, 0, 2, 190));
  CHECK(m.action == "raise" && m.amount == 15);
  m = equity_bot(turn_state({"Ah", "Kh"}, {"9h", "4h", "2h", "3c", "Td"}, 40, 20, 20, 40, 190));
  CHECK(m.action == "raise" && m.amount == 60);
  // Nothing on the river against a pot-sized bet: fold.
  CHECK(equity_bot(turn_state({"4c", "3d"}, {"Ks", "Jc", "9d", "8h", "2s"}, 40, 20, 20, 40, 190)).action == "fold");
  // A flush draw on the flop against a small bet: the price is right, call.
  CHECK(equity_bot(turn_state({"Ah", "4h"}, {"Kh", "9h", "2c"}, 15, 3, 3, 6, 190)).action == "call");
}

TEST(bot_equity_beats_calling_station_and_random){
  match_options options;
  options.hands = 1000;
  match_result r = run_match({bot_style::EQUITY, bot_style::CALLING}, options);
  CHECK(bb_per_100(r.seats[0], r) - bb_per_100_margin(r.seats[0], r) > 0);
  r = run_match({bot_style::EQUITY, bot_style::RANDOM}, options);
  CHECK(bb_per_100(r.seats[0], r) - bb_per_100_margin(r.seats[0], r) > 0);
}
