#include "player.h"

int player::put_in(int amount){
  if(amount < 0){
    amount = 0;
  }
  if(amount >= stack){
    amount = stack;
    all_in = true;
  }
  stack -= amount;
  round_bet += amount;
  total_bet += amount;
  return amount;
}

void player::reset_for_hand(){
  dealt = false;
  in_hand = false;
  all_in = false;
  total_bet = 0;
  cards[0] = nullptr;
  cards[1] = nullptr;
  score = 0;
  reset_for_round();
}

void player::reset_for_round(){
  round_bet = 0;
  acted = false;
}
