#include <iostream>
#include <string.h>
#include <stdlib.h>
#include "card.h"
#include "deck_of_card.h"
#include "player.h"
#include "table_server.h"

table_server::table_server(){
  dcards = new deck_of_card(); 
  total_pot = 0;
  current_bet = 10;
  current_round = 1;
  current_game = 0;
}

void table_server::preparation(){

  dcards->shuffle();
  dcards->show();
  card_index = 0;

  dealer_button = current_game;
  small_blind = dealer_button + 1;
  big_blind = dealer_button + 2;

  char* name = (char*)malloc(100);
  int chips;
  int decision;

  while(true){
    printf("1. Add player(Enter name and chips) 2.Start\n");
    std::cin >> decision;
    if(decision != 1){
      break;
    }

    printf("Player name : ");
    std::cin >> name;
    printf("Chips : ");
    std::cin >> chips;

    player* p = new player(name, 0, chips);
    add_player(p);
  }

  free(name);
}

void table_server::add_player(player* p){
  player_list[number_of_player] = p;
  printf("Name = %s", player_list[number_of_player]->get_name());
  number_of_player += 1;
}

int table_server::get_player_number(){
  return number_of_player;
}

void table_server::set_inital_card(){
  for(int i = 0; i < number_of_player; i++){
    player_list[i]->set_initial_card(dcards->get_card(2*i), dcards->get_card(2*i+1));
    dcards->get_card(2*i)->show();
    dcards->get_card(2*i+1)->show();
    card_index += 2;
  }
}

int table_server::get_current_bet(){
  return current_bet;
}

void table_server::add_total_pot(int amount){
  total_pot += amount;
}

int table_server::check_available_decision(int player_num, int raise_status){
  int decision;
  int money_added;
  if(player_list[player_num]->get_money() <= current_bet - player_list[player_num]->get_current_round_bet()){
    printf("Player %d, The options for you are FOLD/CALL(Allin), Press 1 for Fold; 2 for Call", player_num); 
    std::cin >> decision;
    if(decision == 1){
      player_list[player_num]->fold();
    }
    else if(decision == 2){
      money_added = player_list[player_num]->call(current_bet);
    }
    else{
      printf("Invalid input");
    }
  }
  else{
    if(current_round > 1){
      printf("Player %d, The options for you are FOLD/CHECK/RAISE, Press 1 for Fold; 2 for Check; 3 for Raise", player_num); 
    }
    else{
      printf("Player %d, The options for you are FOLD/CALL/RAISE, Press 1 for Fold; 2 for Call; 3 for Raise", player_num); 
    }
    std::cin >> decision;
    if(decision == 1){
      player_list[player_num]->fold();
      money_added = 0;
    }
    else if(decision == 2){
      money_added = player_list[player_num]->call(current_bet);
    }
    else if(decision == 3){
      money_added = player_list[player_num]->raise();
      current_bet = player_list[player_num]->get_current_round_bet();
      raise_status = 1;
    }
    else{
      printf("Invalid input");
    }
  }
  total_pot += money_added;
  printf("money remaining = %d, money add = %d\n", player_list[player_num]->get_money(), money_added);
  return raise_status;
}

void table_server::preflop(){
  if(current_round != 1){
    printf("Invalid round");
    return;
  }
  int end_pos = big_blind;
  int start_pos = big_blind + 1;
  int raise_status = 0;
  player_list[small_blind]->set_money(current_bet/2);
  player_list[big_blind]->set_money(current_bet);
  if(start_pos >= number_of_player){
    start_pos = 0;
  }
  while(start_pos != end_pos){
    if(player_list[start_pos]->get_allin_status() == false && player_list[start_pos]->get_player_status() == false){
      start_pos++;
      continue;
    }
    raise_status = check_available_decision(start_pos, raise_status);
    printf("current_bet = %d\n", current_bet);
    if(raise_status == 1){
      end_pos = start_pos;
      raise_status = 0;
    }
    start_pos++;
    if(start_pos >= number_of_player){
      start_pos = 0;
    }
  }
  for(int i = 0; i < number_of_player; i++){
    player_list[start_pos]->get_current_round_bet();
  }
  current_round += 1;
}

void table_server::round(int card_count){
  int raise_status = 0;
  current_bet = 0;
  for(int j = 0; j < number_of_player; j++){
    if(player_list[j]->get_player_status() == true){
      player_list[j]->reset_round();
    }
  }
  for(int i = 0; i < card_count; i++){
    dcards->get_card(card_index)->show();
    card_list[card_index - 2*number_of_player] = dcards->get_card(card_index);
    card_index++;
  }

  int current_pos = dealer_button;
  if(current_pos >= number_of_player){
    current_pos = 0;
  }
  while(player_list[current_pos]->get_player_status() == false){
    current_pos += 1;
    if(current_pos >= number_of_player){
      current_pos = 0;
    }
  }
  int end_pos = current_pos;
  bool first_status = true;
  while(current_pos != end_pos || first_status == true){
    first_status = false;
    if(player_list[current_pos]->get_player_status() == false){
      current_pos++;
      continue;
    }
    raise_status = check_available_decision(current_pos, raise_status);
    printf("current_bet = %d\n", current_bet);
    if(raise_status == 1){
      end_pos = current_pos;
      raise_status = 0;
    }
    current_pos++;
    if(current_pos >= number_of_player){
      current_pos = 0;
    }
  }
  first_status = true;
}

void table_server::flop(){
  if(current_round != 2){
    printf("Invalid round");
  }
  round(3);
  current_round++;
}

void table_server::turn(){
  if(current_round != 3){
    printf("Invalid round");
  }
  round(1);
  current_round++;
}

void table_server::river(){
  if(current_round != 4){
    printf("Invalid round");
  }
  round(1);
  current_round++;
}

void table_server::showdown(){
  Card* card_cpy[7];
  
  //for(int i = 0; i < number_of_player; i++){
     for(int j = 0; j < 5; j++){
       card_cpy[j] = card_list[j];
     }
     card_cpy[5] = player_list[0]->get_first_card();
     card_cpy[6] = player_list[0]->get_second_card();
     sort(0, 6, card_cpy);
     
     //check_straight_flush();
  //}
}

void table_server::sort(int item_from_left, int item_from_right, Card* card_list[]){
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

void table_server::swap(int index1, int index2, Card* card_list[]){
  Card* temp = card_list[index1];
  card_list[index1] = card_list[index2];
  card_list[index2] = temp;
}

void table_server::calculate_grade(){
}

int table_server::check_straight_flush(Card* card_list[]){
  bool ace_status = false;
  int count = 1;

  for(int i = 0; i < 3; i++){
    const char* suit = card_list[i]->get_suit();
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

int table_server::check_four_kind(Card* card_list[]){
  int count = 0;
  int number = card_list[0]->get_number();
  bool single_mark = false;
  for(int i = 1; i < 7; i++){
    if(card_list[i]->get_number() == number){
      result_list[count+1] = card_list[i-1]; 
      count++;
      if(count == 3){
        result_list[count+1] = card_list[i]; 
        if(single_mark == false){
          result_list[0] = card_list[i+1]; 
          single_mark = true;
        }
        break;
      }
    }
    else{
      if(single_mark == false){
        result_list[0] = card_list[0];
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

int table_server::check_full_house(Card* card_list[]){
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

int table_server::check_straight(Card* card_list[]){
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

int table_server::check_flush(Card* card_list[]){
  int count = 0;
  int spade_count = 0;
  int club_count = 0;
  int diamond_count = 0;
  int heart_count = 0;
  const char* dest_card;

  for(int i = 0; i < 7; i++){
    if(strcmp(card_list[i]->get_suit(),"Spade") == 0){
      spade_count++;
    }
    if(strcmp(card_list[i]->get_suit(),"Club") == 0){
      club_count++;
    }
    if(strcmp(card_list[i]->get_suit(),"Diamond") == 0){
      diamond_count++;
    }
    if(strcmp(card_list[i]->get_suit(),"Heart") == 0){
      heart_count++;
    }
  }
  if(spade_count == 5){
    dest_card = "Spade";
  }
  if(club_count == 5){
    dest_card = "Club";
  }
  if(heart_count == 5){
    dest_card = "Heart";
  }
  if(diamond_count == 5){
    dest_card = "Diamond";
  }
  if(dest_card != NULL){
    for(int i = 0; i < 7; i++){
      if(strcmp(card_list[i]->get_suit(),dest_card) == 0){
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

int table_server::check_three_kind(Card* card_list[]){
  int count = 0;
  int number = card_list[0]->get_number();
  int two_left_count = 0;
  for(int i = 1; i < 7; i++){
    if(card_list[i]->get_number() == number){
      result_list[count + 2] = card_list[i - 1];
      count++;
      if(count == 2){
        result_list[count + 2] = card_list[i];
        while(two_left_count != 2){
          i++;
          result_list[two_left_count] = card_list[i];
          two_left_count++;
        }
        break;
      }
    }
    else{
      if(two_left_count != 2){
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

int table_server::check_two_pairs(Card* card_list[]){
  int two_strike = 0;
  int number = card_list[0]->get_number();
  int double_num = 0;
  bool single_left = false;
  for(int i = 1; i < 7; i++){
    if(card_list[i]->get_number() == number){
      result_list[two_strike + 1] = card_list[i - 1];
      two_strike++;
      if(two_strike == 1){
        result_list[two_strike + 1] = card_list[i];
        double_num = number;
        break;
      }
    }
    number = card_list[i]->get_number();
    if(single_left == false){
      result_list[0] = card_list[i - 1];
      single_left = true;
    }
  }
  if(two_strike >= 1){
    two_strike = 0;
    number = card_list[0]->get_number();
    for(int j = 1; j < 7; j++){
      if(double_num != number){
        if(card_list[j]->get_number() == number){
          result_list[two_strike + 3] = card_list[j - 1];
          two_strike++;
          if(two_strike == 1){
            result_list[two_strike + 3] = card_list[j];
            if(single_left == false){
              result_list[0] = card_list[j + 1];
            }
            break;
          }
        }
      }
      if(single_left == false && double_num != number && number != card_list[j]->get_number()){
        result_list[0] = card_list[j];
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

int table_server::check_one_pair(Card* card_list[]){
  int count = 0;
  int number = card_list[0]->get_number();
  int triple_count = 0;
  for(int i = 1; i < 7; i++){
    if(card_list[i]->get_number() == number){
      result_list[count + 3] = card_list[i - 1];
      count++;
      if(count > 1){
        break;
      }
      result_list[count + 3] = card_list[i];
      while(triple_count != 3){
        result_list[triple_count] = card_list[i + 1];
        triple_count++;
      }
    }
    if(triple_count != 3){
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

void table_server::print_result_list(){
  for(int i = 0; i < 5; i++){
    result_list[i]->show();
  }
}