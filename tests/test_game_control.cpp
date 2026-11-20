// Two things a player at a browser runs into: a connection that drops for a
// moment in the middle of a hand, and wanting to stop or restart a game played
// against bots only.
#include <string>
#include "test_framework.h"
#include "table_harness.h"
#include "stats.h"

namespace {

table_config with_grace(int seconds, int turn_seconds = 0){
  table_config c = quiet_config(turn_seconds);
  c.disconnect_grace_seconds = seconds;
  return c;
}

// A table with one person (client 101, seat 0) and `bots` bots after them.
struct solo_table: harness{
  explicit solo_table(int bots, table_config config = quiet_config()) : harness(0, config){
    join(client(0), "Bryan");
    for(int i = 1; i <= bots; i++){
      t.on_connect(client(i));
      send(client(i), {{"type", "join"}, {"name", "Robo" + std::to_string(i)}, {"bot", true}});
    }
  }
  json state(int seat){ return last(client(seat), "state"); }
  std::string error_code(int seat){ return last(client(seat), "error").value("code", ""); }
  bool got_kind(int seat, const std::string& kind){
    for(const json& m : out.got[client(seat)]){
      if(m["type"] == "event" && m.value("kind", "") == kind) return true;
    }
    return false;
  }
};

}

// ---------------------------------------------------------------- coming back

TEST(disconnect_keeps_the_cards_for_a_while){
  harness h(3, with_grace(30));
  h.start();
  int s = h.t.get_turn();
  int other = (s + 1) % 3;
  std::string token = h.token_of(harness::client(s));
  h.t.on_disconnect(harness::client(s));

  // Still in the hand, still their turn; the others see a countdown.
  CHECK(h.t.seat(s).in_hand);
  CHECK_EQ(h.t.get_turn(), s);
  json st = h.last(harness::client(other), "state");
  CHECK(st["seats"][s]["status"] == "disconnected");
  CHECK_EQ(st["seats"][s].value("reconnect_in", -1), 30);
  json left = h.last(harness::client(other), "event");
  CHECK(left.value("kind", "") == "disconnected");
  CHECK_EQ(left.value("grace", 0), 30);
  CHECK(left.value("text", "") == "P" + std::to_string(s) + " disconnected (30 s to come back)");

  h.now += 10000;
  h.t.tick();
  CHECK(h.t.seat(s).in_hand);

  // Back in time, on a new connection: same cards, and the move is theirs to make.
  h.join(250, "P" + std::to_string(s), token);
  CHECK_EQ(h.t.seat_of(250), s);
  st = h.last(250, "state");
  CHECK(st["seats"][s]["cards"].is_array());
  CHECK(st.contains("actions"));
  CHECK(!st["seats"][s].contains("reconnect_in"));
  h.send(250, {{"type", "action"}, {"action", "call"}});
  CHECK(h.t.get_turn() != s);
  CHECK(h.t.seat(s).in_hand);

  // Long after the old deadline nothing happens to them.
  h.now += 60000;
  h.t.tick();
  CHECK(h.t.seat(s).in_hand);
}

TEST(disconnect_folds_when_the_time_is_up){
  harness h(3, with_grace(30));
  h.start();
  int s = h.t.get_turn();
  int waiting = (s + 2) % 3;           // someone whose turn it is not
  h.t.on_disconnect(harness::client(waiting));
  h.t.on_disconnect(harness::client(s));
  h.now += 29999;
  h.t.tick();
  CHECK(h.t.seat(s).in_hand && h.t.seat(waiting).in_hand);

  // Both are folded: that leaves one player, who wins the blinds.
  h.now += 1;
  h.t.tick();
  h.t.tick();
  CHECK(!h.t.seat(s).in_hand && !h.t.seat(waiting).in_hand);
  CHECK(h.t.get_stage() == WAITING);
  int left = 3 - s - waiting;
  CHECK(h.got_event(harness::client(left), "P" + std::to_string(s) + " folds (disconnected)"));
  CHECK(h.got_event(harness::client(left), "P" + std::to_string(waiting) + " folds (disconnected)"));
  CHECK_EQ(h.total_chips(), 600);
  // The history says why they folded.
  int folds = 0;
  for(const json& a : h.t.last_hand()["actions"]){
    folds += a["action"] == "fold" && a.value("reason", "") == "disconnected";
  }
  CHECK_EQ(folds, 2);
  // Between hands nobody is shown as "coming back".
  CHECK(!h.last(harness::client(left), "state")["seats"][s].contains("reconnect_in"));
}

TEST(disconnect_while_someone_else_decides_does_not_stop_the_hand){
  harness h(3, with_grace(30));
  h.start();
  int s = h.t.get_turn();
  int away = (s + 1) % 3;
  h.t.on_disconnect(harness::client(away));
  h.act(s, "call");
  CHECK_EQ(h.t.get_turn(), away);      // the turn comes to the absent player and waits
  h.now += 30000;
  h.t.tick();
  CHECK(!h.t.seat(away).in_hand);
  CHECK(h.t.get_turn() != away);
  CHECK(h.t.get_stage() != WAITING);
}

TEST(all_in_player_who_disconnects_stays_in_the_hand){
  harness h(3, with_grace(30));
  h.start();
  int a = h.t.get_turn();
  h.act(a, "raise", h.t.seat(a).round_bet + h.t.seat(a).stack);
  int b = h.t.get_turn();
  h.act(b, "call");
  int c = h.t.get_turn();
  CHECK(h.t.seat(a).all_in && h.t.seat(b).all_in && c != a && c != b);

  h.t.on_disconnect(harness::client(a));
  CHECK(!h.last(harness::client(c), "state")["seats"][a].contains("reconnect_in"));
  h.now += 120000;
  h.t.tick();
  CHECK(h.t.seat(a).in_hand);
  h.act(c, "fold");
  CHECK(h.t.get_stage() == WAITING);
  CHECK_EQ(h.t.last_hand()["showdown"].size(), 2);
  CHECK_EQ(h.total_chips(), 600);
}

TEST(turn_timer_still_applies_to_an_absent_player){
  harness h(3, with_grace(60, 20));
  h.start();
  int s = h.t.get_turn();
  h.t.on_disconnect(harness::client(s));
  h.now += 20000;
  h.t.tick();                          // out of time before the grace runs out: folds
  CHECK(!h.t.seat(s).in_hand);
  h.now += 60000;
  h.t.tick();                          // and is not folded a second time
  int folds = 0;
  for(const json& m : h.out.got[harness::client((s + 1) % 3)]){
    folds += m["type"] == "event" && m.value("kind", "") == "fold" && m.value("name", "") == h.t.seat(s).name;
  }
  CHECK_EQ(folds, 1);
}

// ---------------------------------------------------------------- ending and restarting

TEST(a_person_alone_with_bots_ends_the_game_at_once_bots_never_can){
  solo_table h(2);
  CHECK(h.state(0).value("solo", false) == true);
  CHECK(h.state(1).value("solo", true) == false);       // a bot never is
  CHECK(h.state(0)["seats"][1].value("bot", false) == true);
  CHECK(h.state(0)["seats"][0].value("bot", true) == false);
  h.start();
  h.send(harness::client(1), {{"type", "end_game"}});
  CHECK(h.error_code(1) == "bots_cannot");
  h.send(harness::client(2), {{"type", "new_game"}});
  CHECK(h.error_code(2) == "bots_cannot");
  CHECK(h.t.get_stage() != WAITING);

  // A second person sits down: ending now needs her yes (see test_leave_and_vote).
  h.join(301, "Alice");
  CHECK(h.state(0).value("solo", true) == false);
  h.send(harness::client(0), {{"type", "end_game"}});
  CHECK(h.t.get_stage() != WAITING);
  CHECK(h.state(0).value("game_over", true) == false);
  CHECK(h.state(0)["vote"].value("kind", "") == "end");
  h.send(301, {{"type", "vote"}, {"agree", false}});
  CHECK(h.state(0)["vote"].is_null());

  // She disconnects again: alone once more, it takes effect at once.
  h.t.on_disconnect(301);
  CHECK(h.state(0).value("solo", false) == true);
  h.send(harness::client(0), {{"type", "end_game"}});
  CHECK(h.state(0).value("game_over", false) == true);
}

TEST(ending_the_game_cancels_the_hand_and_shows_who_is_up){
  solo_table h(1);
  // One hand first, so that the stacks differ: whoever acts first raises, the other folds.
  h.start();
  int first = h.t.get_turn();
  h.act(first, "raise", 10);
  h.act(1 - first, "fold");
  CHECK(h.t.get_stage() == WAITING);
  int up = h.t.seat(first).stack - 200;
  CHECK(up > 0);

  // In the middle of the next hand the person has had enough.
  h.start();
  h.act(h.t.get_turn(), "raise", 20);
  json before = h.t.last_hand();
  h.send(harness::client(0), {{"type", "end_game"}});

  CHECK(h.t.get_stage() == WAITING);
  CHECK_EQ(h.t.get_pot(), 0);
  CHECK_EQ(h.t.seat(first).stack, 200 + up);            // every chip of the cancelled hand went back
  CHECK_EQ(h.t.seat(1 - first).stack, 200 - up);
  CHECK(h.t.last_hand() == before);                     // and it is not in the history
  CHECK(h.got_kind(1, "hand_cancelled") && h.got_kind(1, "game_over"));

  json st = h.state(0);
  CHECK(st.value("game_over", false) == true);
  CHECK(st["board"].empty() && st["seats"][0]["cards"].is_null());
  CHECK(!st.contains("result"));
  CHECK_EQ(st["standings"].size(), 2);
  CHECK(st["standings"][0]["seat"] == first);           // best first
  CHECK_EQ(st["standings"][0].value("net", 0), up);
  CHECK_EQ(st["standings"][1].value("net", 0), -up);
  CHECK(st["standings"][1 - first == 1 ? 1 : 0].value("bot", false) == true);

  h.send(harness::client(0), {{"type", "end_game"}});
  CHECK(h.error_code(0) == "game_over");
}

TEST(nothing_is_dealt_after_the_end_until_a_new_game){
  solo_table h(1, quiet_config(30, 5));                 // next hand would start by itself after 5 s
  h.start();
  h.play_out();
  h.send(harness::client(0), {{"type", "end_game"}});
  h.now += 60000;
  h.t.tick();
  CHECK(h.t.get_stage() == WAITING);
  // A bot cannot decide to play on: only the host deals.
  h.send(harness::client(1), {{"type", "start"}});
  CHECK(h.error_code(1) == "not_host");
  CHECK(h.t.get_stage() == WAITING);

  h.send(harness::client(0), {{"type", "new_game"}});
  json st = h.state(1);
  CHECK(st.value("game_over", true) == false);
  CHECK(!st.contains("standings"));
  CHECK_EQ(st.value("hand", -1), 0);
  CHECK_EQ(h.t.seat(0).stack, 200);
  CHECK_EQ(h.t.seat(1).stack, 200);
  CHECK(h.got_kind(1, "new_game"));
  CHECK(h.t.get_stage() == WAITING);                    // a person still deals each hand
  h.start();
  CHECK_EQ(h.t.hand_number(), 1);
  CHECK(h.t.get_stage() == PREFLOP);
}

TEST(restart_in_the_middle_of_a_hand){
  solo_table h(2);
  h.start();
  h.play_out();
  h.start();
  h.act(h.t.get_turn(), "raise", 30);
  h.send(harness::client(0), {{"type", "new_game"}});
  CHECK(h.t.get_stage() == WAITING);
  CHECK_EQ(h.t.hand_number(), 0);
  CHECK_EQ(h.t.get_dealer(), -1);
  for(int i = 0; i < 3; i++){
    CHECK_EQ(h.t.seat(i).stack, 200);
    CHECK(!h.t.seat(i).in_hand);
  }
  CHECK(h.state(0).value("game_over", true) == false);
  CHECK(h.got_kind(0, "hand_cancelled") && h.got_kind(0, "new_game"));
}

TEST(start_after_the_end_begins_a_new_game){
  // The terminal client only knows "start": it must not be stuck after the end.
  solo_table h(1);
  h.start();
  int first = h.t.get_turn();
  h.act(first, "raise", 10);
  h.act(1 - first, "fold");
  h.send(harness::client(0), {{"type", "end_game"}});
  h.start();
  CHECK(h.t.get_stage() == PREFLOP);
  CHECK_EQ(h.t.hand_number(), 1);
  CHECK(h.state(0).value("game_over", true) == false);
  CHECK_EQ(h.total_chips(), 400);
  CHECK(h.got_kind(0, "new_game"));

  // Once it is over and a second person has joined, either of them may begin again.
  h.send(harness::client(0), {{"type", "end_game"}});
  h.join(301, "Alice");
  h.send(301, {{"type", "new_game"}});
  CHECK(h.last(301, "state").value("game_over", true) == false);
  CHECK_EQ(h.t.seat(h.t.seat_of(301)).stack, 200);
}

// ---------------------------------------------------------------- a bet nobody called

TEST(uncalled_bet_goes_back_and_is_not_counted_as_won){
  harness h(3);
  h.start();
  // Seat 0 raises to 60, the blinds fold: 58 of it was never matched.
  h.act(0, "raise", 60);
  h.act(1, "fold");
  h.act(2, "fold");
  CHECK(h.t.get_stage() == WAITING);
  json back = json();
  for(const json& m : h.out.got[harness::client(1)]){
    if(m["type"] == "event" && m.value("kind", "") == "uncalled") back = m;
  }
  CHECK(back.value("name", "") == "P0");
  CHECK_EQ(back.value("amount", 0), 58);
  CHECK(back.value("text", "") == "Uncalled bet of 58 returned to P0");
  // What was really won: the two blinds and the 2 of the raise that the big blind covered.
  json st = h.last(harness::client(1), "state");
  CHECK_EQ(st["result"][0]["amount"].get<int>(), 5);
  CHECK(h.got_event(harness::client(1), "P0 wins 5"));
  CHECK_EQ(h.t.seat(0).stack, 203);
  CHECK_EQ(h.total_chips(), 600);

  const json& hand = h.t.last_hand();
  CHECK_EQ(hand["pot"].get<int>(), 5);
  CHECK(hand["returned"][0]["seat"] == 0 && hand["returned"][0]["amount"] == 58);
  CHECK(hand["winners"][0]["won"] == 5);
  CHECK(check_hand(hand) == "");
}

TEST(a_called_bet_is_not_returned){
  harness h(2);
  h.start();
  int first = h.t.get_turn();
  h.act(first, "raise", 20);
  h.act(1 - first, "call");
  h.play_out();
  const json& hand = h.t.last_hand();
  CHECK(hand["returned"].empty());
  CHECK_EQ(hand["pot"].get<int>(), 40);
  int won = 0;
  for(const json& w : hand["winners"]) won += w["won"].get<int>();
  CHECK_EQ(won, 40);
  CHECK(check_hand(hand) == "");
}

TEST(the_part_of_a_bet_a_short_stack_cannot_cover_goes_back){
  harness h(2);
  h.start();
  h.play_out();                                   // an ordinary hand first
  h.t.set_stack(0, 30);                           // then seat 0 is short
  h.t.set_stack(1, 370);
  h.start();
  if(h.t.get_turn() == 0) h.act(0, "call");       // the short stack is the dealer: it completes the blind first
  h.act(1, "raise", 100);
  h.act(0, "call");                               // all it has: 30
  CHECK(h.t.get_stage() == WAITING);              // nobody can bet any more: straight to showdown
  const json& hand = h.t.last_hand();
  CHECK_EQ(hand["showdown"].size(), 2);
  CHECK_EQ(hand["pot"].get<int>(), 60);           // 30 each
  CHECK(hand["returned"][0]["seat"] == 1 && hand["returned"][0]["amount"] == 70);
  CHECK(check_hand(hand) == "");
  CHECK_EQ(h.total_chips(), 400);
  // Seat 1 can lose 30 at most, whatever the cards.
  CHECK(h.t.seat(1).stack >= 340);
}
