#include <iostream>
#include <string.h>
#include "card.h"

Card::Card(card_suit s, int n){
  suit = s;
  number = n;
}

void Card::show(){
  const char* str_suit = NULL;
  switch(suit){
    case Diamond:
      str_suit = "Diamond";
      break;
    case Heart:
      str_suit = "Heart";
      break;
    case Club:
      str_suit = "Club";
      break;
    default:
      str_suit = "Spade";
      break;
  }
  printf("suit : %s\n number : %d\n", str_suit, number);
}

void Card::set_suit(card_suit s){
  suit = s;
}

void Card::set_number(int n){
  number = n;
}

card_suit Card::get_suit(){
  return suit;
}

int Card::get_number(){
  return number;
}
