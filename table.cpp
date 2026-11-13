#include <string.h>
#include <stdlib.h>
#include <cstdio>
#include <string>
#include <vector>
#include <iostream>
#include <arpa/inet.h>
#include "transport.h"
#include "message_content.h"
#include "table.h"
#include "card.h"
#include "hand_evaluator.h"
#include "pot_calculator.h"

table::~table(){
  for(int i = 0; i < number_of_player; i++){
    delete player_list[i];
  }
  delete dcards;
}

void table::handle_message(message_content* message, int socket_id){
  switch(message->get_command()){
    // New connection (sent by transport, not by a client)
    case 3:
      create_player(socket_id);
      break;

    // Player name
    case 4:
      {
        player* p = search_player_by_socket(socket_id);
        if(p != NULL && message->get_charmessage() != NULL){
          p->set_name(message->get_charmessage());
        }
      }
      break;

    // Starting chips, only between hands
    case 5:
      {
        player* p = search_player_by_socket(socket_id);
        if(p != NULL && current_state == HAND_OVER && message->get_intmessage() > 0){
          p->set_inital_bet(message->get_intmessage());
        }
      }
      break;

    // Ack for public card(s): hand them to the next player, or start the next betting round
    case 1:
      {
        if(current_state != IN_ROUND || !dealing){
          break;
        }
        if(player_list[current_pos]->get_client_socket() != socket_id){
          printf("Unexpected card ack from socket %d\n", socket_id);
          break;
        }
        current_pos++;
        if(current_pos >= number_of_player){
          current_pos = 0;
        }
        if(current_pos != end_pos){
          if(current_round == 2){
            player_list[current_pos]->set_public_card(card_list[0], card_list[1], card_list[2]);
          }
          else{
            player_list[current_pos]->set_public_card(card_list[current_round], NULL, NULL);
          }
        }
        else{
          dealing = false;
          round();
        }
      }
      break;

    // Start a new hand
    case 7:
      {
        if(current_state != HAND_OVER){
          printf("A hand is already running\n");
          break;
        }
        if(!preparation()){
          break;
        }
        set_initial_card();
        preflop();
      }
      break;

    //Input for RAISE, CALL, CHECK, FOLD
    case 10:
      handle_action(message, socket_id);
      break;

    // Ack that a player saw the new bet
    case 13:
      {
        if(current_state != WAITING_NOTIFY){
          break;
        }
        if(player_list[inform_pos]->get_client_socket() != socket_id){
          printf("Unexpected bet ack from socket %d\n", socket_id);
          break;
        }
        inform_pos++;
        if(inform_pos >= number_of_player){
          inform_pos = 0;
        }

        if(inform_pos != inform_end_pos){
          // notify next player current bet has changed
          inform_action(inform_pos);
        }
        else{
          // all players have been notified, continue to play round
          current_state = IN_ROUND;
          get_next_player_decision();
        }
      }
      break;

    default:
      break;
  }
}

void table::handle_action(message_content* message, int socket_id){
  if(current_state != IN_ROUND || dealing){
    printf("Not waiting for an action\n");
    return;
  }
  if(player_list[current_pos]->get_client_socket() != socket_id){
    printf("Socket %d acted out of turn\n", socket_id);
    return;
  }
  if(message->get_length() < 2){
    get_available_decision(current_pos);
    return;
  }

  char msg[6];
  memset(msg, 0, 6);
  memcpy(msg, message->get_charmessage(), message->get_length() < 6 ? message->get_length() : 6);

  // Check against what the server allows, not the bitmask the client echoes back.
  int decision = check_valid_input(available_decisions(current_pos), msg[1]);
  int raise_to = 0;

  if(decision == RAISE){
    uint32_t n_raise = 0;
    memcpy(&n_raise, msg + 2, 4);
    raise_to = (int)ntohl(n_raise);
    player* p = player_list[current_pos];
    int max_total = p->get_money() + p->get_current_round_bet();
    if(message->get_length() < 6 || raise_to <= current_bet || raise_to > max_total){
      decision = -1;
    }
  }

  if(decision == -1){
    printf("Invalid action, asking again\n");
    get_available_decision(current_pos);
    return;
  }

  input_action(decision, current_pos, raise_to);

  if(count_in_hand() <= 1){
    // Everyone else folded
    finish_hand();
    return;
  }

  if(decision == RAISE){
    // current bet changed, notify all players, then everyone else acts again
    current_state = WAITING_NOTIFY;
    end_pos = current_pos;
    player_list[current_pos]->reset_raise_status();

    inform_pos = current_pos;
    inform_end_pos = current_pos;
    inform_action(inform_pos);
  }
  else{
    get_next_player_decision();
  }
}

// Next seat clockwise after `from` that is still in the hand (and, if asked,
// not all-in). Returns -1 if there is none.
int table::next_seat_in_hand(int from, bool skip_allin){
  for(int k = 1; k <= number_of_player; k++){
    int seat = (from + k) % number_of_player;
    player* p = player_list[seat];
    if(p->get_player_status() && !(skip_allin && p->get_allin_status())){
      return seat;
    }
  }
  return -1;
}

int table::count_in_hand(){
  int remaining = 0;
  for(int i = 0; i < number_of_player; i++){
    if(player_list[i]->get_player_status()){
      remaining++;
    }
  }
  return remaining;
}

void table::get_next_player_decision(){
  do{
    current_pos++;
    if(current_pos >= number_of_player){
      current_pos = 0;
    }
    if(current_pos == end_pos){
      break;
    }
  } while(player_list[current_pos]->get_allin_status() == true || player_list[current_pos]->get_player_status() == false);

  if(current_pos != end_pos){
    get_available_decision(current_pos);
  }
  else{
    reset_round();
  }
}

void table::create_player(int client_socket){
  if(number_of_player >= MAX_PLAYERS){
    printf("Table is full, rejecting socket %d\n", client_socket);
    transport::get_instance()->close_client_socket(client_socket);
    return;
  }
  player* new_player = new player(client_socket, 50);
  if(current_state != HAND_OVER){
    // Joined in the middle of a hand: sit out until the next one.
    new_player->fold();
  }
  player_list[number_of_player] = new_player;
  number_of_player++;
}

player* table::search_player_by_name(const char* player_name){
  for(int i = 0; i < number_of_player; i++){
    if(strcmp(player_name, player_list[i]->get_name()) == 0){
      return player_list[i];
    }
  }
  printf("Wrong name\n");
  return NULL;
}

player* table::search_player_by_socket(int socket_id){
  for(int i = 0; i < number_of_player; i++){
    if(socket_id == player_list[i]->get_client_socket()){
      return player_list[i];
    }
  }
  printf("Wrong id\n");
  return NULL;
}

// Set up a new hand: reset players, move the button, post blinds.
// Returns false if fewer than two players have chips.
bool table::preparation(){
  int ready = 0;
  for(int i = 0; i < number_of_player; i++){
    player_list[i]->reset_game();
    if(player_list[i]->get_money() <= 0){
      player_list[i]->fold(); // busted, sits out
    }
    else{
      ready++;
    }
  }
  if(ready < 2){
    printf("Need at least 2 players with chips to start\n");
    return false;
  }

  for(int i = 0; i < 5; i++){
    card_list[i] = NULL;
  }

  if(current_game == 0){
    dealer_pos = next_seat_in_hand(number_of_player - 1, false);
  }
  else{
    dealer_pos = next_seat_in_hand(dealer_pos, false);
  }
  current_game++;

  // Heads-up: the dealer posts the small blind.
  small_blind = ready == 2 ? dealer_pos : next_seat_in_hand(dealer_pos, false);
  big_blind = next_seat_in_hand(small_blind, false);

  current_round = 1;
  card_index = 0;
  total_pot = 0;
  current_bet = BIG_BLIND;
  dealing = false;
  dcards->shuffle();

  total_pot += player_list[small_blind]->post_blind(BIG_BLIND / 2);
  total_pot += player_list[big_blind]->post_blind(BIG_BLIND);

  transport* tp = transport::get_instance();
  const char* dealer_msg = "You, dealer";
  const char* small_msg = "You, small blind";
  const char* big_msg = "You, big blind";
  tp->serialize(player_list[dealer_pos]->get_client_socket(), 2, strlen(dealer_msg), dealer_msg);
  tp->serialize(player_list[small_blind]->get_client_socket(), 2, strlen(small_msg), small_msg);
  tp->serialize(player_list[big_blind]->get_client_socket(), 2, strlen(big_msg), big_msg);

  current_state = IN_ROUND;
  return true;
}

void table::set_initial_card(){
  for(int i = 0; i < number_of_player; i++){
    if(!player_list[i]->get_player_status()){
      continue;
    }
    player_list[i]->set_initial_card(dcards->get_card(card_index), dcards->get_card(card_index + 1));
    card_index += 2;
  }
}

int table::get_current_bet(){
  return current_bet;
}

int table::available_decisions(int player_num){
  player* p = player_list[player_num];
  int to_call = current_bet - p->get_current_round_bet();
  if(p->get_money() <= to_call){
    // Can only call all-in or fold
    return FOLD | CALL;
  }
  return FOLD | RAISE | (to_call == 0 ? CHECK : CALL);
}

void table::get_available_decision(int player_num){

  char msg[10];
  memset(msg, 0, 10);

  uint8_t combined = available_decisions(player_num);
  memcpy(msg, (char*)(&combined), 1);

  // Raise range is the new total bet for this round: above the current bet,
  // up to everything this player has.
  player* p = player_list[player_num];
  int highest_range = p->get_money() + p->get_current_round_bet();
  int lowest_range = current_bet;
  int n_highest_range = htonl(highest_range);
  int n_lowest_range = htonl(lowest_range);
  memcpy(msg + 1, &n_highest_range, 4);
  memcpy(msg + 5, &n_lowest_range, 4);

  transport::get_instance()->serialize(p->get_client_socket(), 8, 9, msg);
}

int table::check_valid_input(int combined, char input){
  int decision = 0;
  switch(input){
    case 'F':
    case 'f':
      decision = FOLD;
      break;
    case 'R':
    case 'r':
      decision = RAISE;
      break;
    case 'c':
    case 'C':
      decision = CALL;
      break;
    case 'H':
    case 'h':
      decision = CHECK;
      break;
    default:
      break;
  }
  return decision & combined ? decision : -1;
}

void table::input_action(int decision, int player_num, int raise_amount){
  int money_added = 0;
  switch(decision){
    case FOLD:
      player_list[player_num]->fold();
      break;
    case CALL:
    case CHECK:
      money_added = player_list[player_num]->call(current_bet);
      break;
    case RAISE:
      money_added = player_list[player_num]->raise(raise_amount);
      current_bet = player_list[player_num]->get_current_round_bet();
      player_list[player_num]->set_raise_status();
      break;
    default:
      return;
  }
  total_pot += money_added;
  printf("money remaining = %d, money add = %d\n", player_list[player_num]->get_money(), money_added);
}

//Tell a player the current bet and pot after a raise
void table::inform_action(int player_num){
  char msg[8];
  memset(msg, 0, 8);
  int h_cur_bet = htonl(current_bet);
  int h_total_pot = htonl(total_pot);
  memcpy(msg, &h_cur_bet, 4);
  memcpy(msg + 4, &h_total_pot, 4);
  transport::get_instance()->serialize(player_list[player_num]->get_client_socket(), 12, 8, msg);
}

void table::preflop(){
  if(current_round != 1){
    printf("Invalid round\n");
    return;
  }
  current_pos = next_seat_in_hand(big_blind, true);
  if(current_pos == -1){
    // Everyone is all-in from the blinds: just deal the board.
    current_pos = big_blind;
    reset_round();
    return;
  }
  end_pos = current_pos;
  get_available_decision(current_pos);
}

// Start a betting round after public cards were handed out.
void table::round(){
  int can_act = 0;
  for(int i = 0; i < number_of_player; i++){
    if(player_list[i]->get_player_status() && !player_list[i]->get_allin_status()){
      can_act++;
    }
  }
  if(can_act <= 1){
    // Nobody left to bet against: deal the next card straight away.
    reset_round();
    return;
  }

  current_bet = 0;
  current_pos = next_seat_in_hand(dealer_pos, true);
  end_pos = current_pos;
  get_available_decision(current_pos);
}

// A betting round is over: deal the next public card(s), or go to showdown.
void table::reset_round(){
  current_bet = 0;
  for(int j = 0; j < number_of_player; j++){
    player_list[j]->reset_round();
  }

  if(current_round >= 4){
    finish_hand();
    return;
  }

  // Cards are handed to players one by one, each acking with command 1,
  // starting and ending at the current seat.
  end_pos = current_pos;
  dealing = true;
  if(current_round == 1){
    for(int i = 0; i < 3; i++){
      card_list[i] = dcards->get_card(card_index);
      card_index++;
    }
    player_list[current_pos]->set_public_card(card_list[0], card_list[1], card_list[2]);
  }
  else{
    card_list[current_round + 1] = dcards->get_card(card_index);
    card_index++;
    player_list[current_pos]->set_public_card(card_list[current_round + 1], NULL, NULL);
  }

  current_round++;
}

// Score hands (if there is a showdown), pay out main and side pots, tell everyone.
void table::finish_hand(){
  int n = number_of_player;
  std::vector<int> contributions(n, 0);
  std::vector<bool> in_hand(n, false);
  std::vector<int> scores(n, 0);

  bool showdown = count_in_hand() > 1 && card_list[4] != NULL;
  for(int i = 0; i < n; i++){
    player* p = player_list[i];
    contributions[i] = p->get_total_bet();
    in_hand[i] = p->get_player_status();
    if(in_hand[i] && showdown){
      const Card* seven[7] = {card_list[0], card_list[1], card_list[2], card_list[3], card_list[4],
                              p->get_first_card(), p->get_second_card()};
      scores[i] = evaluate_best(seven, 7);
      p->set_grade(scores[i]);
    }
  }

  std::vector<int> payout = distribute_pot(contributions, in_hand, scores, (dealer_pos + 1) % n);

  std::string result = "Hand over.";
  for(int i = 0; i < n; i++){
    if(payout[i] <= 0){
      continue;
    }
    player_list[i]->add_money(payout[i]);
    result += " ";
    result += player_list[i]->get_name();
    result += " wins " + std::to_string(payout[i]);
    if(showdown){
      result += " (";
      result += hand_category_name(hand_category_of(scores[i]));
      result += ")";
    }
    result += ";";
  }
  printf("%s\n", result.c_str());

  total_pot = 0;
  dealing = false;
  current_state = HAND_OVER;

  if(result.size() > MAX_PAYLOAD){
    result.resize(MAX_PAYLOAD);
  }
  broadcast(28, result.c_str(), result.size());
}

void table::broadcast(int command, const char* msg, int length){
  for(int i = 0; i < number_of_player; i++){
    transport::get_instance()->serialize(player_list[i]->get_client_socket(), command, length, msg);
  }
}
