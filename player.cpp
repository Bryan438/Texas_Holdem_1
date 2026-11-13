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
  total_bet = 0;
  raise_status = 0;
  grade = -1;
  player_status = true;
  allin_status = false;
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

void player::set_name(const char* name){
  // Copy: the buffer we get comes from a network message that is freed later.
  player_name = name != NULL ? name : "";
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

const char* player::get_name(){
  return player_name.c_str();
}

// Move up to `amount` chips from the stack into the pot. Going to zero means all-in.
int player::put_in(int amount){
  if(amount < 0){
    amount = 0;
  }
  if(amount >= remaining_money){
    amount = remaining_money;
    allin_status = true;
  }
  remaining_money -= amount;
  current_round_bet += amount;
  total_bet += amount;
  return amount;
}

int player::post_blind(int amount){
  return put_in(amount);
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

// Match the current bet. If the stack is too small this is an all-in for less.
int player::call(int current){
  return put_in(current - current_round_bet);
}

// Raise so this round's bet becomes `new_amount` in total (all-in if the stack runs out).
int player::raise(int new_amount){
  return put_in(new_amount - current_round_bet);
}

void player::fold(){
  player_status = false;
}

void player::reset_raise_status(){
  raise_status = 0;
}

void player::reset_round(){
  current_round_bet = 0;
}

void player::reset_game(){
  player_status = true;
  allin_status = false;
  current_round_bet = 0;
  total_bet = 0;
  raise_status = 0;
  grade = -1;
  card1 = NULL;
  card2 = NULL;
}




  
  
