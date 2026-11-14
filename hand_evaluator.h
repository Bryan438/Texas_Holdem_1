#ifndef _HAND_EVALUATOR_H_
#define _HAND_EVALUATOR_H_

#include "card.h"

enum hand_category{
  HIGH_CARD = 1,
  ONE_PAIR = 2,
  TWO_PAIR = 3,
  THREE_OF_A_KIND = 4,
  STRAIGHT = 5,
  FLUSH = 6,
  FULL_HOUSE = 7,
  FOUR_OF_A_KIND = 8,
  STRAIGHT_FLUSH = 9
};

// A hand score is a single int: a higher score beats a lower one, equal
// scores split the pot. Layout: category << 20, then up to five tie-break
// ranks (2..14) packed 4 bits each, most significant first.

// Score exactly five cards.
int evaluate_five(const Card* const cards[5]);

// Score the best five-card hand out of `count` cards (5 to 7).
// Returns 0 if fewer than five cards are given.
int evaluate_best(const Card* const cards[], int count);

// Same score as evaluate_best, much faster: counts ranks and suits once instead
// of trying all 21 five-card combinations. Used by the bots' simulations, which
// score millions of hands; the tests check it agrees with evaluate_best.
int evaluate_fast(const Card* const cards[], int count);

int hand_category_of(int score);
const char* hand_category_name(int category);

#endif
