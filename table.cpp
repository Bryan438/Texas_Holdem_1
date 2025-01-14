#include <string.h>
#include <stdlib.h>
#include <cstdio>
#include <stdio.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "transport.h"
#include "message_content.h"
#include "table.h" 
#include "card.h"

void table::handle_message(message_content* message, int socket_id){
  switch(message->get_command()){
    case 3: 
      create_player(message->get_intmessage());
      break;
    case 4:
      {
        player* p = search_player_by_socket(socket_id);
        if(p != NULL){
          p->set_name(message->get_charmessage());
        }
      }
      break;
    case 5:
      {
        player* p = search_player_by_socket(socket_id);
        if(p != NULL){
          p->set_inital_bet(message->get_intmessage());
        }
      }
      break;
      
      //Set public card(s) and head on to the next stage
    case 1:
      {
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
          round();
        }
      }
      break;
    case 7: 
      {
        preparation();
        set_initial_card();
        preflop();
      }
      break;

      //Input for RAISE, CALL, CHECK, FOLD
    case 10:
      {
        char msg[6];
        memset(msg, 0, 6);
        memcpy(msg, message->get_charmessage(), 6);
        printf("msg = %s", msg);
        int valid_status = check_valid_input(msg[0], msg[1]);
        int h_raise_amount = 0;
        
        if(valid_status == RAISE){
          int raise_amount = 0;
          memcpy(&raise_amount, msg + 2, 4);
          h_raise_amount = ntohl(raise_amount);
        }

        input_action(valid_status, current_pos, h_raise_amount);
        if(valid_status == FOLD){
          active_player--;
        }

        if(player_list[current_pos]->get_raise_status() == 1){
          end_pos = current_pos;
          player_list[current_pos]->reset_raise_status();
        }

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
      break;

    case 11:
      {
        char msg[1];
        memcpy(msg, message->get_charmessage(), 1);
      }
      break;

    default:
      break;
  }
}

void table::set_current_player(int player_num){
  current_player = player_num;
}

void table::create_player(int client_socket){
  player* new_player = new player(client_socket, 50);
  player_list[number_of_player] = new_player;
  number_of_player++;
}

player* table::search_player_by_name(char* player_name){
  for(int i = 0; i < number_of_player; i++){
    if(my_strcmp(player_name, player_list[i]->get_name()) == 0){
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

int table::my_strcmp(const char* str1, const char* str2){
  if(str1 == NULL && str2 == NULL){
    return 0;
  }
  if(str1 == NULL){
    return -1;
  }
  else if(str2 == NULL){
    return 1;
  }
  
  int i = 0;
  while(true){
    if(str1[i] == 0 && str2[i] == 0){
      return 0;
    }
    else if(str1[i] == str2[i]){
      i++;
      continue;
    }
    return str1[i] > str2[i] ? 1 : -1;  
  }
}

void table::preparation(){

  current_round = 1;
  current_bet = 10;
  dcards->shuffle();
  dcards->show();
  card_index = 0;

  dealer_pos = current_game;
  small_blind = dealer_pos + 1;
  big_blind = dealer_pos + 2;

  player_list[small_blind]->set_money(current_bet/2);
  player_list[big_blind]->set_money(current_bet);
  
  transport* tp = transport::get_instance();
  tp->serialize(player_list[dealer_pos]->get_client_socket(), 2, 20, "You, dealer");
  tp->serialize(player_list[small_blind]->get_client_socket(), 2, 20, "You, small bind");
  tp->serialize(player_list[big_blind]->get_client_socket(), 2, 20, "You, big_blind");
}

void table::set_initial_card(){
  for(int i = 0; i < number_of_player; i++){
    printf("Assign cards to player %s\n", player_list[i]->get_name());
    player_list[i]->set_initial_card(dcards->get_card(2*i), dcards->get_card(2*i+1));
    dcards->get_card(2*i)->show();
    dcards->get_card(2*i+1)->show();
    card_index += 2;
  }
}

int table::get_current_bet(){
  return current_bet;
}

int table::get_active_player(){
  return active_player;
}

void table::add_total_pot(int amount){
  total_pot += amount;
}

void table::get_available_decision(int player_num){

  char msg[10];
  memset(msg, 0, 10);

  uint8_t combined = 0x00;
  if(player_list[player_num]->get_money() <= current_bet - player_list[player_num]->get_current_round_bet()){
    combined = FOLD | CALL;
  }
  else{
    if(current_round > 1){
      combined = FOLD | RAISE | CHECK;
    }
    else{
      combined = FOLD | RAISE | CALL;
    }
  }
  memcpy(msg, (char*)(&combined), 1);
  
  int highest_range = player_list[player_num]->get_money();
  int lowest_range = current_bet;
  int n_highest_range = htonl(highest_range);
  int n_lowest_range = htonl(lowest_range);
  memcpy(msg + 1, &n_highest_range, 4);
  memcpy(msg + 5, &n_lowest_range, 4);

  transport::get_instance()->serialize(player_list[player_num]->get_client_socket(), 8, 9, msg);
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
      check_all_fold();
      break;
    case CALL:
      money_added = player_list[player_num]->call(current_bet);
      break;
    case CHECK:
      money_added = player_list[player_num]->call(current_bet);
      break;
    default:
      money_added = player_list[player_num]->raise(raise_amount);
      current_bet = player_list[player_num]->get_current_round_bet();
      player_list[player_num]->set_raise_status();
      break;
  }
  total_pot += money_added;
  printf("money remaining = %d, money add = %d\n", player_list[player_num]->get_money(), money_added);
}

void table::preflop(){
  if(current_round != 1){
    printf("Invalid round\n");
    return;
  }
  current_pos = big_blind + 1;
  if(current_pos >= number_of_player){
    current_pos = 0;
  }
  end_pos = current_pos;

  get_available_decision(current_pos);
}

void table::round(){
  current_bet = 0;

  current_pos = dealer_pos + 1;
  if(current_pos >= number_of_player){
    current_pos = 0;
  }
  while(player_list[current_pos]->get_player_status() == false){
    current_pos += 1;
    if(current_pos >= number_of_player){
      current_pos = 0;
    }
  }
  end_pos = current_pos;
  get_available_decision(current_pos);
}

//Reset steps corresponding to each round
void table::reset_round(){
  current_bet = 0;
  for(int j = 0; j < number_of_player; j++){
    if(player_list[j]->get_player_status() == true){
      player_list[j]->reset_round();
    }
  }

  if(current_round == 1){
    for(int i = 0; i < 3; i++){
      dcards->get_card(card_index)->show();
      card_list[card_index - 2*number_of_player] = dcards->get_card(card_index);
      card_index++;
    }
    player_list[current_pos]->set_public_card(card_list[0], card_list[1], card_list[2]);
  }
  else if(current_round == 4){
    //TODO
    printf("Time for showdown!");
    showdown();
  }
  else{
      dcards->get_card(card_index)->show();
      card_list[current_round+1] = dcards->get_card(card_index);
      card_index++;
      player_list[current_pos]->set_public_card(card_list[current_round+1], NULL, NULL);
  }

  current_round++;
}

//Check after each fold if there are more than one player
//if no, then the only one left is the winner
void table::check_all_fold(){
  if(get_active_player() == 1){
    for(int i = 0; i < number_of_player; i++){
      if(player_list[i]->get_player_status() == true){
        player_list[i]->add_money(total_pot);
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////SHOWDOWN Below!!!///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void table::showdown(){
  Card* card_cpy[7];
  player* winner_list[12];
  player* player_candidate[12];
  int candidate_count = 0;
  int winner_count = 0;

  for(int i = 0; i < number_of_player; i++){
    if(player_list[i]->get_player_status() == true){
      for(int j = 0; j < 5; j++){
        card_cpy[j] = card_list[j];
      }
      card_cpy[5] = player_list[i]->get_first_card();
      card_cpy[6] = player_list[i]->get_second_card();
      sort(0, 6, card_cpy);
      reverse(card_cpy);
      //player_list[i]->set_grade(calculate_grade(card_cpy));
      //player_list[i]->set_result_card(result_list);
    }
  }
}

void table::sort(int item_from_left, int item_from_right, Card* card_list[]){
  if(item_from_right - item_from_left < 1){
    return;
  }
  int left_pos = item_from_left;
  int right_pos = item_from_right;
  int pivot = card_list[right_pos]->get_number();
  bool left_check = false;
  bool right_check = false;
  while(item_from_left <= item_from_right){
    if(pivot > card_list[item_from_right]->get_number()){
      right_check = true;
    }
    else{
      item_from_right--;
    }
    if(pivot < card_list[item_from_left]->get_number()){
      left_check = true;
    }
    else{
      item_from_left++;
    }
    if(left_check == true && right_check == true){
      swap(item_from_left, item_from_right, card_list);
      left_check = false;
      right_check = false;
    }
  }
  swap(item_from_left, right_pos, card_list);
  sort(left_pos, item_from_left - 1, card_list);
  sort(item_from_left + 1, right_pos, card_list);
}

void table::swap(int index1, int index2, Card* card_list[]){
  Card* temp = card_list[index1];
  card_list[index1] = card_list[index2];
  card_list[index2] = temp;
}

void table::reverse(Card* card_list[]){
  Card* temp_list[7];
  int reverse_count = 6;
  for(int i = 0; i < 7; i++){
    temp_list[i] = card_list[reverse_count];
    reverse_count--;
  }
  for(int j = 0; j < 7; j++){
    card_list[j] = temp_list[j];
  }
}

