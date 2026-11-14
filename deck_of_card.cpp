#include <iostream>
#include <string.h>
#include <algorithm>
#include <random>
#include "card.h"
#include "deck_of_card.h"

// Seeded from the OS once per deck. (Reseeding with time(NULL) on every
// shuffle used to give two hands started within the same second the same deck.)
deck_of_card::deck_of_card() : rng(std::random_device{}()){
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
  std::shuffle(cards, cards + 52, rng);
}

void deck_of_card::seed(unsigned value){
  rng.seed(value);
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
  
  
