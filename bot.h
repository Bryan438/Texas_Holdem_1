#ifndef _BOT_H_
#define _BOT_H_

#include <random>
#include <string>
#include <vector>
#include "third_party/json.hpp"
#include "card.h"

using json = nlohmann::json;

// How a bot plays. A bot decides only from the "state" message the server
// sends to its seat, so it sees exactly what a person in that seat would see.
enum class bot_style{
  RANDOM,   // any legal move: a baseline every other bot should beat
  CALLING,  // checks or calls, never folds or raises: a "calling station"
  RULES,    // starting-hand score preflop; made hand, draws and pot odds after
  EQUITY,   // simulates the rest of the hand to estimate its chance of winning,
            // then compares that with the price of calling
};

bool parse_bot_style(const std::string& name, bot_style& out);
const char* bot_style_name(bot_style style);
const char* bot_style_list();   // "random, calling, rules" for help texts

struct bot_move{
  std::string action;   // fold, check, call or raise
  int amount = 0;       // raise only: the total bet for this round ("raise to")
  json to_message() const;
};

// The move for seat state["you"]. The state must hold "actions", i.e. it is
// that seat's turn; the move is always one of the allowed actions.
bot_move decide(const json& state, bot_style style, std::mt19937& rng);

// ---------------------------------------------------------------- building blocks
// Exposed for the tests.

// Chen formula: a quick score for two hole cards, from -1 (7-2 offsuit) to 20 (aces).
int chen_score(const Card& a, const Card& b);

enum class hand_strength{
  AIR,      // nothing worth paying for
  DRAW,     // not made yet, but has outs to a flush or straight
  PAIR,     // a pair below the top card on the board
  STRONG,   // top pair, an overpair, or two pair made with both hole cards
  MONSTER,  // a set or trips, or a straight or better that uses a hole card
};

struct hand_read{
  hand_strength strength = hand_strength::AIR;
  int outs = 0;   // cards that complete a flush or straight draw
};

// What the hole cards are worth on this board (3 to 5 cards).
hand_read read_hand(const std::vector<Card>& hole, const std::vector<Card>& board);

const char* hand_strength_name(hand_strength s);

#endif
