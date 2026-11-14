// Test harness for the table: a fake sender that records every JSON message
// per client, and a fake clock the tests move forward by hand.
#ifndef _TABLE_HARNESS_H_
#define _TABLE_HARNESS_H_

#include <map>
#include <string>
#include <vector>
#include "table.h"

namespace {

class fake_sender: public sender{
  public:
    std::map<int, std::vector<json>> got;
    void send_line(int client, const std::string& line) override{
      got[client].push_back(json::parse(line));
    }
};

table_config quiet_config(int turn_seconds = 30, int next_hand_seconds = 0){
  table_config c;
  c.log_events = false;
  c.turn_seconds = turn_seconds;
  c.next_hand_seconds = next_hand_seconds;
  return c;
}

class harness{
  public:
    fake_sender out;
    int64_t now = 0;
    table t;

    explicit harness(int players, table_config config = quiet_config())
      : t(&out, config, [this]{ return now; }){
      for(int i = 0; i < players; i++){
        join(client(i), "P" + std::to_string(i));
      }
    }

    static int client(int seat){ return 101 + seat; }

    void join(int c, const std::string& name, const std::string& token = ""){
      t.on_connect(c);
      json msg = {{"type", "join"}, {"name", name}};
      if(!token.empty()){
        msg["token"] = token;
      }
      send(c, msg);
    }

    std::string token_of(int c){ return last(c, "welcome").value("token", ""); }

    void send(int c, const json& msg){ t.on_message(c, msg.dump()); }
    void start(){ send(client(0), {{"type", "start"}}); }

    void act(int seat, const std::string& action, int amount = 0){
      json msg = {{"type", "action"}, {"action", action}};
      if(action == "raise"){
        msg["amount"] = amount;
      }
      send(client(seat), msg);
    }

    // Last message of a given type that client `c` received ({} if none).
    json last(int c, const std::string& type){
      std::vector<json>& msgs = out.got[c];
      for(auto it = msgs.rbegin(); it != msgs.rend(); ++it){
        if((*it)["type"] == type){
          return *it;
        }
      }
      return json();
    }

    bool got_event(int c, const std::string& text){
      for(const json& m : out.got[c]){
        if(m["type"] == "event" && m["text"] == text){
          return true;
        }
      }
      return false;
    }

    int total_chips(){
      int sum = t.get_pot();
      for(int i = 0; i < t.seat_count(); i++){
        sum += t.seat(i).stack;
      }
      return sum;
    }

    // Check or call for whoever is to act until the hand ends.
    void play_out(){
      for(int guard = 0; guard < 200 && t.get_stage() != WAITING; guard++){
        int s = t.get_turn();
        int to_call = t.get_current_bet() - t.seat(s).round_bet;
        act(s, to_call == 0 ? "check" : "call");
      }
    }
};

}  // namespace

#endif
