#ifndef _CARD_H_
#define _CARD_H_
#include <string>

enum card_suit{
  Diamond = 1,
  Spade = 2,
  Heart = 3,
  Club = 4
};

class Card{
 private:
    int number;
    card_suit suit;
 public:
    Card(card_suit suit, int number);
    Card(){};
    void show() const;
    void set_number(int number);
    card_suit get_suit() const;
    int get_number() const;
    // "Ad", "Th", "7c": rank (2-9, T, J, Q, K, A) + suit (s, h, d, c)
    std::string code() const;
    // "A♦", "10♥": for messages people read
    std::string text() const;
};

#endif
