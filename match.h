#ifndef _MATCH_H_
#define _MATCH_H_

#include <string>
#include <vector>
#include "bot.h"

// Bots playing each other on a real table, in one process with no network:
// each bot gets the same state messages a networked player would get.
struct match_options{
  int hands = 1000;
  int chips = 200;        // every bot starts every hand with this many chips
  unsigned seed = 1;      // same seed, same cards and same bot choices
  std::string history_path;   // also save the hands here ("" = don't)
};

struct match_seat{
  std::string name;
  bot_style style;
  long long net = 0;          // chips won over all hands
  double sum_squares = 0;     // of each hand's net, for the spread
};

struct match_result{
  std::vector<match_seat> seats;
  int hands = 0;
  int big_blind = 2;
  int illegal_moves = 0;      // moves the table rejected (should be 0)
  std::vector<json> records;  // every hand, as in the history file
};

match_result run_match(const std::vector<bot_style>& styles, const match_options& options);

// Big blinds won per 100 hands, and the half-width of its 95% confidence interval.
double bb_per_100(const match_seat& seat, const match_result& result);
double bb_per_100_margin(const match_seat& seat, const match_result& result);

#endif
