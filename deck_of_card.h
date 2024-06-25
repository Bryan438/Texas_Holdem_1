class deck_of_card{
  private:
    Card* cards[52]; 
  public:
    deck_of_card();
    ~deck_of_card();

    Card* get_card(int index);
    void shuffle();
    void show();
};
