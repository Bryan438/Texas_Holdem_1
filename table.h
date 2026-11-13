#ifndef _TABLE_H_
#define _TABLE_H_

#include "observer.h"
#include "transport.h"
#include "player.h"
#include "card.h"
#include "deck_of_card.h"

enum state{
  WAITING_NOTIFY = 1, // a raise is being announced to every player one by one
  IN_ROUND = 2,       // waiting for a player's action or a public-card ack
  HAND_OVER = 3       // no hand running; command 7 starts the next one
};

#define MAX_PLAYERS 12
#define BIG_BLIND 2

class table: public observer{
  private:
    deck_of_card* dcards = new deck_of_card();
    player* player_list[MAX_PLAYERS];
    Card* card_list[5] = {NULL, NULL, NULL, NULL, NULL}; // community cards

    int number_of_player = 0;
    state current_state = HAND_OVER;

    int card_index = 0;

    int big_blind = 0;
    int small_blind = 0;

    int current_game = 0;
    int current_bet = 0;
    int current_round = 0;
    int total_pot = 0;

    int dealer_pos = 0;
    int current_pos = 0;
    int end_pos = 0;

    int inform_pos = 0;
    int inform_end_pos = 0;

    bool dealing = false; // public cards are being handed out, waiting for acks (command 1)

    void handle_action(message_content* msg, int socket_id);
    int available_decisions(int player_num);
  public:
    table(){};
    ~table();
    bool preparation();
    void set_initial_card();

    virtual void handle_message(message_content* msg, int socket_id);

    void create_player(int client_socket);
    player* search_player_by_name(const char* name);
    player* search_player_by_socket(int socket_id);

    int get_current_bet();
    int get_total_pot(){return total_pot;};
    int get_player_count(){return number_of_player;};
    player* get_player(int seat){return player_list[seat];};
    int get_current_pos(){return current_pos;};
    int get_dealer_pos(){return dealer_pos;};
    state get_state(){return current_state;};
    bool is_dealing(){return dealing;};
    int get_inform_pos(){return inform_pos;};

    int next_seat_in_hand(int from, bool skip_allin);
    int count_in_hand();

    void get_available_decision(int current_player);
    void get_next_player_decision();

    int check_valid_input(int combined, char input);
    void input_action(int decision, int player_num, int raise_status);
    void inform_action(int player_num);

    void preflop();
    void round();
    void reset_round();

    void finish_hand();
    void broadcast(int command, const char* msg, int length);
};
#endif
