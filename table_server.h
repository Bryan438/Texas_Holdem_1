
class table_server{
  private:
    deck_of_card* dcards;
    player* player_list[12];
    Card* card_list[7];
    Card* result_list[5];
    
    int number_of_player;
    int active_player;

    bool end_status = false;

    int dealer_button;
    int small_blind;
    int big_blind;
    int total_pot;
    int current_bet;
    int card_index;
    int current_round;
    int current_game;
    void round(int card_count);
  public:
    table_server();
    
    //void handle_message(message_content* msg);
    void preparation();
    void add_player(player* p);
    int get_player_number();
    int get_active_player();

    void set_inital_card();
    int get_current_bet();
    void add_total_pot(int amount);

    int get_available_decision(int current_player);
    int check_available_decision(int current_player, int raise_status);
    int check_valid_input(int combined, char input);
    char input();
    int input_action(int decision, int player_num, int raise_status);

    void preflop();
    void flop();
    void turn();
    void river();

    void showdown();
    void sort(int left, int right, Card* card_list[]);
    void reverse(Card* card_list[]);
    void swap(int left, int right, Card* card_list[]);
    int get_highest_grade();
    void all_fold_case();
    
    int calculate_grade(Card* card_list[]);
    int check_straight_flush(Card* card_list[]);
    int check_four_kind(Card* card_list[]);
    int check_full_house(Card* card_list[]);
    int check_flush(Card* card_list[]);
    int check_straight(Card* card_list[]);
    int check_three_kind(Card* card_list[]);
    int check_two_pairs(Card* card_list[]);
    int check_one_pair(Card* card_list[]);

    void print_result_list();
    void reset();
};

