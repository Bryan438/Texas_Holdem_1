// Who runs the table (the host), and chips that stay with a person for the
// whole game: leaving and coming back, buying in again, the standings.
#include <string>
#include "test_framework.h"
#include "table_harness.h"

namespace {

struct hosted: harness{
  explicit hosted(int players = 3, table_config config = quiet_config()) : harness(players, config){}
  json state(int c){ return last(c, "state"); }
  std::string error_code(int c){ return last(c, "error").value("code", ""); }
  bool got_kind(int c, const std::string& kind){
    for(const json& m : out.got[c]){
      if(m["type"] == "event" && m.value("kind", "") == kind) return true;
    }
    return false;
  }
  int seat_named(const std::string& name){
    for(int i = 0; i < t.seat_count(); i++){
      if(t.seat(i).name == name) return i;
    }
    return -1;
  }
  std::string host_name(int c){
    json st = state(c);
    int h = st.value("host", -1);
    return h < 0 ? "" : st["seats"][h].value("name", "");
  }
  void vote(int c, bool agree){ send(c, {{"type", "vote"}, {"agree", agree}}); }
  // Move chips from P1 to P0 between hands, as if P0 had won them.
  void p0_takes_from_p1(int amount){
    t.set_stack(0, t.seat(0).stack + amount);
    t.set_stack(1, t.seat(1).stack - amount);
  }
};

}

// ---------------------------------------------------------------- the host

TEST(the_first_person_is_the_host_and_only_they_deal){
  hosted h;
  CHECK(h.host_name(harness::client(2)) == "P0");
  CHECK(h.state(harness::client(0))["seats"][0].value("host", false) == false);   // told by "host", not per seat
  CHECK(h.got_kind(harness::client(0), "host"));
  h.send(harness::client(1), {{"type", "start"}});
  CHECK(h.error_code(harness::client(1)) == "not_host");
  CHECK(h.last(harness::client(1), "error").value("message", "").find("P0") != std::string::npos);
  CHECK(h.t.get_stage() == WAITING);
  h.send(harness::client(1), {{"type", "add_bot"}});
  CHECK(h.error_code(harness::client(1)) == "not_host");
  CHECK_EQ(h.t.seat_count(), 3);
  h.start();
  CHECK(h.t.get_stage() == PREFLOP);
}

TEST(a_bot_that_sits_down_first_is_not_the_host){
  hosted h(0);
  h.t.on_connect(201);
  h.send(201, {{"type", "join"}, {"name", "Robo"}, {"bot", true}});
  CHECK(h.state(201).value("host", 0) == -1);
  h.join(301, "Ann");
  CHECK(h.host_name(301) == "Ann");
}

TEST(with_only_bots_at_the_table_anyone_may_deal){
  hosted h(0);
  for(int c : {201, 202}){
    h.t.on_connect(c);
    h.send(c, {{"type", "join"}, {"name", "Robo" + std::to_string(c)}, {"bot", true}});
  }
  h.send(201, {{"type", "start"}});
  CHECK(h.t.get_stage() == PREFLOP);
}

TEST(the_host_hands_over_on_leaving_or_staying_away){
  table_config c = quiet_config();
  c.disconnect_grace_seconds = 30;
  hosted h(3, c);
  h.send(harness::client(0), {{"type", "leave"}});
  CHECK(h.host_name(harness::client(1)) == "P1");

  // A reload keeps the host; staying away hands over.
  h.t.on_disconnect(harness::client(1));
  h.now += 5000;
  h.t.tick();
  CHECK(h.host_name(harness::client(2)) == "P1");
  h.join(harness::client(1), "P1", h.token_of(harness::client(1)));
  CHECK(h.host_name(harness::client(2)) == "P1");
  h.t.on_disconnect(harness::client(1));
  h.now += 30000;
  h.t.tick();
  CHECK(h.host_name(harness::client(2)) == "P2");
  h.send(harness::client(2), {{"type", "start"}});
  CHECK(h.error_code(harness::client(2)) == "not_enough_players");   // allowed, but P1 is away
}

TEST(the_host_removes_a_player_or_a_bot_from_the_page){
  hosted h;
  h.send(harness::client(0), {{"type", "add_bot"}, {"style", "calling"}});
  int bot = h.seat_named("Calling 1");
  CHECK(bot >= 0);
  h.send(harness::client(1), {{"type", "kick"}, {"seat", 2}});
  CHECK(h.error_code(harness::client(1)) == "not_host");
  h.send(harness::client(0), {{"type", "kick"}, {"seat", 0}});
  CHECK(h.error_code(harness::client(0)) == "kick_self");
  h.send(harness::client(0), {{"type", "kick"}, {"seat", 9}});
  CHECK(h.error_code(harness::client(0)) == "bad_seat");

  // A bot between hands goes at once.
  h.send(harness::client(0), {{"type", "kick"}, {"seat", bot}});
  CHECK(h.seat_named("Calling 1") < 0);

  // A person in the middle of a hand folds, and goes when it is over.
  h.start();
  h.send(harness::client(0), {{"type", "kick"}, {"seat", 2}});
  json left = h.last(harness::client(2), "left");
  CHECK(left.value("reason", "") == "kicked");
  CHECK(left.value("by", "") == "P0");
  CHECK(h.got_event(harness::client(1), "P2 was removed by P0"));
  h.play_out();
  CHECK(h.t.get_stage() == WAITING);
  CHECK(h.seat_named("P2") < 0);
}

// ---------------------------------------------------------------- chips that stay with you

TEST(leaving_and_coming_back_brings_the_chips_back){
  hosted h;
  h.p0_takes_from_p1(150);                         // P1 is down to 50
  std::string token = h.token_of(harness::client(1));
  h.send(harness::client(1), {{"type", "leave"}});
  CHECK(h.seat_named("P1") < 0);

  // Somebody else cannot take the name, or the chips with it.
  h.join(401, "P1");
  CHECK(h.error_code(401) == "name_left");
  // A new name is a new player: a fresh buy-in, counted as such.
  h.join(402, "P1b");
  CHECK_EQ(h.t.seat(h.seat_named("P1b")).stack, 200);

  h.join(harness::client(1), "P1", token);
  int back = h.seat_named("P1");
  CHECK(back >= 0);
  CHECK_EQ(h.t.seat(back).stack, 50);
  CHECK(h.got_event(harness::client(0), "P1 is back with 50 chips"));
  CHECK(h.token_of(harness::client(1)) == token);
}

TEST(standings_count_those_who_left){
  hosted h;
  h.p0_takes_from_p1(150);
  h.send(harness::client(1), {{"type", "leave"}});
  h.send(harness::client(0), {{"type", "end_game"}});
  h.vote(harness::client(2), true);
  json standings = h.state(harness::client(0))["standings"];
  CHECK_EQ(standings.size(), 3);
  CHECK(standings[0].value("name", "") == "P0");
  CHECK_EQ(standings[0].value("net", 0), 150);
  json last = standings[2];
  CHECK(last.value("name", "") == "P1");
  CHECK(last.value("left", false) == true);
  CHECK_EQ(last.value("net", 0), -150);
  CHECK_EQ(last.value("stack", 0), 50);

  // A new game: everyone starts again, and the names are free.
  h.send(harness::client(0), {{"type", "new_game"}});
  h.join(401, "P1");
  CHECK_EQ(h.t.seat(h.seat_named("P1")).stack, 200);
}

TEST(a_disconnected_players_seat_given_away_keeps_their_chips){
  hosted h(12);                                    // a full table
  h.p0_takes_from_p1(120);
  std::string token = h.token_of(harness::client(1));
  h.t.on_disconnect(harness::client(1));
  h.join(401, "New");                              // takes the seat of the one who is gone
  CHECK(h.seat_named("P1") < 0);
  h.send(401, {{"type", "leave"}});
  h.join(harness::client(1), "P1", token);
  CHECK_EQ(h.t.seat(h.seat_named("P1")).stack, 80);
}

TEST(buying_in_again_only_when_out_of_chips_and_counted){
  hosted h(2);
  h.send(harness::client(1), {{"type", "rebuy"}});
  CHECK(h.error_code(harness::client(1)) == "has_chips");
  h.p0_takes_from_p1(200);
  h.send(harness::client(1), {{"type", "rebuy"}});
  CHECK_EQ(h.t.seat(1).stack, 200);
  CHECK_EQ(h.t.seat(1).buy_in, 400);
  CHECK_EQ(h.state(harness::client(0))["seats"][1].value("rebuys", 0), 1);
  CHECK(h.got_event(harness::client(0), "P1 buys in again for 200 chips"));
  h.send(harness::client(0), {{"type", "end_game"}});
  h.vote(harness::client(1), true);
  json standings = h.state(harness::client(0))["standings"];
  CHECK_EQ(standings[1].value("net", 0), -200);    // still 200 down: the rebuy is not a win
  h.send(harness::client(1), {{"type", "rebuy"}});
  CHECK(h.error_code(harness::client(1)) == "game_over");
}
