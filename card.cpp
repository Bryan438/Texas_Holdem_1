#include <iostream>
#include <string.h>
#include "card.h"

Card::Card(const char* s, int n){
  suit = s;
  number = n;
}

void Card::show(){
  printf("suit : %s\n number : %d\n", suit, number);
}

void Card::set_suit(const char* s){
  suit = s;
}

void Card::set_number(int n){
  number = n;
}

const char* Card::get_suit(){
  return suit;
}

int Card::get_number(){
  return number;
}
