// Drives whole hands through table::on_message, the same entry point the
// network uses. A fake sender records every JSON message per client and a fake
// clock lets the tests move time forward for the turn timer.
#include <map>
#include <string>
#include <vector>
#include "test_framework.h"
#include "table.h"

namespace {

class fake_sender: public sender{
  public:
    std::map<int, std::vector<json>> got;
    void send_line(int client, const std::string& line) override{
      got[client].push_back(json::parse(line));
    }
};

table_config quiet_config(int turn_seconds = 30){
  table_config c;
  c.log_events = false;
  c.turn_seconds = turn_seconds;
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

    void join(int c, const std::string& name){
      t.on_connect(c);
      send(c, {{"type", "join"}, {"name", name}});
    }

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

}

TEST(table_join_welcome_and_errors){
  harness h(2);
  json welcome = h.last(harness::client(1), "welcome");
  CHECK_EQ(welcome["seat"].get<int>(), 1);
  json state = h.last(harness::client(0), "state");
  CHECK_EQ(state["seats"].size(), 2);
  CHECK(state["seats"][1]["name"] == "P1");
  CHECK(state["seats"][1]["status"] == "ready");
  CHECK_EQ(state["seats"][1]["stack"].get<int>(), 200);

  h.join(300, "P0");                                  // name in use
  CHECK(h.last(300, "error")["message"].get<std::string>().find("taken") != std::string::npos);

  h.t.on_message(301, "not json");                    // garbage
  CHECK(!h.last(301, "error").is_null());

  h.send(302, {{"type", "start"}});                   // not seated
  CHECK(h.last(302, "error")["message"] == "join the table first");
  CHECK_EQ(h.t.get_stage(), WAITING);
}

TEST(table_hole_cards_are_private){
  harness h(3);
  h.start();
  json st0 = h.last(harness::client(0), "state");
  CHECK(st0["seats"][0]["cards"].is_array());
  CHECK_EQ(st0["seats"][0]["cards"].size(), 2);
  CHECK(st0["seats"][1]["cards"].is_null());
  CHECK(st0["seats"][2]["cards"].is_null());
  json st1 = h.last(harness::client(1), "state");
  CHECK(st1["seats"][0]["cards"].is_null());
  CHECK(st1["seats"][1]["cards"].is_array());
}

// The pot and bets must reach every player after every action, not only after raises.
TEST(table_every_action_updates_everyone){
  harness h(3);
  h.start();                       // dealer 0, SB 1, BB 2, seat 0 first
  CHECK_EQ(h.t.get_turn(), 0);
  CHECK_EQ(h.last(harness::client(2), "state")["pot"].get<int>(), 3);

  h.act(0, "raise", 20);
  for(int s = 0; s < 3; s++){
    json st = h.last(harness::client(s), "state");
    CHECK_EQ(st["pot"].get<int>(), 23);
    CHECK_EQ(st["seats"][0]["bet"].get<int>(), 20);
    CHECK_EQ(st["turn"].get<int>(), 1);
  }

  h.act(1, "call");
  for(int s = 0; s < 3; s++){
    json st = h.last(harness::client(s), "state");
    CHECK_EQ(st["pot"].get<int>(), 42);
    CHECK_EQ(st["seats"][1]["bet"].get<int>(), 20);
    CHECK_EQ(st["seats"][1]["stack"].get<int>(), 180);
    CHECK(h.got_event(harness::client(s), "P1 calls 19"));
  }

  // Only the player to act gets the list of allowed actions.
  json to_act = h.last(harness::client(2), "state");
  CHECK_EQ(to_act["actions"]["call"].get<int>(), 18);
  CHECK_EQ(to_act["actions"]["raise"]["min"].get<int>(), 38);
  CHECK_EQ(to_act["actions"]["raise"]["max"].get<int>(), 200);
  CHECK(!h.last(harness::client(0), "state").contains("actions"));
}

TEST(table_everyone_folds_to_big_blind){
  harness h(3);
  h.start();
  h.act(0, "fold");
  h.act(1, "fold");
  CHECK_EQ(h.t.get_stage(), WAITING);
  CHECK_EQ(h.t.seat(0).stack, 200);
  CHECK_EQ(h.t.seat(1).stack, 199);
  CHECK_EQ(h.t.seat(2).stack, 201);
  CHECK_EQ(h.total_chips(), 600);

  json st = h.last(harness::client(0), "state");
  CHECK_EQ(st["result"].size(), 1);
  CHECK_EQ(st["result"][0]["seat"].get<int>(), 2);
  CHECK_EQ(st["result"][0]["amount"].get<int>(), 3);
  CHECK(st["seats"][2]["cards"].is_null());  // no showdown: cards stay hidden
  CHECK_EQ(st["seats"][2]["bet"].get<int>(), 0);  // no bets left on the table between hands
}

TEST(table_rejects_invalid_actions){
  harness h(3);
  h.start();
  int c0 = harness::client(0);

  h.act(1, "fold");
  CHECK(h.last(harness::client(1), "error")["message"] == "it is not your turn");
  CHECK(h.t.seat(1).in_hand);

  h.act(0, "check");
  CHECK(h.last(c0, "error")["message"] == "cannot check: 2 to call");
  h.act(0, "raise", 3);                       // min raise is to 4
  CHECK(h.last(c0, "error")["message"] == "raise must be to between 4 and 200");
  h.act(0, "raise", 201);
  CHECK(h.last(c0, "error")["message"] == "raise must be to between 4 and 200");
  h.act(0, "dance");
  CHECK(!h.last(c0, "error").is_null());
  CHECK_EQ(h.t.seat(0).stack, 200);
  CHECK_EQ(h.t.get_turn(), 0);

  h.act(0, "call");
  CHECK_EQ(h.t.seat(0).stack, 198);
  CHECK_EQ(h.t.get_turn(), 1);
}

TEST(table_minimum_raise_follows_last_raise){
  harness h(3);
  h.start();
  h.act(0, "raise", 6);                       // raises by 4
  CHECK_EQ(h.t.get_min_raise_to(), 10);
  h.act(1, "raise", 8);                       // too small
  CHECK_EQ(h.t.get_turn(), 1);
  h.act(1, "raise", 10);
  CHECK_EQ(h.t.get_current_bet(), 10);
  CHECK_EQ(h.t.get_min_raise_to(), 14);
}

TEST(table_short_all_in_does_not_reopen_betting){
  harness h(3);
  h.t.set_stack(1, 12);
  h.start();                                  // dealer 0, SB 1 (12 chips), BB 2
  h.act(0, "raise", 10);                      // full raise by 8
  h.act(1, "raise", 12);                      // all-in for only 2 more: not a full raise
  CHECK_EQ(h.t.get_current_bet(), 12);
  CHECK_EQ(h.t.get_min_raise_to(), 20);       // still based on the raise of 8
  h.act(2, "call");
  // Seat 0 must call the extra 2 but has already acted, so it can still act.
  CHECK_EQ(h.t.get_turn(), 0);
  h.act(0, "call");
  CHECK_EQ(h.t.get_stage(), FLOP);
}

TEST(table_heads_up_check_down_shows_cards){
  harness h(2);
  h.start();
  CHECK_EQ(h.t.get_dealer(), 0);             // heads-up: dealer posts small blind, acts first
  CHECK_EQ(h.t.seat(0).stack, 199);
  CHECK_EQ(h.t.get_turn(), 0);
  h.act(0, "call");
  h.act(1, "check");
  CHECK_EQ(h.t.get_stage(), FLOP);
  CHECK_EQ(h.t.get_turn(), 1);               // after the flop the big blind acts first
  json st = h.last(harness::client(1), "state");
  CHECK(st["your_hand"].is_string());
  CHECK_EQ(st["board"].size(), 3);

  h.play_out();
  CHECK_EQ(h.t.get_stage(), WAITING);
  CHECK_EQ(h.t.board_size(), 5);
  CHECK_EQ(h.total_chips(), 400);
  json end = h.last(harness::client(0), "state");
  CHECK(end["seats"][0]["cards"].is_array());
  CHECK(end["seats"][1]["cards"].is_array());  // showdown reveals both hands
  CHECK(end["seats"][1]["hand"].is_string());
  CHECK(end["result"].size() >= 1);
}

TEST(table_button_moves_and_hands_repeat){
  harness h(3);
  for(int hand = 0; hand < 6; hand++){
    h.start();
    CHECK_EQ(h.t.get_dealer(), hand % 3);
    h.play_out();
    CHECK_EQ(h.t.get_stage(), WAITING);
    CHECK_EQ(h.total_chips(), 600);
  }
  CHECK_EQ(h.t.hand_number(), 6);
}

TEST(table_all_in_side_pot_is_paid_correctly){
  harness h(3);
  h.t.set_stack(0, 50);
  h.t.set_stack(1, 20);
  h.t.set_stack(2, 100);
  h.start();
  h.act(0, "raise", 50);                      // all-in
  h.act(1, "call");                           // all-in for 20
  h.act(2, "call");
  // Nobody can bet any more: the board is dealt out automatically.
  CHECK_EQ(h.t.get_stage(), WAITING);
  CHECK_EQ(h.t.board_size(), 5);
  CHECK_EQ(h.total_chips(), 170);
  CHECK(h.t.seat(1).stack <= 60);             // the short stack can win only the main pot
  CHECK(h.t.seat(2).stack >= 50);             // seat 2 only ever risked 50
}

TEST(table_turn_timer_checks_or_folds){
  harness h(3, quiet_config(10));
  h.start();
  CHECK_EQ(h.t.get_turn(), 0);
  CHECK_EQ(h.last(harness::client(0), "state")["turn_time_left"].get<int>(), 10);

  h.now += 9999;
  h.t.tick();
  CHECK_EQ(h.t.get_turn(), 0);                // not yet
  h.now += 1;
  h.t.tick();
  CHECK(!h.t.seat(0).in_hand);                // facing the big blind: folded
  CHECK_EQ(h.t.get_turn(), 1);

  h.act(1, "call");
  CHECK_EQ(h.t.get_turn(), 2);                // big blind can check
  h.now += 10000;
  h.t.tick();
  CHECK(h.t.seat(2).in_hand);                 // timed out but checked, not folded
  CHECK_EQ(h.t.get_stage(), FLOP);
}

TEST(table_turn_timer_off_when_zero){
  harness h(2, quiet_config(0));
  h.start();
  h.now += 1000000;
  h.t.tick();
  CHECK_EQ(h.t.get_turn(), 0);
  CHECK(h.last(harness::client(0), "state")["turn_time_left"].is_null());
}

TEST(table_disconnect_on_turn_folds_and_moves_on){
  harness h(3);
  h.start();
  CHECK_EQ(h.t.get_turn(), 0);
  h.t.on_disconnect(harness::client(0));
  CHECK(!h.t.seat(0).in_hand);
  CHECK_EQ(h.t.get_turn(), 1);
  json st = h.last(harness::client(1), "state");
  CHECK(st["seats"][0]["status"] == "disconnected");
  CHECK(h.got_event(harness::client(2), "P0 disconnected"));
}

TEST(table_disconnect_and_reconnect_by_name){
  harness h(2);
  h.start();                                  // seat 0 to act
  h.t.on_disconnect(harness::client(1));      // the other player leaves
  CHECK_EQ(h.t.get_stage(), WAITING);         // seat 0 wins at once
  CHECK_EQ(h.t.seat(0).stack, 202);
  CHECK_EQ(h.t.seat(1).stack, 198);

  h.send(harness::client(0), {{"type", "start"}});
  CHECK_EQ(h.t.get_stage(), WAITING);         // one player is not enough
  CHECK(!h.last(harness::client(0), "error").is_null());

  h.join(500, "P1");                          // comes back on a new connection
  CHECK_EQ(h.last(500, "welcome")["seat"].get<int>(), 1);
  CHECK_EQ(h.t.seat(1).stack, 198);           // chips kept
  CHECK_EQ(h.t.seat_count(), 2);
  h.send(500, {{"type", "start"}});
  CHECK_EQ(h.t.get_stage(), PREFLOP);
}

TEST(table_late_joiner_sits_out){
  harness h(2);
  h.start();
  h.join(600, "Late");
  CHECK_EQ(h.t.seat_count(), 3);
  CHECK(!h.t.seat(2).dealt);
  CHECK(h.last(600, "state")["seats"][2]["status"] == "sitting_out");
  h.play_out();
  CHECK_EQ(h.t.seat(2).stack, 200);
  h.start();
  CHECK(h.t.seat(2).dealt);
  h.play_out();
  CHECK_EQ(h.total_chips(), 600);
}

TEST(table_busted_player_sits_out){
  harness h(3);
  h.t.set_stack(1, 0);
  CHECK(h.last(harness::client(0), "state")["seats"].size() == 3);
  h.start();
  CHECK(!h.t.seat(1).dealt);
  CHECK(h.t.seat(0).dealt);
  CHECK(h.t.seat(2).dealt);
  h.play_out();
  CHECK_EQ(h.total_chips(), 400);
}

TEST(table_cannot_start_twice){
  harness h(2);
  h.start();
  h.start();
  CHECK(h.last(harness::client(0), "error")["message"] == "a hand is already running");
  CHECK_EQ(h.t.hand_number(), 1);
}
