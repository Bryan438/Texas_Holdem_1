#include <algorithm>
#include <set>
#include "opponent_stats.h"

// An average player, which a stranger is assumed to be until seen: the rates
// start here and move towards what the player does as hands are seen.
static const float PRIOR[4] = {0.40f, 0.15f, 0.35f, 0.40f};
// How many chances to act a prior is worth.
static const double PRIOR_WEIGHT = 5;
// Players remembered at once; a long-running table forgets everyone past this.
static const size_t MAX_PLAYERS_KEPT = 256;

opponent_tracker::opponent_tracker(double m): memory(std::max(0.0, std::min(m, 1.0))){}

void opponent_tracker::add_hand(const json& hand){
  if(!hand.is_object() || !hand.contains("players") || !hand["players"].is_array()) return;
  if(players.size() > MAX_PLAYERS_KEPT) players.clear();

  std::set<std::string> dealt;
  for(const json& p : hand["players"]){
    if(!p.is_object() || !p.contains("name") || !p["name"].is_string()) continue;
    std::string name = p["name"].get<std::string>();
    if(!dealt.insert(name).second) continue;
    counts& c = players[name];
    for(double* x : {&c.hands, &c.vpip, &c.pfr, &c.aggressive, &c.passive, &c.faced, &c.folded}){
      *x *= memory;
    }
    c.hands += 1;
  }

  std::set<std::string> put_in, raised;
  std::string street, bettor;   // who last bet or raised on this street after the flop
  for(const json& a : hand.value("actions", json::array())){
    if(!a.is_object() || !a.contains("name") || !a["name"].is_string()) continue;
    std::string name = a["name"].get<std::string>();
    std::string action = a.value("action", ""), on = a.value("street", "");
    if(on != street){
      street = on;
      bettor.clear();
    }
    // Blinds are forced; timeouts and disconnects were nobody's choice.
    if(action == "small_blind" || action == "big_blind" || a.contains("reason") || !dealt.count(name)) continue;
    counts& c = players[name];
    bool aggressive = action == "bet" || action == "raise";
    if(street == "preflop"){
      if(aggressive || action == "call") put_in.insert(name);
      if(aggressive) raised.insert(name);
      continue;
    }
    if(aggressive) c.aggressive += 1;
    if(action == "call") c.passive += 1;
    if(!bettor.empty() && bettor != name && (action == "fold" || action == "call" || aggressive)){
      c.faced += 1;
      c.folded += action == "fold";
    }
    if(aggressive) bettor = name;
  }
  for(const std::string& name : put_in) players[name].vpip += 1;
  for(const std::string& name : raised) players[name].pfr += 1;
}

void opponent_tracker::describe(const std::string& name, float out[OPPONENT_FEATURES]) const{
  counts c;
  auto found = players.find(name);
  if(found != players.end()) c = found->second;
  auto rate = [](double part, double of, float prior){
    return (float)((part + PRIOR_WEIGHT * prior) / (of + PRIOR_WEIGHT));
  };
  out[0] = rate(c.vpip, c.hands, PRIOR[0]);
  out[1] = rate(c.pfr, c.hands, PRIOR[1]);
  out[2] = rate(c.aggressive, c.aggressive + c.passive, PRIOR[2]);
  out[3] = rate(c.folded, c.faced, PRIOR[3]);
  out[4] = (float)(c.hands / (c.hands + 20));
}

double opponent_tracker::hands_seen(const std::string& name) const{
  auto found = players.find(name);
  return found == players.end() ? 0 : found->second.hands;
}
