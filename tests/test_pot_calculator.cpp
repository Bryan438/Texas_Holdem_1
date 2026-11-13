#include <numeric>
#include <vector>
#include "test_framework.h"
#include "pot_calculator.h"

static int sum(const std::vector<int>& v){
  return std::accumulate(v.begin(), v.end(), 0);
}

TEST(pot_single_winner_takes_all){
  std::vector<int> pay = distribute_pot({10, 10, 10}, {true, true, true}, {5, 9, 3}, 0);
  CHECK_EQ(pay[0], 0);
  CHECK_EQ(pay[1], 30);
  CHECK_EQ(pay[2], 0);
}

TEST(pot_folded_players_money_goes_to_winner){
  std::vector<int> pay = distribute_pot({1, 2, 6}, {false, false, true}, {0, 0, 0}, 1);
  CHECK_EQ(pay[2], 9);
  CHECK_EQ(sum(pay), 9);
}

TEST(pot_split_with_odd_chip_goes_left_of_dealer){
  // 3 chips, two winners: seats 0 and 2. first_seat = 2, so seat 2 gets the odd chip.
  std::vector<int> pay = distribute_pot({1, 1, 1}, {true, false, true}, {7, 0, 7}, 2);
  CHECK_EQ(pay[2], 2);
  CHECK_EQ(pay[0], 1);
}

TEST(pot_short_allin_only_wins_main_pot){
  // Seat 0 all-in for 20 with the best hand; seats 1 and 2 put in 50 each.
  // Main pot 60 -> seat 0. Side pot 60 -> best of seats 1, 2 (seat 2).
  std::vector<int> pay = distribute_pot({20, 50, 50}, {true, true, true}, {9, 3, 5}, 0);
  CHECK_EQ(pay[0], 60);
  CHECK_EQ(pay[1], 0);
  CHECK_EQ(pay[2], 60);
}

TEST(pot_multiple_side_pots){
  // Stacks 10 / 30 / 60 / 60 all-in, hand strength 4 > 3 > 2 > 1.
  // Main 40 -> seat 0, side1 (20*3=60) -> seat 1, side2 (30*2=60) -> seat 2.
  std::vector<int> pay = distribute_pot({10, 30, 60, 60}, {true, true, true, true}, {4, 3, 2, 1}, 0);
  CHECK_EQ(pay[0], 40);
  CHECK_EQ(pay[1], 60);
  CHECK_EQ(pay[2], 60);
  CHECK_EQ(pay[3], 0);
}

TEST(pot_uncalled_bet_returns_to_bettor){
  // Seat 1 bet 100 but seat 0 could only call 40. Seat 0 wins the 80 main pot,
  // the uncalled 60 goes back to seat 1.
  std::vector<int> pay = distribute_pot({40, 100}, {true, true}, {8, 2}, 0);
  CHECK_EQ(pay[0], 80);
  CHECK_EQ(pay[1], 60);
}

TEST(pot_split_side_pot_between_tied_players){
  std::vector<int> pay = distribute_pot({10, 40, 40}, {true, true, true}, {1, 6, 6}, 0);
  CHECK_EQ(pay[0], 0);
  CHECK_EQ(pay[1], 45);
  CHECK_EQ(pay[2], 45);
}

TEST(pot_folded_player_put_in_more_than_allin){
  // Seat 0 all-in 10, seat 1 bet 50 and folded later, seat 2 called 50.
  std::vector<int> pay = distribute_pot({10, 50, 50}, {true, false, true}, {9, 0, 1}, 0);
  CHECK_EQ(pay[0], 30);
  CHECK_EQ(pay[2], 80);
  CHECK_EQ(sum(pay), 110);
}

TEST(pot_chips_are_always_conserved){
  // Small brute force over many contribution / fold / score patterns.
  int checked = 0;
  for(int a = 0; a <= 30; a += 5)
  for(int b = 1; b <= 30; b += 7)
  for(int c = 0; c <= 30; c += 6)
  for(int mask = 1; mask < 8; mask++)
  for(int s = 0; s < 4; s++){
    std::vector<int> contrib = {a, b, c};
    std::vector<bool> in = {(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};
    std::vector<int> scores = {s, (s + 1) % 3, 2};
    std::vector<int> pay = distribute_pot(contrib, in, scores, s % 3);
    CHECK_EQ(sum(pay), sum(contrib));
    for(int i = 0; i < 3; i++){
      if(!in[i]){
        CHECK_EQ(pay[i], 0);
      }
    }
    checked++;
  }
  CHECK(checked > 0);
}
