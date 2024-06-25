class Card{
 private:
    const char* suit;
    int number;
 public:
    Card(const char* suit, int number);
    Card(){};
    void show();
    void set_suit(const char* suit);
    void set_number(int number);
    const char* get_suit();
    int get_number();
};

