#include <string.h>
#include <stdlib.h>
#include <cstdio>
#include <stdio.h>
#include "transport.h"
#include "message_content.h"
#include "table.h" 
#include "card.h"

void table::handle_message(message_content* message, int socket_id){
  if(message->get_charmessage() != NULL){
    printf("%s\n", message->get_charmessage());
  }
  else{
    printf("%d\n", message->get_intmessage());
  }
  printf("%d\n", message->get_length());
  printf("%d\n", message->get_command());
  /*char* msg = "Echo back";
  int temp = tp->get_client_socket();
  tp->serialize(temp, 5, 9, msg);

  */

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

    default:
      break;
  }
}

void table::set_transport(transport* tport){
  tp = tport;
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

  dcards->shuffle();
  dcards->show();
  card_index = 0;

  dealer_button = current_game;
  small_blind = dealer_button + 1;
  big_blind = dealer_button + 2;
}
