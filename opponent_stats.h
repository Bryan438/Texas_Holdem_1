#ifndef _OPPONENT_STATS_H_
#define _OPPONENT_STATS_H_

#include <map>
#include <string>
#include "third_party/json.hpp"

using json = nlohmann::json;

// What a player at the table can learn about the others by watching: how
// often each one puts money in, raises, bets, and gives up when bet into.
// It reads finished hands (the hand_summary every seat is sent, or a hand
// record) and uses only the actions, which everyone saw; never hole cards.
//
// The learned bot reads these numbers to tell a maniac from a rock.

const int OPPONENT_FEATURES = 5;   // numbers per opponent (see opponent_tracker::describe)

class opponent_tracker{
  public:
    // memory: how much an old hand still counts after each new one (0.99:
    // a hand 100 hands ago counts about a third), so a player who changes
    // style is noticed. 1 never forgets.
    explicit opponent_tracker(double memory = 0.99);

    void add_hand(const json& hand);
    void clear(){ players.clear(); }

    // Five numbers between 0 and 1 for the player of that name:
    //   [0] VPIP: how often they put money in preflop by choice
    //   [1] PFR: how often they raise preflop
    //   [2] aggression: bets and raises after the flop, out of bets, raises and calls
    //   [3] fold to a bet: how often they fold after the flop when someone bet before them
    //   [4] how much has been seen: 0 for a stranger, 0.5 after 20 hands, towards 1
    // With few hands seen, [0] to [3] stay near those of an average player:
    // 3 hands of 100% raises say little.
    void describe(const std::string& name, float out[OPPONENT_FEATURES]) const;
    // Hands seen of that player, older ones counting less (see memory).
    double hands_seen(const std::string& name) const;

  private:
    struct counts{
      double hands = 0, vpip = 0, pfr = 0;
      double aggressive = 0, passive = 0;   // after the flop: bets and raises; calls
      double faced = 0, folded = 0;         // after the flop: times bet into; folds then
    };
    std::map<std::string, counts> players;
    double memory;
};

#endif
