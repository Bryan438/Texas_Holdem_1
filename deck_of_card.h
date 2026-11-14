#ifndef _DECK_OF_CARD_H_
#define _DECK_OF_CARD_H_
#include <random>
#include "card.h"

class deck_of_card{
  private:
    Card* cards[52];
    std::mt19937 rng;
  public:
    deck_of_card();
    ~deck_of_card();

    Card* get_card(int index);
    void shuffle();
    // Same seed, same sequence of decks: for reproducible bot matches.
    void seed(unsigned value);
    void show();
};

#endif
