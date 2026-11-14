#include <algorithm>
#include <cstdio>
#include <set>
#include <sstream>
#include "stats.h"

std::map<std::string, player_stats> compute_stats(const std::vector<json>& hands){
  std::map<std::string, player_stats> stats;
  for(const json& hand : hands){
    std::map<int, std::string> name_of;
    for(const json& p : hand.value("players", json::array())){
      std::string name = p.value("name", "?");
      name_of[p.value("seat", -1)] = name;
      player_stats& s = stats[name];
      s.name = name;
      s.hands++;
      s.net += p.value("net", 0);
    }

    std::set<int> vpip, pfr;
    for(const json& a : hand.value("actions", json::array())){
      if(a.value("street", "") != "preflop"){
        continue;
      }
      std::string action = a.value("action", "");
      int seat = a.value("seat", -1);
      if(action == "call" || action == "raise" || action == "bet"){
        vpip.insert(seat);
      }
      if(action == "raise" || action == "bet"){
        pfr.insert(seat);
      }
    }
    for(int seat : vpip) stats[name_of[seat]].vpip++;
    for(int seat : pfr) stats[name_of[seat]].pfr++;

    std::set<int> showdown;
    for(const json& s : hand.value("showdown", json::array())){
      showdown.insert(s.value("seat", -1));
      stats[name_of[s.value("seat", -1)]].showdowns++;
    }
    for(const json& w : hand.value("winners", json::array())){
      int seat = w.value("seat", -1);
      player_stats& s = stats[name_of[seat]];
      s.hands_won++;
      s.biggest_win = std::max(s.biggest_win, w.value("won", 0));
      if(showdown.count(seat)){
        s.showdowns_won++;
      }
    }
  }
  return stats;
}

std::string check_hand(const json& hand){
  std::map<int, int> put_in, won;
  int total_in = 0, total_out = 0;
  for(const json& a : hand.value("actions", json::array())){
    put_in[a.value("seat", -1)] += a.value("amount", 0);
    total_in += a.value("amount", 0);
  }
  for(const json& w : hand.value("winners", json::array())){
    won[w.value("seat", -1)] += w.value("won", 0);
    total_out += w.value("won", 0);
  }
  std::ostringstream problem;
  int pot = hand.value("pot", -1);
  if(total_in != pot){
    problem << "chips put in (" << total_in << ") != pot (" << pot << "); ";
  }
  if(total_out != pot){
    problem << "chips paid out (" << total_out << ") != pot (" << pot << "); ";
  }
  for(const json& p : hand.value("players", json::array())){
    int seat = p.value("seat", -1);
    int start = p.value("stack", 0), end = p.value("end_stack", 0);
    if(start - put_in[seat] + won[seat] != end){
      problem << p.value("name", "?") << ": " << start << " - " << put_in[seat] << " + " << won[seat]
              << " != " << end << "; ";
    }
    if(p.value("net", 0) != end - start){
      problem << p.value("name", "?") << ": net " << p.value("net", 0) << " != " << end - start << "; ";
    }
  }
  return problem.str();
}

static std::string percent(int part, int whole){
  if(whole == 0) return "-";
  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", (int)(100.0 * part / whole + 0.5));
  return buf;
}

std::string format_stats(const std::map<std::string, player_stats>& stats){
  std::vector<player_stats> rows;
  for(const auto& kv : stats) rows.push_back(kv.second);
  std::sort(rows.begin(), rows.end(), [](const player_stats& a, const player_stats& b){
    return a.net != b.net ? a.net > b.net : a.name < b.name;
  });

  std::ostringstream out;
  char line[200];
  snprintf(line, sizeof(line), "%-16s %6s %8s %6s %6s %6s %10s %8s\n",
           "Player", "Hands", "Net", "VPIP", "PFR", "Won", "Showdowns", "Best");
  out << line;
  for(const player_stats& s : rows){
    std::string sd = std::to_string(s.showdowns_won) + "/" + std::to_string(s.showdowns);
    snprintf(line, sizeof(line), "%-16.16s %6d %+8d %6s %6s %6s %10s %8d\n",
             s.name.c_str(), s.hands, s.net, percent(s.vpip, s.hands).c_str(),
             percent(s.pfr, s.hands).c_str(), percent(s.hands_won, s.hands).c_str(), sd.c_str(), s.biggest_win);
    out << line;
  }
  return out.str();
}
