enum state{
  WAITING_NOTIFY = 1,
  IN_ROUND = 2
};

#ifndef _TABLE_H_
#define _TABLE_H_

#include "observer.h"
#include "transport.h"
#include "player.h"
#include "card.h"
#include "deck_of_card.h"

class table: public observer{
  private:
    deck_of_card* dcards = new deck_of_card();
    player* player_list[12];
    Card* card_list[7];
    Card* result_list[5];

    int number_of_player = 0;
    int active_player;
    state current_state = IN_ROUND;

    int card_index;

    int big_blind = 0;
    int small_blind = 0;

    int current_player = 0;
    int current_game = 0;
    int current_bet;
    int current_round;
    int total_pot;

    int dealer_pos;
    int current_pos;
    int end_pos = 0;

    int inform_pos = 0;
    int inform_end_pos = 0;
  public:
    table(){};
    void preparation();
    void set_initial_card();

    virtual void handle_message(message_content* msg, int socket_id);

    void create_player(int client_socket);
    player* search_player_by_name(char* name);
    player* search_player_by_socket(int socket_id);
    int my_strcmp(const char* str1, const char* str2);

    int get_active_player();
    int get_current_bet();

    void add_total_pot(int amount);
    void set_current_player(int player_num);

    void get_available_decision(int current_player);
    void get_next_player_decision();
    
    int check_valid_input(int combined, char input);
    void input_action(int decision, int player_num, int raise_status);
    void inform_action(int player_num);
    void check_all_fold();

    void preflop();
    void flop();
    void round();

    void showdown(); 
    void sort(int left, int right, Card* card_list[]);
    void reverse(Card* card_list[]);
    void swap(int left, int right, Card* card_list[]);

    int get_highest_grade();
    int calculate_grade(Card* card_list[]);
    int check_straight_flush(Card* card_list[]);
    int check_four_kind(Card* card_list[]);
    int check_full_house(Card* card_list[]);
    int check_flush(Card* card_list[]);
    int check_straight(Card* card_list[]);
    int check_three_kind(Card* card_list[]);
    int check_two_pairs(Card* card_list[]);
    int check_one_pair(Card* card_list[]);

    void reset_round();
};
#endif
