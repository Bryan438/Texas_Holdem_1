#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

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
  raise_status = 0;
  winning_sidepot_amount = 0;
  grade = -1;
  player_status = true;
  allin_status = false;
  need_sidepot = false;
}

int player::get_client_socket(){
  return client_socket;
}

void player::send(){
  char message[50];
  memset(message, 0, 50);
  while(true){
    printf("Send message? \n");
    std::cin >> message;
    transport::get_instance()->serialize(client_socket, 4, my_strlen(message), message);
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
  int card_num1 = c1->get_number();
  int card_num2 = c2->get_number();
  card_suit suit1 = c1->get_suit();
  card_suit suit2 = c2->get_suit();
  char message[10]; 
  memset(message, 0, 10);

  int n_num1 = htonl(card_num1);
  memcpy(message, &n_num1, 4);
  message[4] = suit1; 

  int n_num2 = htonl(card_num2);
  memcpy(message + 5, &n_num2, 4);
  message[9] = suit2; 

  transport::get_instance()->serialize(client_socket, 6, 10, message);
  card1 = c1;
  card2 = c2;
}

void player::set_public_card(Card* c1, Card* c2, Card* c3){
  card_suit suit1 = c1->get_suit();
  card_suit suit2;
  card_suit suit3;
  if(c2 != NULL){
    suit2 = c2->get_suit();
    suit3 = c3->get_suit();
  }
  char message[15]; 
  memset(message, 0, 15);

  int n_num1 = htonl(c1->get_number());
  memcpy(message, &n_num1, 4);
  message[4] = suit1; 

  if(c2 != NULL){
    int n_num2 = htonl(c2->get_number());
    memcpy(message + 5, &n_num2, 4);
    message[9] = suit2; 

    int n_num3 = htonl(c3->get_number());
    memcpy(message + 10, &n_num3, 4);
    message[14] = suit3; 
  }
  transport::get_instance()->serialize(client_socket, 10, 15, message);
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

void player::set_raise_status(){
  raise_status = 1;
}

int player::get_raise_status(){
  return raise_status;
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

char* player::get_name(){
  return player_name;
}

void player::set_money(int moneychange){
  char message[15]; 
  memset(message, 0, 15);
  remaining_money = remaining_money - moneychange;
  current_round_bet += moneychange;
  total_bet += moneychange;
}

void player::add_money(int moneychange){
  char message[8]; 
  memset(message, 0, 8);
  
  int n_moneychange = htonl(moneychange);
  memcpy(message, &n_moneychange, 4);

  remaining_money += moneychange;
  int n_remaining = htonl(remaining_money);
  memcpy(message + 4, &n_remaining, 4);

  transport::get_instance()->serialize(client_socket, 20, 8, message);
}

int player::get_money(){
  return remaining_money;
}

void player::set_sidepot(int money){
  winning_sidepot_amount = money;
}

int player::get_sidepot(){
  return winning_sidepot_amount;
}

bool player::get_player_status(){
  return player_status;
}

void player::set_allin_status(){
  allin_status = true;
}

bool player::get_allin_status(){
  return allin_status;
}

int player::get_current_round_bet(){
  return current_round_bet;
}

int player::get_total_bet(){
  return total_bet;
}

int player::call(int current){
  if(remaining_money <= current){
    int remaining = remaining_money;
    total_bet += remaining_money;
    current_round_bet = remaining_money;
    remaining_money = 0;
    allin_status = true;
    
    //If the money of the current player is less than current bet, then the player need a sidepot
    if(remaining_money < current){
      need_sidepot = true;
    }
    return remaining;
  }
  int difference = current - current_round_bet;
  current_round_bet = current;
  total_bet += difference;
  remaining_money -= difference;
  return difference;
}

//Current_bet and current_round_bet are different. CB is the money bet by all the player this round, CRB is only pointed to this player
int player::raise(int new_amount){
  if(remaining_money == new_amount){
    int remaining = remaining_money;
    total_bet += remaining_money;
    current_round_bet = remaining_money;
    allin_status = true;
    remaining_money = 0;
    return remaining;
  }
  int difference = new_amount - current_round_bet;
  current_round_bet = new_amount;
  total_bet += difference;
  remaining_money -= difference;
  return difference;
}

void player::fold(){
  player_status = false;
}

void player::reset_raise_status(){
  raise_status = 0;
}

void player::set_needsidepot_status(){
  if(need_sidepot == false){
    need_sidepot = true;
  }
  else{
    need_sidepot = false;
  }
}

bool player::get_needsidepot_status(){
  return need_sidepot;
}

void player::reset_round(){
  current_round_bet = 0;
}

void player::reset_game(){
  player_status = true;
  allin_status = false;
  current_round_bet = 0;
}




  
  
