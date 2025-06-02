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
        switch(current_state){
          case IN_ROUND:
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
                if(check_all_fold()){
                  break;
                }
              }

              if(valid_status == RAISE) {
                if(player_list[current_pos]->get_allin_status() == true){
                  allin_player_list[allin_player_num] = player_list[current_pos];

                  //Change the need sidepot status for all previous allin players to true
                  for(int i = 0; i < allin_player_num - 1; i++){
                    allin_player_list[i]->set_needsidepot_status();
                  }
                }
                // current bet changed, notify all players
                current_state = WAITING_NOTIFY;
                
                end_pos = current_pos;
                player_list[current_pos]->reset_raise_status();

                inform_pos = current_pos;
                inform_end_pos = current_pos;

                inform_action(inform_pos);
              } else {
                if(player_list[current_pos]->get_allin_status() == true){
                  allin_player_list[allin_player_num] = player_list[current_pos];
                  allin_player_num++;
                }
                /*if(check_all_allin()){
                  showdown();
                  break;
                }*/
                get_next_player_decision();
              }
            }
            break;

          default:
            // error case, unexpected message
            // print message info
            break;
        }
      }
      break;

    case 11:
      {
        char msg[1];
        memcpy(msg, message->get_charmessage(), 1);
      }
      break;

    case 13: 
      {
        switch(current_state){
          case WAITING_NOTIFY:
            {
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

                // take next player action
                get_next_player_decision();
              }
            }
            break;

          default:;
        }
      }
      break;
    default:
      break;
  }
}

void table::set_current_player(int player_num){
  current_player = player_num;
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
  current_bet = 2;
  dcards->shuffle();
  dcards->show();
  card_index = 0;

  dealer_pos = current_game;
  small_blind = dealer_pos + 1;
  big_blind = dealer_pos + 2;

  player_list[small_blind]->set_money(current_bet/2);
  player_list[big_blind]->set_money(current_bet);
  total_pot += current_bet + current_bet/2;

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
      active_player--;
      player_list[player_num]->fold();
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

//Tell every player in the table current raise bet
void table::inform_action(int player_num){
  char msg[10];
  memset(msg, 0, 10);
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
  current_pos = big_blind + 1;
  if(current_pos >= number_of_player){
    current_pos = 0;
  }
  end_pos = current_pos;

  get_available_decision(current_pos);
}

void table::round(){
  int remaining_player = number_of_player;
  for(int i = 0; i < number_of_player; i++){
    if(player_list[i]->get_player_status() || player_list[i]->get_allin_status()){
      remaining_player--;
    }
  }
  if(remaining_player <= 1){
    for(int i = 0; i < number_of_player; i++){
      if(player_list[i]->get_allin_status() == false){
        player_list[i]->set_allin_status();
      }
    }
    reset_round();
  }
  else{
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
}

//Reset steps corresponding to each round
void table::reset_round(){
  
  //Check the allin player list, check if there are allin player with the need of sidepot, if yes, then calculate the money
  int temp_total_pot = total_pot;
  for(int i = 0; i < allin_player_num; i++){
    if(allin_player_list[i]->get_needsidepot_status()){
      
      //Check all player with bet amount larger than sidepot amount, calculate the sidepot
      for(int j = 0; j < number_of_player; j++){
        if(player_list[j]->get_current_round_bet() > allin_player_list[i]->get_current_round_bet()){
          temp_total_pot -= (player_list[j]->get_current_round_bet() - allin_player_list[i]->get_current_round_bet());
        }
      }
      allin_player_list[i]->set_sidepot(temp_total_pot);
      temp_total_pot = total_pot;
    }
  }

  //Reset the round
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

//Check after each fold if there are more than one active player
//if no, then the only one left is the winner
bool table::check_all_fold(){
  int remaining_player = 0;
  for(int i = 0; i < number_of_player; i++){
    if(player_list[i]->get_player_status() == true || player_list[i]->get_allin_status() == true){
      remaining_player++;
    }
  }
  if(remaining_player > 1){
    return false;
  }
  else{
    return true;
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
      player_list[i]->set_grade(calculate_grade(card_cpy));
      player_list[i]->set_result_card(result_list);
    }
  }
  int highest_grade = get_highest_grade();

  for(int i = 0; i < number_of_player; i++){
    if(player_list[i]->get_grade() == highest_grade){
      player_candidate[candidate_count] = player_list[i];
      candidate_count++;
    }
  }

  if(candidate_count == 1){
    winner_list[0] = player_candidate[0];
    winner_count++;
  }
  else{
    Card** high_result_list = player_candidate[0]->get_result_card();
    int equal_count = 0;
    bool first_added = false;

    for(int i = 1; i < candidate_count; i++){
      Card** cur_result_list = player_candidate[i]->get_result_card();
      for(int j = 0; j < 5; j++){
        if(high_result_list[j]->get_number() < cur_result_list[j]->get_number()){

          winner_count = 0;
          winner_list[winner_count] = player_candidate[i];
          winner_count++;

          high_result_list = cur_result_list;
          first_added = true;
          break;
        }
        else if(high_result_list[j]->get_number() > cur_result_list[j]->get_number()){
          if(first_added == false){
            winner_list[winner_count] = player_candidate[0];
            winner_count++;
            first_added = true;
          }
          break;
        }
        else{
          equal_count++;
        }
      }
      if(equal_count == 5){
        if(first_added == false){
          winner_list[winner_count] = player_candidate[i-1];
          winner_count++;
          first_added = true;
        }
        winner_list[winner_count] = player_candidate[i];
        winner_count++;
      }
    }
  }
  /*
  int winning_money = total_pot / winner_count;
  for(int i = 0; i < winner_count; i++){
    if(winner_list[i]->get_allin_status()){
      int main_pot = 0;
      
    }
    else{
      winner_list[i]->add_money(winning_money);
    }
  }
  */
  //Rework on the final winning condition
  //Condition 1: Only 1 player win, if the player allin with the need of sidepot, then give them calculated money, else he/she wins the total pot
  if(winner_count == 1){
    if(winner_list[0]->get_needsidepot_status()){
      winner_list[0]->add_money(winner_list[0]->get_sidepot());
    }
    else{
      winner_list[0]->add_money(total_pot);
    }
  }
  //Condition 2: Multiply players win
  else{
    //Sort the winner list first, check from the smallest if the player need sidepot
    sort_for_player(0, winner_count, winner_list, 0);
    //Check the first player to see if we need to calculate sidepot
    if(winner_list[0]->get_needsidepot_status()){
      int cur_win_pos = 0;
      int sidepot = 0;
      while(winner_list[cur_win_pos]->get_needsidepot_status()){
        //Different between the first and remain players
        if(cur_win_pos == 0){
          sidepot = winner_list[cur_win_pos]->get_sidepot();
        }
        else{
          sidepot = winner_list[cur_win_pos]->get_sidepot() - sidepot;
        }
        //Divide evenly for sidepot
        for(int i = cur_win_pos; i < winner_count; i++){
          winner_list[i]->add_money(sidepot/(winner_count - cur_win_pos));
        }
        cur_win_pos++;
        total_pot = total_pot - sidepot;
      }
      //Even divide the rest of the money with the rest of the winners who did not
      int winning_money = total_pot / (winner_count - cur_win_pos);
      for(int j = cur_win_pos; j < winner_count; j++){
        winner_list[j]->add_money(winning_money);
        total_pot -= winning_money;
      }
    }
    //All players win with the same amount, then divide the winning money evenly.
    else{
      int winning_money = total_pot / winner_count;
      for(int i = 0; i < winner_count; i++){
        winner_list[i]->add_money(winning_money);
        total_pot -= winning_money;
      }
    }
  }

  //If there are remaining, add 1 to random player
  if(total_pot < winner_count){
    int i = 0;
    while(total_pot != 0){
      winner_list[i]->add_money(1);
      total_pot--;
      i++;
    }
  }
  //There is a case when the total pot is not 0 after all the situation, then the second largest will get the remaining money
  if(total_pot != 0){
    sort_for_player(0, number_of_player, player_list, 1);
    //Get the second largest, and the number of players who are second largest
    int second_largest_grade = 0;
    int second_pos = 0;
    int second_count = 0;
    int second_pos_cpy = 0;
    for(int i = 0; i < number_of_player; i++){
      if(player_list[i]->get_grade() != highest_grade){
        second_largest_grade = player_list[i]->get_grade();
        second_pos = i;
        second_pos_cpy = i;
        break;
      }
    }
    while(player_list[second_pos]->get_grade() == second_largest_grade){
      second_count++;
      second_pos++;
    }
    int remain_for_second = total_pot / second_count;
    for(int i = second_pos_cpy; i < second_pos; i++){
      winner_list[i]->add_money(remain_for_second);
      total_pot -= remain_for_second;
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

void table::sort_for_player(int item_from_left, int item_from_right, player* player_list[], int mode){
  if(item_from_right - item_from_left < 1){
    return;
  }
  int left_pos = item_from_left;
  int right_pos = item_from_right;
  int pivot = 0;
  if(mode == 0){
    pivot = player_list[right_pos]->get_total_bet();
  }
  else{
    pivot = player_list[right_pos]->get_grade();
  }
  bool left_check = false;
  bool right_check = false;
  while(item_from_left <= item_from_right){
    if(mode == 0){
      if(pivot > player_list[item_from_right]->get_total_bet()){
        right_check = true;
      }
      else{
        item_from_right--;
      }
      if(pivot < player_list[item_from_left]->get_total_bet()){
        left_check = true;
      }
      else{
        item_from_left++;
      }
      if(left_check == true && right_check == true){
        swap_for_player(item_from_left, item_from_right, player_list);
        left_check = false;
        right_check = false;
      }
    }
    else{
      if(pivot > player_list[item_from_right]->get_grade()){
        right_check = true;
      }
      else{
        item_from_right--;
      }
      if(pivot < player_list[item_from_left]->get_grade()){
        left_check = true;
      }
      else{
        item_from_left++;
      }
      if(left_check == true && right_check == true){
        swap_for_player(item_from_left, item_from_right, player_list);
        left_check = false;
        right_check = false;
      }
    }
  }
  swap_for_player(item_from_left, right_pos, player_list);
  sort_for_player(left_pos, item_from_left - 1, player_list, mode);
  sort_for_player(item_from_left + 1, right_pos, player_list, mode);
}

void table::swap_for_player(int index1, int index2, player* player_list[]){
  player* temp = player_list[index1];
  player_list[index1] = player_list[index2];
  player_list[index2] = temp;
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

int table::get_highest_grade(){
  int highest_grade = -1;
  for(int i = 0; i < number_of_player; i++){
    int current_grade = player_list[i]->get_grade();
    if(current_grade > highest_grade){
      highest_grade = current_grade;
    }
  }
  return highest_grade;
}

//Determine which combo this 5 cards will make
int table::calculate_grade(Card* card_list[]){
  int grade = 0;
  if(check_straight_flush(card_list) == 10){
    grade = 10;
  }
  else if(check_straight_flush(card_list) == 9){
    grade = 9;
  }
  else if(check_four_kind(card_list) == 8){
    grade = 8;
  }
  else if(check_full_house(card_list) == 7){
    grade = 7;
  }
  else if(check_flush(card_list) == 6){
    grade = 6;
  }
  else if(check_straight(card_list) == 5){
    grade = 5;
  }
  else if(check_three_kind(card_list) == 4){
    grade = 4;
  }
  else if(check_two_pairs(card_list) == 3){
    grade = 3;
  }
  else if(check_one_pair(card_list) == 2){
    grade = 2;
  }
  else{
    for(int i = 0; i < 5; i++){
      result_list[i] = card_list[i];
    }
    grade = 1;
  }
  return grade;
}


int table::check_straight_flush(Card* card_list[]){
  bool ace_status = false;
  int count = 1;

  for(int i = 0; i < 3; i++){
    card_suit suit = card_list[i]->get_suit();
    int number = card_list[i]->get_number();
    int index = i + 1;

    while(index < 7){
      if(number == 14){
        ace_status = true;
      }
      if(card_list[index]->get_suit() == suit && number - card_list[index]->get_number() == 1){
        result_list[count - 1] = card_list[index - 1];
        count++;
        if(count == 5){
          result_list[count - 1] = card_list[index];
          break;
        }
        number = card_list[index]->get_number();
      }
      index++;
    }

    if(count == 5){
      break;
    }

    count = 1;
    ace_status = false;
  }
  if(count < 5){
    memset(result_list, 0, 5*sizeof(Card*));
  }
  return count >= 5 ? (ace_status ? 10 : 9) : -1;
}

int table::check_four_kind(Card* card_list[]){
  int count = 0;
  int number = card_list[0]->get_number();
  bool single_mark = false;
  for(int i = 1; i < 7; i++){
    if(card_list[i]->get_number() == number){
      result_list[count] = card_list[i-1]; 
      count++;
      if(count == 3){
        result_list[count] = card_list[i]; 
        if(single_mark == false){
          result_list[4] = card_list[i+1]; 
          single_mark = true;
        }
        break;
      }
    }
    else{
      if(single_mark == false){
        result_list[4] = card_list[0];
        single_mark = true;
      }
      count = 0;
    }
    number = card_list[i]->get_number();
  }
  if(count == 3){
    return 8;
  }
  memset(result_list, 0, 5*sizeof(Card*));
  return -1;
}

int table::check_full_house(Card* card_list[]){
  int two_strike = 0;
  int three_strike = 0;
  int number = card_list[0]->get_number();
  int triple_num = 0;
  for(int i = 1; i < 7; i++){
    if(card_list[i]->get_number() == number){
      result_list[three_strike] = card_list[i - 1];
      three_strike++;
      if(three_strike == 2){
        result_list[three_strike] = card_list[i];
        triple_num = number;
        break;
      }
    }
    else{
      three_strike = 0;
    }
    number = card_list[i]->get_number();
  }
  if(three_strike >= 2){
    number = card_list[0]->get_number();
    for(int j = 1; j < 7; j++){
      if(triple_num != number){
        if(card_list[j]->get_number() == number){
          result_list[three_strike + two_strike + 1] = card_list[j - 1];
          two_strike++;
          if(two_strike == 1){
            result_list[three_strike + two_strike + 1] = card_list[j];
            break;
          }
        }
      }
      number = card_list[j]->get_number();
    }
    if(two_strike == 1){
      return 7;
    }
  }
  memset(result_list, 0, 5*sizeof(Card*));
  return -1;
}

int table::check_straight(Card* card_list[]){
  int count = 1;

  for(int i = 0; i < 3; i++){
    int number = card_list[i]->get_number();
    int index = i + 1;

    while(index < 7){
      if(number - card_list[index]->get_number() == 1){
        result_list[count - 1] = card_list[index - 1];
        count++;
        if(count == 5){
          result_list[count - 1] = card_list[index];
          break;
        }
        number = card_list[index]->get_number();
      }
      index++;
    }

    if(count == 5){
      break;
    }
    count = 1;
  }

  if(count < 5){
    memset(result_list, 0, 5*sizeof(Card*));
  }

  return count >= 5 ? 5 : -1;
}

int table::check_flush(Card* card_list[]){
  int count = 0;
  int spade_count = 0;
  int club_count = 0;
  int diamond_count = 0;
  int heart_count = 0;
  card_suit dest_card;
  bool found = false;

  for(int i = 0; i < 7; i++){
    switch(card_list[i]->get_suit()){
      case Spade:
        spade_count++;
        if(spade_count >= 5){
          dest_card = Spade;
          found = true;
        }
        break;
      case Club:
        club_count++;
        if(club_count >= 5){
          dest_card = Club;
          found = true;
        }
        break;
      case Diamond:
        diamond_count++;
        if(heart_count >= 5){
          dest_card = Heart;
          found = true;
        }
        break;
      case Heart:
        heart_count++;
        if(diamond_count >= 5){
          dest_card = Diamond;
          found = true;
        }
        break;
      default:
        break;
    }
    if(found){
      break;
    }
  }
  if(found){
    for(int i = 0; i < 7; i++){
      if(card_list[i]->get_suit() == dest_card){
        result_list[count] = card_list[i];
        count++;
        if(count == 5){
          return 6;
        }
      }
    }
  }
  memset(result_list, 0, 5*sizeof(Card*));
  return -1;
}

int table::check_three_kind(Card* card_list[]){
  int count = 0;
  int number = card_list[0]->get_number();
  int two_left_count = 3;
  for(int i = 1; i < 7; i++){
    if(card_list[i]->get_number() == number){
      result_list[count] = card_list[i - 1];
      count++;
      if(count == 2){
        result_list[count] = card_list[i];
        while(two_left_count != 5){
          i++;
          result_list[two_left_count] = card_list[i];
          two_left_count++;
        }
        break;
      }
    }
    else{
      if(two_left_count != 5){
        result_list[two_left_count] = card_list[i - 1];
        two_left_count++;
      }
      count = 0;
    }
    number = card_list[i]->get_number();
  }
  if(count == 2){
    return 4;
  }
  memset(result_list, 0, 5*sizeof(Card*));
  return -1;
}

int table::check_two_pairs(Card* card_list[]){
  int two_strike = 0;
  int number = card_list[0]->get_number();
  int double_num = 0;
  bool single_left = false;
  for(int i = 1; i < 7; i++){
    if(card_list[i]->get_number() == number){
      result_list[two_strike] = card_list[i - 1];
      two_strike++;
      if(two_strike == 1){
        result_list[two_strike] = card_list[i];
        double_num = number;
        break;
      }
    }
    number = card_list[i]->get_number();
    if(single_left == false){
      result_list[4] = card_list[i - 1];
      single_left = true;
    }
  }
  if(two_strike >= 1){
    two_strike = 0;
    number = card_list[0]->get_number();
    for(int j = 1; j < 7; j++){
      if(double_num != number){
        if(card_list[j]->get_number() == number){
          result_list[two_strike + 2] = card_list[j - 1];
          two_strike++;
          if(two_strike == 1){
            result_list[two_strike + 2] = card_list[j];
            if(single_left == false){
              result_list[4] = card_list[j + 1];
            }
            break;
          }
        }
      }
      if(single_left == false && double_num != number && number != card_list[j]->get_number()){
        result_list[4] = card_list[j - 1];
        single_left = true;
      }
      number = card_list[j]->get_number();
    }
    if(two_strike == 1){
      return 3;
    }
  }
  memset(result_list, 0, 5*sizeof(Card*));
  return -1;
}

int table::check_one_pair(Card* card_list[]){
  int count = 0;
  int number = card_list[0]->get_number();
  int triple_count = 2;
  for(int i = 1; i < 7; i++){
    if(card_list[i]->get_number() == number){
      result_list[count] = card_list[i - 1];
      count++;
      if(count > 1){
        break;
      }
      result_list[count] = card_list[i];
      while(triple_count != 5){
        result_list[triple_count] = card_list[i + 1];
        triple_count++;
      }
    }
    if(triple_count != 5){
      result_list[triple_count] = card_list[i - 1];
      triple_count++;
    }
    number = card_list[i]->get_number();
  }
  if(count == 1){
    return 2;
  }
  memset(result_list, 0, 5*sizeof(Card*));
  return -1;
}

