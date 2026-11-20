#ifndef _PLAYER_H_
#define _PLAYER_H_

#include <cstdint>
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
  bool bot = false;         // said so when joining (thbot does); people do not
  bool house = false;       // a bot the server runs itself (added from a page): no connection
  std::string style;        // a house bot's style: "rl", "equity", ...
  int buy_in = 0;           // chips at the start of this game, to tell who is up or down
  int64_t gone_since = 0;   // when the connection dropped (table clock); used while !connected
  bool leaving = false;     // left (or was removed) during a hand: the seat goes when it is over

  // Disconnected during a hand: the cards are held until this time (table
  // clock, ms), then folded unless the player is back. 0 = not waiting.
  int64_t away_until = 0;

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
