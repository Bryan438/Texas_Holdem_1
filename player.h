#ifndef _PLAYER_H_
#define _PLAYER_H_
#include <string>
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
    std::string player_name;
    bool player_status;   // false once folded (or sitting out this hand)
    bool allin_status;

    int current_round_bet; // chips put in during the current betting round
    int total_bet;         // chips put in during the whole hand

    int raise_status;
    int remaining_money;
    int grade;             // hand score from hand_evaluator, -1 if none

    Card* card1;
    Card* card2;

    int put_in(int amount);
  public:
    player(int client_socket, int money);

    int get_client_socket();

    int my_strlen(char* message);
    void send();

    void set_name(const char* name);
    const char* get_name();

    void set_inital_bet(int bet);

    void set_initial_card(Card* card1, Card* card2);
    void set_public_card(Card* card1, Card* card2, Card* card3);

    void set_grade(int grade);
    int get_grade();

    Card* get_first_card();
    Card* get_second_card();

    void show_card();

    int get_raise_status();
    void reset_raise_status();
    void set_raise_status();

    int post_blind(int amount);
    void add_money(int change);
    int get_money();
    int get_current_round_bet();
    int get_total_bet();

    bool get_player_status();

    void set_allin_status();
    bool get_allin_status();

    int call(int current);
    int raise(int raise_amount);
    void fold();

    void reset_round();
    void reset_game();
};
#endif
