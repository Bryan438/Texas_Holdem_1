#include <iostream>
#include <string.h>
#include "card.h"

Card::Card(card_suit s, int n){
  suit = s;
  number = n;
}

void Card::show() const{
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
    case Spade:
      str_suit = "Spade";
      break;
    default:
      str_suit = "Unknown";
      break;
  }
  printf("suit : %s\n number : %d\n", str_suit, number);
}

void Card::set_number(int n){
  number = n;
}

card_suit Card::get_suit() const{
  return suit;
}

int Card::get_number() const{
  return number;
}

static const char* rank_code(int number){
  static const char* codes[] = {"?", "?", "2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K", "A"};
  return (number >= 2 && number <= 14) ? codes[number] : "?";
}

static char suit_code(card_suit suit){
  switch(suit){
    case Spade: return 's';
    case Heart: return 'h';
    case Diamond: return 'd';
    case Club: return 'c';
    default: return '?';
  }
}

std::string Card::code() const{
  return std::string(rank_code(number)) + suit_code(suit);
}

std::string Card::text() const{
  std::string rank = number == 10 ? "10" : rank_code(number);
  switch(suit){
    case Spade: return rank + "\u2660";
    case Heart: return rank + "\u2665";
    case Diamond: return rank + "\u2666";
    case Club: return rank + "\u2663";
    default: return rank + "?";
  }
}
