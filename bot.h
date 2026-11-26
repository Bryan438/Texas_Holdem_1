#ifndef _BOT_H_
#define _BOT_H_

#include <random>
#include <string>
#include <vector>
#include "third_party/json.hpp"
#include "card.h"

using json = nlohmann::json;

class rl_model;            // rl_policy.h
class opponent_tracker;    // opponent_stats.h

// How a bot plays. A bot decides only from the "state" message the server
// sends to its seat, so it sees exactly what a person in that seat would see.
enum class bot_style{
  RANDOM,   // any legal move: a baseline every other bot should beat
  CALLING,  // checks or calls, never folds or raises: a "calling station"
  RULES,    // starting-hand score preflop; made hand, draws and pot odds after
  EQUITY,   // simulates the rest of the hand to estimate its chance of winning,
            // then compares that with the price of calling
  RL,       // a neural network trained by playing the other styles (see rl_policy.h)
};

bool parse_bot_style(const std::string& name, bot_style& out);
const char* bot_style_name(bot_style style);
const char* bot_style_list();   // "random, calling, rules, ..." for help texts

struct bot_move{
  std::string action;   // fold, check, call or raise
  int amount = 0;       // raise only: the total bet for this round ("raise to")
  json to_message() const;
};

// The move for seat state["you"]. The state must hold "actions", i.e. it is
// that seat's turn; the move is always one of the allowed actions.
// `model` is the network an RL bot plays with (null: the one in use, see
// rl_current_model), so that different versions can sit at one table.
// `opponents` is what this bot has learned about the others from earlier
// hands (null: nothing; every opponent looks like an average player). Only
// the RL style reads it.
bot_move decide(const json& state, bot_style style, std::mt19937& rng, const rl_model* model = nullptr,
                const opponent_tracker* opponents = nullptr);

// ---------------------------------------------------------------- building blocks
// Exposed for the tests and for the learned bot.

// An opponent still in the hand.
struct opponent_view{
  std::string name;
  int bet = 0;             // put in this round
  int total_bet = 0;       // and this hand
};

// The parts of a state message a bot decides from.
struct bot_view{
  std::string stage;
  int pot = 0;
  int current_bet = 0;     // the bet everyone has to match this round
  int big_blind = 2;
  int to_call = 0;         // chips needed to call (0: can check)
  bool can_check = false;
  bool can_raise = false;
  int raise_min = 0, raise_max = 0;
  int opponents = 0;       // others still in the hand
  int opponents_all_in = 0;
  int opponent_stack = 0;  // the biggest stack among them
  int stack = 0;           // our chips left
  int round_bet = 0;       // what we have put in this round
  int total_bet = 0;       // and this hand
  bool on_button = false, small_blind = false, is_big_blind = false;
  std::vector<Card> hole, board;
  std::vector<opponent_view> in_hand;   // the opponents still in, in turn order after us
};

bot_view read_bot_view(const json& state);

// Moves that are always allowed. A raise is kept inside the allowed range
// (the top of it is all in) and becomes a call when raising is not allowed.
bot_move check_or_call(const bot_view& v);
bot_move check_or_fold(const bot_view& v);
bot_move raise_to(const bot_view& v, int to);

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
