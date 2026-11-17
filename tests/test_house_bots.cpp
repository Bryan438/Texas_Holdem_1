// Bots that a person adds from the web page: the server seats them and plays
// them itself, with no program to start and no connection.
#include <string>
#include "test_framework.h"
#include "table_harness.h"
#include "stats.h"

namespace {

table_config house_config(int bot_delay_ms = 1000, int next_hand_seconds = 0){
  table_config c = quiet_config(0, next_hand_seconds);
  c.bot_delay_ms = bot_delay_ms;
  c.seed = 7;
  return c;
}

// One person (client 101, seat 0), and whatever bots they add.
struct house: harness{
  explicit house(table_config c = house_config()) : harness(1, c){}

  void add(const std::string& style, int count = 1){
    send(client(0), {{"type", "add_bot"}, {"style", style}, {"count", count}});
  }
  json state(){ return last(client(0), "state"); }
  std::string error_code(){ return last(client(0), "error").value("code", ""); }

  // Play until the hand is over: the person checks or calls, time runs for the bots.
  void play_hand(){
    for(int guard = 0; guard < 400 && t.get_stage() != WAITING; guard++){
      if(t.get_turn() == 0){
        act(0, t.get_current_bet() - t.seat(0).round_bet == 0 ? "check" : "call");
      }
      else{
        now += 500;
        t.tick();
      }
    }
  }
};

}

TEST(a_person_adds_bots_that_the_server_plays){
  house h;
  h.add("rl", 2);
  h.add("equity");
  CHECK_EQ(h.t.seat_count(), 4);
  json st = h.state();
  CHECK(st["seats"][1]["name"] == "RL 1" && st["seats"][2]["name"] == "RL 2" && st["seats"][3]["name"] == "Equity 1");
  for(int i = 1; i <= 3; i++){
    CHECK(st["seats"][i].value("bot", false) && st["seats"][i].value("house", false));
    CHECK_EQ(st["seats"][i].value("stack", 0), 200);
    CHECK(st["seats"][i]["status"] == "ready");
  }
  CHECK(st["seats"][3]["style"] == "equity");
  CHECK(!st["seats"][0].contains("house"));
  CHECK(st.value("solo", false));                 // still alone with bots: may end the game
  json added = h.last(harness::client(0), "event");
  CHECK(added.value("kind", "") == "bot_added" && added.value("name", "") == "Equity 1");
  CHECK(added.value("by", "") == "P0" && added.value("style", "") == "equity");
  CHECK(added.value("text", "") == "P0 added the bot Equity 1");
}

TEST(house_bots_wait_their_delay_then_play_whole_hands){
  house h;
  h.add("rl");
  h.add("equity");
  h.add("rules");
  int hands_seen = 0;
  for(int hand = 0; hand < 25; hand++){
    if(h.t.get_stage() == WAITING){
      h.start();
      hands_seen++;
    }
    // A bot that is to act does not move before its delay.
    if(h.t.get_turn() != 0 && h.t.get_stage() != WAITING){
      int s = h.t.get_turn();
      h.now += 999;
      h.t.tick();
      CHECK_EQ(h.t.get_turn(), s);
      h.now += 1;
      h.t.tick();
      CHECK(h.t.get_turn() != s || h.t.get_stage() == WAITING);
    }
    h.play_hand();
    CHECK(h.t.get_stage() == WAITING);
    CHECK_EQ(h.total_chips(), 800);
    CHECK(check_hand(h.t.last_hand()) == "");
  }
  CHECK_EQ(hands_seen, 25);
  CHECK_EQ(h.t.hand_number(), 25);
  // Nobody sent anything illegal: the person only got errors if something broke.
  CHECK(h.last(harness::client(0), "error").is_null());
}

TEST(adding_during_a_hand_seats_the_bot_for_the_next_one){
  house h;
  h.add("calling");
  h.start();
  h.add("random");
  CHECK_EQ(h.t.seat_count(), 3);
  CHECK(!h.t.seat(2).dealt);
  CHECK(h.state()["seats"][2]["status"] == "sitting_out");
  CHECK(h.last(harness::client(0), "event").value("next_hand", false));
  h.play_hand();
  h.start();
  CHECK(h.t.seat(2).dealt);
}

TEST(add_bot_refuses_what_it_cannot_do){
  house h;
  h.add("magic");
  CHECK(h.error_code() == "bad_style");
  CHECK_EQ(h.t.seat_count(), 1);
  h.add("rl", 50);                                // more than fit: as many as fit
  CHECK_EQ(h.t.seat_count(), MAX_PLAYERS);
  h.add("rl");
  CHECK(h.error_code() == "table_full");
  // A bot (here: one that joined over the network, like thbot) cannot add or remove bots.
  house g;
  g.t.on_connect(500);
  g.send(500, {{"type", "join"}, {"name", "Robo"}, {"bot", true}});
  g.send(500, {{"type", "add_bot"}});
  CHECK(g.last(500, "error").value("code", "") == "bots_cannot");
  g.send(500, {{"type", "remove_bot"}, {"seat", 0}});
  CHECK(g.last(500, "error").value("code", "") == "bots_cannot");
  CHECK_EQ(g.t.seat_count(), 2);
}

TEST(remove_bot_between_hands_only_and_only_house_bots){
  house h;
  h.add("rl", 3);
  h.t.on_connect(500);
  h.send(500, {{"type", "join"}, {"name", "Robo"}, {"bot", true}});   // a networked bot, seat 4
  h.start();
  h.send(harness::client(0), {{"type", "remove_bot"}, {"seat", 1}});
  CHECK(h.error_code() == "hand_running");
  h.send(harness::client(0), {{"type", "end_game"}});       // not alone with bots? Robo is a bot too
  CHECK(h.t.get_stage() == WAITING);

  h.send(harness::client(0), {{"type", "remove_bot"}, {"seat", 0}});     // a person
  CHECK(h.error_code() == "not_house_bot");
  h.send(harness::client(0), {{"type", "remove_bot"}, {"seat", 4}});     // a networked bot
  CHECK(h.error_code() == "not_house_bot");
  h.send(harness::client(0), {{"type", "remove_bot"}, {"seat", 9}});
  CHECK(h.error_code() == "not_house_bot");

  h.send(harness::client(0), {{"type", "remove_bot"}, {"seat", 2}});
  CHECK_EQ(h.t.seat_count(), 4);
  json st = h.state();
  CHECK(st["seats"][1]["name"] == "RL 1" && st["seats"][2]["name"] == "RL 3" && st["seats"][3]["name"] == "Robo");
  for(int i = 0; i < 4; i++) CHECK_EQ(st["seats"][i].value("seat", -1), i);
  CHECK_EQ(st.value("you", -1), 0);
  CHECK_EQ(h.t.seat_of(500), 3);                  // the networked bot's messages still reach its seat
  json gone = h.last(harness::client(0), "event");
  CHECK(gone.value("kind", "") == "bot_removed" && gone.value("name", "") == "RL 2");
  // The freed name is used again.
  h.add("rl");
  CHECK(h.state()["seats"][4]["name"] == "RL 2");
}

TEST(removing_the_dealer_passes_the_button_on){
  house h;
  h.add("calling", 3);
  // Find a hand where a bot has the button.
  for(int i = 0; i < 4 && (h.t.get_dealer() < 1); i++){
    h.start();
    h.play_hand();
  }
  int d = h.t.get_dealer();
  CHECK(d >= 1);
  std::string after = h.t.seat((d + 1) % 4).name;
  h.send(harness::client(0), {{"type", "remove_bot"}, {"seat", d}});
  h.start();
  CHECK(h.t.seat(h.t.get_dealer()).name == after);
}

TEST(house_bots_do_not_play_on_when_every_person_has_left){
  house h(house_config(1000, 5));                 // the next hand would start by itself after 5 s
  h.add("calling", 2);
  h.start();
  h.play_hand();
  h.t.on_disconnect(harness::client(0));
  h.now += 60000;
  h.t.tick();
  CHECK(h.t.get_stage() == WAITING);
  CHECK_EQ(h.t.hand_number(), 1);
}

TEST(new_game_keeps_the_bots_and_gives_them_fresh_chips){
  house h;
  h.add("rl", 2);
  for(int i = 0; i < 3; i++){
    h.start();
    h.play_hand();
  }
  h.send(harness::client(0), {{"type", "new_game"}});
  CHECK_EQ(h.t.seat_count(), 3);
  for(int i = 0; i < 3; i++) CHECK_EQ(h.t.seat(i).stack, 200);
  CHECK(h.t.seat(1).house && h.t.seat(2).house);
}
