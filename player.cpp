#include <iostream>
#include <string.h>
#include <stdlib.h>
#include "card.h"
#include "deck_of_card.h"
#include "player.h"
#include "transport.h"

player::player(int socket_id, int money){
  client_socket = socket_id;
  remaining_money = money;
  card1 = NULL;
  card2 = NULL;
  current_round_bet = 0;
  grade = -1;
  player_status = true;
  allin_status = false;
}

int player::get_client_socket(){
  return client_socket;
}

void player::send(transport* tp){
  char message[50];
  memset(message, 0, 50);
  while(true){
    printf("Send message? \n");
    std::cin >> message;
    tp->serialize(client_socket, 4, my_strlen(message), message);
  }

}

int player::my_strlen(char* str){
  int i = 0;
  if(!str){
    return -1;
  }
  while(str[i] != 0){
    i++;
  }
  return i;
}

void player::set_name(char* name){
  player_name = name;
}

void player::set_inital_bet(int bet){
  remaining_money = bet;
}

void player::set_initial_card(Card* c1, Card* c2){
  card1 = c1;
  card2 = c2;
}
  
void player::set_result_card(Card* card_list[]){
  for(int i = 0; i < 5; i++){
    result_list[i] = card_list[i];
  }
}

Card** player::get_result_card(){
  return result_list;
}

void player::set_grade(int grade_num){
  grade = grade_num;
}

int player::get_grade(){
  return grade;
}

Card* player::get_first_card(){
  return card1;
}

Card* player::get_second_card(){
  return card2;
}

void player::show_card(){
  card1->show(); 
  card2->show();
}

const char* player::get_name(){
  return player_name;
}

void player::set_money(int moneychange){
  remaining_money = remaining_money -= moneychange;
  current_round_bet += moneychange;
}

void player::add_money(int moneychange){
  remaining_money += moneychange;
}

int player::get_money(){
  return remaining_money;
}

bool player::get_player_status(){
  return player_status;
}

bool player::get_allin_status(){
  return allin_status;
}

int player::get_current_round_bet(){
  return current_round_bet;
}

int player::call(int current){
  if(remaining_money <= current){
    int remaining = remaining_money;
    current_round_bet = remaining_money;
    remaining_money = 0;
    allin_status = true;
    return remaining;
  }
  int difference = current - current_round_bet;
  current_round_bet = current;
  remaining_money -= difference;
  return difference;
}

int player::raise(){
  int new_amount;
  printf("Enter the new bet : ");
  std::cin >> new_amount;
  if(current_round_bet > new_amount){
    printf("Invalid input");
    return -1;
  }
  if(remaining_money == new_amount){
    int remaining = remaining_money;
    current_round_bet = remaining_money;
    allin_status = true;
    remaining_money = 0;
    return remaining;
  }
  int difference = new_amount - current_round_bet;
  current_round_bet = new_amount;
  remaining_money -= difference;
  return difference;
}

void player::fold(){
  player_status = false;
}

void player::reset_round(){
  current_round_bet = 0;
}

void player::reset_game(){
  player_status = true;
  allin_status = false;
  current_round_bet = 0;
}




  
  
