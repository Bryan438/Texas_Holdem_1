#include <iostream>
#include <string.h>
#include <algorithm>
#include <random>
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
  // Seeded once from the OS; reseeding with time(NULL) on every shuffle gave
  // two hands started within the same second the exact same deck.
  static std::mt19937 rng(std::random_device{}());
  std::shuffle(cards, cards + 52, rng);
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
  
  
