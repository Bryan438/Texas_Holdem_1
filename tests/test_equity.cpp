// Monte Carlo equity: known odds, exact cases (ties, the nuts), opponent
// ranges, and the preflop table.
#include <vector>
#include "test_framework.h"
#include "equity.h"

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

static double equity(std::initializer_list<const char*> hole, std::initializer_list<const char*> board,
                     int opponents, opponent_range range = opponent_range::ANY){
  std::mt19937 rng(7);
  return estimate_equity(cards(hole), cards(board), opponents, 5000, rng, range);
}

TEST(equity_matches_known_preflop_odds){
  // Published all-in odds against one random hand: AA 85%, 72o 35%, AKs 67%.
  double aa = equity({"As", "Ah"}, {}, 1);
  CHECK(aa > 0.83 && aa < 0.87);
  double seven_two = equity({"7d", "2c"}, {}, 1);
  CHECK(seven_two > 0.32 && seven_two < 0.37);
  double ak = equity({"As", "Ks"}, {}, 1);
  CHECK(ak > 0.65 && ak < 0.69);
  // More opponents, less of the pot: AA wins about 56% against four.
  double aa_four = equity({"As", "Ah"}, {}, 4);
  CHECK(aa_four > 0.53 && aa_four < 0.59);
}

TEST(equity_exact_cases){
  // The board is a royal flush: everyone ties and splits the pot.
  CHECK(equity({"2c", "3d"}, {"As", "Ks", "Qs", "Js", "Ts"}, 1) == 0.5);
  double three_way = equity({"2c", "3d"}, {"As", "Ks", "Qs", "Js", "Ts"}, 2);
  CHECK(three_way > 0.3333 && three_way < 0.3334);
  // The nut flush on the river cannot lose.
  CHECK(equity({"Ah", "Kh"}, {"9h", "4h", "2h", "3c", "Td"}, 1) == 1.0);
  // Four kings and a queen on the board: we split unless they hold an ace for
  // a better kicker. Half of P(no ace in two of the 45 unseen cards) = 0.414.
  double kicker = equity({"2c", "3d"}, {"Ks", "Kd", "Kh", "Kc", "Qs"}, 1);
  CHECK(kicker > 0.39 && kicker < 0.44);
}

TEST(equity_ranges_on_the_board){
  std::vector<Card> board = cards({"Ks", "8c", "2d"});
  CHECK(in_range(card("Kh"), card("Qd"), board, opponent_range::STRONG));       // top pair
  CHECK(in_range(card("Ah"), card("Ad"), board, opponent_range::STRONG));       // overpair
  CHECK(in_range(card("8h"), card("2h"), board, opponent_range::STRONG));       // two pair
  CHECK(in_range(card("9h"), card("9d"), board, opponent_range::CONNECTED));    // a pair...
  CHECK(!in_range(card("9h"), card("9d"), board, opponent_range::STRONG));      // ...but not a strong one
  CHECK(!in_range(card("5h"), card("4h"), board, opponent_range::CONNECTED));   // nothing
  CHECK(in_range(card("5h"), card("4h"), board, opponent_range::ANY));
  std::vector<Card> two_spades = cards({"Ks", "8s", "2d"});
  CHECK(in_range(card("As"), card("4s"), two_spades, opponent_range::STRONG));  // flush draw
  // A pair on the board does not count for anyone.
  CHECK(!in_range(card("Ah"), card("4c"), cards({"Ks", "Kd", "2d"}), opponent_range::CONNECTED));
}

TEST(equity_drops_when_opponents_have_something){
  // Middle pair: fine against any two cards, poor against hands that bet big.
  double any = equity({"8s", "7d"}, {"Ks", "8c", "2d"}, 1, opponent_range::ANY);
  double connected = equity({"8s", "7d"}, {"Ks", "8c", "2d"}, 1, opponent_range::CONNECTED);
  double strong = equity({"8s", "7d"}, {"Ks", "8c", "2d"}, 1, opponent_range::STRONG);
  CHECK(any > 0.6);
  CHECK(connected < any);
  CHECK(strong < connected);
  CHECK(strong < 0.3);
}

TEST(equity_preflop_table){
  // Same kind of hand, same answer, every time.
  CHECK(preflop_equity(card("As"), card("Ks"), 1) == preflop_equity(card("Ah"), card("Kh"), 1));
  CHECK(preflop_equity(card("Kd"), card("Ad"), 2) == preflop_equity(card("Ac"), card("Kc"), 2));
  CHECK(preflop_equity(card("As"), card("Ks"), 1) > preflop_equity(card("As"), card("Kd"), 1));   // suited is worth a bit
  double aa = preflop_equity(card("As"), card("Ah"), 1);
  CHECK(aa > 0.83 && aa < 0.87);
  CHECK(preflop_equity(card("As"), card("Ah"), 3) < aa);
}
