class player{
  private:
    const char* player_name;
    bool player_status;
    bool allin_status;
    int player_num;

    int current_round_bet;
    int remaining_money;
    
    Card* card1;
    Card* card2;
    Card* result_card[5];
  public:
    player(const char* player_name, int player_num, int remaining_money);
    void set_initial_card(Card* card1, Card* card2);
    Card* get_first_card();
    Card* get_second_card();
    void show_card();

    const char* get_name();

    void set_money(int change);
    int get_money();
    int get_current_round_bet();

    bool get_player_status();
    bool get_allin_status();

    int call(int current);
    int raise();
    void fold();
    
    void reset_round();
};