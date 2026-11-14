#ifndef _PLAYER_H_
#define _PLAYER_H_

#include <string>
#include "card.h"

// One seat at the table. Pure data: the table decides what happens,
// the network layer is never touched from here.
struct player{
  int socket = -1;          // -1 while disconnected
  std::string name;
  std::string token;        // secret given at join, needed to reconnect
  bool connected = true;
  int stack = 0;            // chips not yet in the pot

  // Per hand
  bool dealt = false;       // was dealt into the current/last hand
  bool in_hand = false;     // dealt and not folded
  bool all_in = false;
  int total_bet = 0;        // chips put in during the whole hand
  const Card* cards[2] = {nullptr, nullptr};
  int score = 0;            // hand score at showdown (hand_evaluator)

  // Per betting round
  int round_bet = 0;        // chips put in during this betting round
  bool acted = false;       // has acted since the last full raise

  // Move up to `amount` chips into the pot. Running out means all-in.
  int put_in(int amount);

  void reset_for_hand();
  void reset_for_round();
};

#endif
