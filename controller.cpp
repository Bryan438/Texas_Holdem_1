#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "card.h"
#include "deck_of_card.h"
#include "player.h"
#include "table_server.h"

void test_case_straight_flush(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Spade, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Heart, 12);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Heart, 10);
  card_list[6] = new Card(Heart, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_straight_flush(card_list));
  ts->print_result_list();
}

void test_case_four_kind(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Spade, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Heart, 12);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Heart, 10);
  card_list[6] = new Card(Heart, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_four_kind(card_list));
}

void test_case_four_kind2(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Spade, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Club, 13);
  card_list[4] = new Card(Diamond, 13);
  card_list[5] = new Card(Heart, 10);
  card_list[6] = new Card(Heart, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_four_kind(card_list));
  ts->print_result_list();
}

void test_case_full_house(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Spade, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Club, 13);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Spade, 11);
  card_list[6] = new Card(Heart, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_full_house(card_list));
  ts->print_result_list();
}

void test_case_full_house2(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Spade, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Club, 11);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Spade, 11);
  card_list[6] = new Card(Heart, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_full_house(card_list));
  ts->print_result_list();
}

void test_case_full_house3(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Spade, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Club, 11);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Spade, 10);
  card_list[6] = new Card(Heart, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_full_house(card_list));
}

void test_case_full_house4(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Spade, 14);
  card_list[2] = new Card(Heart, 14);
  card_list[3] = new Card(Club, 11);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Spade, 11);
  card_list[6] = new Card(Heart, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_full_house(card_list));
  ts->print_result_list();
}

void test_case_flush(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Club, 13);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Club, 10);
  card_list[6] = new Card(Club, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_flush(card_list));
  ts->print_result_list();
}

void test_case_flush2(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Heart, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Heart, 13);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Heart, 10);
  card_list[6] = new Card(Club, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_flush(card_list));
  ts->print_result_list();
}

void test_case_flush3(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Heart, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Heart, 13);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Spade, 10);
  card_list[6] = new Card(Club, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_flush(card_list));
}

void test_case_straight(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Club, 12);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Club, 10);
  card_list[6] = new Card(Club, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_straight(card_list));
  ts->print_result_list();
}

void test_case_straight2(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 13);
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 11);
  card_list[3] = new Card(Spade, 10);
  card_list[4] = new Card(Heart, 9);
  card_list[5] = new Card(Club, 8);
  card_list[6] = new Card(Club, 7);

  table_server* ts = new table_server();
  printf("%d", ts->check_straight(card_list));
  ts->print_result_list();
}

void test_case_straight3(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 13);
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 11);
  card_list[3] = new Card(Spade, 10);
  card_list[4] = new Card(Heart, 8);
  card_list[5] = new Card(Club, 8);
  card_list[6] = new Card(Club, 7);

  table_server* ts = new table_server();
  printf("%d", ts->check_straight(card_list));
}

void test_case_three_kind2(){
  Card* card_list[7];
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Spade, 13);
  card_list[0] = new Card(Club, 12);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Club, 10);
  card_list[6] = new Card(Club, 8);

  table_server* ts = new table_server();
  printf("%d", ts->check_three_kind(card_list));
  ts->print_result_list();
}

void test_case_three_kind(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Spade, 13);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Club, 10);
  card_list[6] = new Card(Club, 9);

  table_server* ts = new table_server();
  printf("%d", ts->check_three_kind(card_list));
  ts->print_result_list();
}

void test_case_two_pair(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Spade, 11);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Club, 10);
  card_list[6] = new Card(Club, 9);

  table_server* ts = new table_server();
  printf("%d ", ts->check_two_pairs(card_list));
  ts->print_result_list();
}

void test_case_two_pair2(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Spade, 11);
  card_list[4] = new Card(Heart, 10);
  card_list[5] = new Card(Club, 10);
  card_list[6] = new Card(Club, 9);

  table_server* ts = new table_server();
  printf("%d ", ts->check_two_pairs(card_list));
  ts->print_result_list();
}

void test_case_two_pair3(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Club, 14);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Spade, 13);
  card_list[4] = new Card(Heart, 10);
  card_list[5] = new Card(Club, 10);
  card_list[6] = new Card(Club, 9);

  table_server* ts = new table_server();
  printf("%d ", ts->check_two_pairs(card_list));
  ts->print_result_list();
}

void test_case_two_pair4(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Club, 14);
  card_list[2] = new Card(Heart, 13);
  card_list[3] = new Card(Spade, 12);
  card_list[4] = new Card(Heart, 12);
  card_list[5] = new Card(Club, 10);
  card_list[6] = new Card(Club, 9);

  table_server* ts = new table_server();
  printf("%d ", ts->check_two_pairs(card_list));
  ts->print_result_list();
}

void test_case_one_pair(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 12);
  card_list[3] = new Card(Spade, 11);
  card_list[4] = new Card(Heart, 9);
  card_list[5] = new Card(Club, 8);
  card_list[6] = new Card(Spade, 8);

  table_server* ts = new table_server();
  printf("%d ", ts->check_one_pair(card_list));
  ts->print_result_list();
}

void test_case_one_pair2(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 11);
  card_list[3] = new Card(Spade, 11);
  card_list[4] = new Card(Heart, 9);
  card_list[5] = new Card(Club, 8);
  card_list[6] = new Card(Spade, 7);

  table_server* ts = new table_server();
  printf("%d ", ts->check_one_pair(card_list));
  ts->print_result_list();
}

void test_case_one_pair3(){
  Card* card_list[7];
  card_list[0] = new Card(Club, 14);
  card_list[1] = new Card(Club, 13);
  card_list[2] = new Card(Heart, 11);
  card_list[3] = new Card(Spade, 11);
  card_list[4] = new Card(Heart, 11);
  card_list[5] = new Card(Club, 8);
  card_list[6] = new Card(Spade, 7);

  table_server* ts = new table_server();
  printf("%d ", ts->check_one_pair(card_list));
  ts->print_result_list();
}


int main(){
  
  table_server* ts = new table_server();
  ts->preparation();
  ts->set_inital_card();
  /*
  ts->preflop();
  ts->flop();
  ts->turn();
  ts->river();
  */

  void (table_server::*preflop_ptr)();
  preflop_ptr = &table_server::preflop;

  void (table_server::*flop_ptr)();
  flop_ptr = &table_server::flop;

  void (table_server::*turn_ptr)();
  turn_ptr = &table_server::turn;

  void (table_server::*river_ptr)();
  river_ptr = &table_server::river;

  void (table_server::*process_array[5])();
  process_array[0] = preflop_ptr;
  process_array[1] = flop_ptr;
  process_array[2] = turn_ptr;
  process_array[3] = river_ptr;

  for(int i = 0; i < 4; i++){
    if(ts->get_active_player() == 1){
      break ;
    }
    (*ts.*process_array[i])();
  }
  ts->showdown();
  ts->reset();
  /*test_case_straight_flush();
    test_case_four_kind();
    test_case_four_kind2();

    test_case_full_house();
    test_case_full_house2();
    test_case_full_house3();
    test_case_full_house4();

    test_case_flush();
    test_case_flush2();
    test_case_flush3();

  test_case_straight();
  test_case_straight2();

  test_case_three_kind();
  test_case_three_kind2();

  test_case_two_pair();
  test_case_two_pair2();
  test_case_two_pair3();
  test_case_two_pair4();

  test_case_one_pair();
  test_case_one_pair2();
  test_case_one_pair3();*/
}

