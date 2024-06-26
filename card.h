enum card_suit{
  Diamond,
  Spade,
  Heart,
  Club
};

class Card{
 private:
    int number;
    card_suit suit;
 public:
    Card(card_suit suit, int number);
    Card(){};
    void show();
    void set_suit(card_suit suit);
    void set_number(int number);
    card_suit get_suit();
    int get_number();
};

