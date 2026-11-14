#ifndef _EQUITY_H_
#define _EQUITY_H_

#include <random>
#include <vector>
#include "card.h"

// Monte Carlo equity: deal the unknown cards at random many times and count
// how often our hand wins. Ties count as a share of the pot (half for a
// two-way split).
//
//   hole        our two cards
//   board       0, 3, 4 or 5 cards already dealt
//   opponents   players still in the hand besides us (1 or more)
//   iterations  deals to try: the error is about 0.5 / sqrt(iterations),
//               so 1000 deals give the equity to within about 1.5%
//   range       which hands to deal opponents (see opponent_range); after a
//               bet, assuming random cards makes every call look too good
//
// Returns 0..1: our expected share of the pot if nobody folds from here on.
// What opponents may hold, judged on the board as it is now. Hands outside the
// range are dealt again. Only used once there is a flop.
enum class opponent_range{
  ANY,        // any two cards
  CONNECTED,  // a pair or better using a hole card, or a flush draw
  STRONG,     // top pair, an overpair, two pair using both hole cards or better, or a flush draw
};

double estimate_equity(const std::vector<Card>& hole, const std::vector<Card>& board,
                       int opponents, int iterations, std::mt19937& rng,
                       opponent_range range = opponent_range::ANY);

// Are these hole cards in the range on this board (3 to 5 cards)?
bool in_range(const Card& a, const Card& b, const std::vector<Card>& board, opponent_range range);

// Preflop equity of two hole cards against `opponents` random hands. Computed
// once per kind of hand (AKs, 72o, ...) and number of opponents, then reused,
// since preflop is where most decisions are made. Same answer on every call.
double preflop_equity(const Card& a, const Card& b, int opponents);

#endif
