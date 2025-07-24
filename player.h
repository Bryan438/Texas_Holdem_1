#ifndef _PLAYER_H_
#define _PLAYER_H_
#include "observer.h"
#include "card.h"

enum player_decision{
  CALL = 0x01, 
  RAISE = 0x02, 
  FOLD = 0x04,
  CHECK = 0x08
};

class player{
  private:
    int client_socket;
    char* player_name;
    bool player_status;
    bool allin_status;
    bool need_sidepot;
    int player_num;

    int current_round_bet;
    int total_bet;

    int raise_status;
    int remaining_money;
    int grade;
    int winning_sidepot_amount;
    
    Card* card1;
    Card* card2;
    Card* result_list[5];
  public:
    player(int client_socket, int money);

    int get_client_socket();

    int my_strlen(char* message);
    void send();

    void set_name(char* name);
    char* get_name();

    void set_inital_bet(int bet);
    void set_bet(int bet);

    void set_initial_card(Card* card1, Card* card2);
    void set_result_card(Card* card_list[]);
    void set_public_card(Card* card1, Card* card2, Card* card3);
    Card** get_result_card();
    
    void set_grade(int grade);
    int get_grade();

    Card* get_first_card();
    Card* get_second_card();

    void show_card();

    int get_raise_status();
    void reset_raise_status();
    void set_raise_status();

    void set_money(int change);
    void add_money(int change);
    int get_money();
    int get_current_round_bet();
    int get_total_bet();

    bool get_player_status();

    void set_allin_status();
    bool get_allin_status();
    void set_needsidepot_status();
    bool get_needsidepot_status();
    void set_sidepot(int money);
    int get_sidepot();

    int call(int current);
    int raise(int raise_amount);
    void fold();
    
    void reset_round();
    void reset_game();
};
#endif
