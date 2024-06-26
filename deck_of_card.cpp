#include <iostream>
#include <string.h>
#include "card.h"
#include "deck_of_card.h"

deck_of_card::deck_of_card(){
  card_suit suit;
  for(int i = 2; i <= 14; i++){
    for(int j = 1; j <= 4; j++){
      if(j == 1){
        suit = Diamond;
      }
      else if(j == 2){
        suit = Club;
      }
      else if(j == 3){
        suit = Heart;
      }
      else{
        suit = Spade;
      }
      cards[(i * 4 + j) - 9] = new Card(suit,i);
    }
  }
}

void deck_of_card::shuffle(){
  Card* shuffled_card[52];
  int count = -1;
  int sec_count = -1;
  srand(time(NULL));
  for(int i = 0; i < 52; i++){
    int random_card_num = rand() % (52 - i);
    while(count != random_card_num){
      sec_count++;
      if(cards[sec_count] != NULL){
        count++;
      }
    }
    shuffled_card[i] = cards[sec_count];
    cards[sec_count] = NULL;
    count = -1;
    sec_count = -1;
  }
  for(int i = 0; i < 52; i++){
   cards[i] = shuffled_card[i];
  }
}

deck_of_card::~deck_of_card(){
  for(int i = 0; i < 52; i++){
    delete cards[i];
  }
}

void deck_of_card::show(){
  for(int i = 0; i < 52; i++){
    cards[i]->show();
  }
}

Card* deck_of_card::get_card(int index){
  return cards[index];
}
  
  
