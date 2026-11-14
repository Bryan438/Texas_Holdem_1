#ifndef _STATS_H_
#define _STATS_H_

#include <map>
#include <string>
#include <vector>
#include "third_party/json.hpp"

using json = nlohmann::json;

// Per-player numbers over a set of hand records (see hand history in the README).
struct player_stats{
  std::string name;
  int hands = 0;           // hands dealt in
  int net = 0;             // chips won minus chips lost
  int vpip = 0;            // hands where they put money in preflop by choice (call/raise, not blinds)
  int pfr = 0;             // hands where they raised preflop
  int hands_won = 0;       // hands where they won chips
  int showdowns = 0;       // hands that reached showdown with them still in
  int showdowns_won = 0;
  int biggest_win = 0;     // largest amount won in one hand
};

// Keyed by player name, so a player keeps their stats across seats and restarts.
std::map<std::string, player_stats> compute_stats(const std::vector<json>& hands);

// "" if the record adds up (chips put in == pot == chips paid out, and every
// player's start stack - put in + won == end stack); otherwise what is wrong.
std::string check_hand(const json& hand);

std::string format_stats(const std::map<std::string, player_stats>& stats);

#endif
