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
/*
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
  */
}

void table_server::add_player(player* p){
  player_list[number_of_player] = p;
  printf("Name = %s", player_list[number_of_player]->get_name());
  number_of_player += 1;
  active_player += 1;
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

int table_server::get_active_player(){
  return active_player;
}

void table_server::add_total_pot(int amount){
  total_pot += amount;
}

int table_server::get_available_decision(int player_num){

  int combined = 0x00;
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
  
  printf("Player %d, ", player_num); 
  if(combined & FOLD){
    printf(" [F]old ");
  }
  if(combined & RAISE){
    printf(" [R]aise ");
  }
  if(combined & CALL){
    printf(" [C]all ");
  }
  if(combined & CHECK){
    printf(" c[H]eck ");
  }
  return combined;
}

int table_server::check_valid_input(int combined, char input){
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

char table_server::input(){
  
 /* int listenfd = 0;;
  int connfd = 0;;  
  int rece_client = 0;


  struct sockaddr_in serv_addr; 
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(5000); 

  bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 
  listen(listenfd, 10); 
  while(1)
  {
    connfd = accept(listenfd, (struct sockaddr*)NULL, NULL); 
  }

  recv(connfd, rece_client, sizeof(from_client), 0);
  return rece_client;
  close(connfd);
  */
  char decision;
  std::cin >> decision;
  return decision;
}


int table_server::input_action(int decision, int player_num, int raise_status){
  int money_added = 0;
  switch(decision){
    case FOLD:
      player_list[player_num]->fold();
      break;
    case CALL:
      money_added = player_list[player_num]->call(current_bet);
      break;
    case CHECK:
      money_added = player_list[player_num]->call(current_bet);
      break;
    default:
      money_added = player_list[player_num]->raise();
      current_bet = player_list[player_num]->get_current_round_bet();
      raise_status = 1;
      break;
  }
  total_pot += money_added;
  printf("money remaining = %d, money add = %d\n", player_list[player_num]->get_money(), money_added);
  return raise_status;
}

int table_server::check_available_decision(int player_num, int raise_status){
  int combined_decision = get_available_decision(player_num);
  int decision = 0;
  int valid_status = -1;
  while(valid_status == -1){
    decision = input();
    valid_status = check_valid_input(combined_decision, decision);
  }

  raise_status = input_action(valid_status, player_num, raise_status);
  if(valid_status == FOLD){
    active_player--;
  }
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
    if(player_list[start_pos]->get_allin_status() == true || player_list[start_pos]->get_player_status() == false){
      start_pos++;
      if(start_pos >= number_of_player){
        start_pos = 0;
      }
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
    if(player_list[current_pos]->get_player_status() == false || player_list[current_pos]->get_allin_status() == true){
      current_pos++;
      if(current_pos >= number_of_player){
        current_pos = 0;
      }
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

int table_server::get_highest_grade(){
  int highest_grade = -1;
  for(int i = 0; i < number_of_player; i++){
    int current_grade = player_list[i]->get_grade();
    if(current_grade > highest_grade){
      highest_grade = current_grade;
    }
  }
  return highest_grade;
}

void table_server::showdown(){
  if(active_player == 1){
    all_fold_case();
  }
  else{
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
    int winning_money = total_pot / winner_count;
    for(int i = 0; i < winner_count; i++){
      if(winner_list[i]->get_allin_status()){
        winner_list[i]->add_money(winner_list[i]->get_current_round_bet());
      }
      else{
        winner_list[i]->add_money(winning_money);
      }
    }
  }
}

void table_server::all_fold_case(){
  for(int i = 0; i < number_of_player; i++){
    if(player_list[i]->get_player_status() == true){
      player_list[i]->add_money(total_pot);
    }
  }
}
/*void table_server::compare(Card* cur_result_list[], Card* high_result_list[]){
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

  }*/
void table_server::reset(){
  for(int i = 0; i < number_of_player; i++){
    player_list[i]->reset_game();
  }
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

void table_server::reverse(Card* card_list[]){
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

int table_server::calculate_grade(Card* card_list[]){
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

int table_server::check_straight_flush(Card* card_list[]){
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

int table_server::check_four_kind(Card* card_list[]){
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

int table_server::check_three_kind(Card* card_list[]){
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

int table_server::check_two_pairs(Card* card_list[]){
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

int table_server::check_one_pair(Card* card_list[]){
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

void table_server::print_result_list(){
  for(int i = 0; i < 5; i++){
    result_list[i]->show();
  }
}
