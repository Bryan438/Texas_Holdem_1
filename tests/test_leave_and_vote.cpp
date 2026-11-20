// People playing together: getting up from the table, the server removing
// someone, and ending or restarting a game only when everyone agrees.
#include <string>
#include "test_framework.h"
#include "table_harness.h"

namespace {

// P0, P1, P2 (clients 101-103) and helpers to read what they were sent.
struct friends: harness{
  explicit friends(int players = 3, table_config config = quiet_config()) : harness(players, config){}
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
  void vote(int c, bool agree){ send(c, {{"type", "vote"}, {"agree", agree}}); }
};

}

// ---------------------------------------------------------------- leaving

TEST(leaving_between_hands_frees_the_seat_at_once){
  friends h;
  h.send(harness::client(1), {{"type", "leave"}});
  CHECK(h.last(harness::client(1), "left").value("reason", "") == "left");
  CHECK_EQ(h.t.seat_count(), 2);
  CHECK(h.seat_named("P1") < 0);
  CHECK(h.got_kind(harness::client(0), "left"));
  // No longer seated: the game does not take orders from that connection...
  h.send(harness::client(1), {{"type", "start"}});
  CHECK(h.error_code(harness::client(1)) == "join_first");
  // ...but it may sit down again (see test_host_and_ledger for the chips).
  h.join(harness::client(1), "P1", h.token_of(harness::client(1)));
  CHECK(h.seat_named("P1") >= 0);
  CHECK_EQ(h.t.seat(h.seat_named("P1")).stack, 200);
}

TEST(leaving_on_your_turn_folds_and_the_hand_goes_on){
  friends h;
  h.start();
  int s = h.t.get_turn();
  std::string name = h.t.seat(s).name;
  h.send(harness::client(s), {{"type", "leave"}});
  CHECK(h.t.get_stage() == PREFLOP);
  CHECK(h.t.get_turn() != s);
  CHECK(h.state(harness::client((s + 1) % 3))["seats"][s].value("status", "") == "left");
  CHECK_EQ(h.total_chips(), 600);                 // the chips stay at the table until the hand is over
  // Nobody can take the name back while the hand runs.
  h.join(401, name);
  CHECK(h.error_code(401) == "name_leaving");
  h.play_out();
  CHECK(h.t.get_stage() == WAITING);
  CHECK_EQ(h.t.seat_count(), 2);
  CHECK(h.seat_named(name) < 0);
  json folds = h.t.last_hand()["actions"];
  bool recorded = false;
  for(const json& a : folds){
    recorded = recorded || (a.value("action", "") == "fold" && a.value("reason", "") == "left");
  }
  CHECK(recorded);
}

TEST(when_all_but_one_leave_the_last_one_wins_the_pot){
  friends h(2);
  h.start();
  int s = h.t.get_turn();
  h.send(harness::client(1 - s), {{"type", "leave"}});   // the one not to act
  CHECK(h.t.get_stage() == WAITING);
  CHECK_EQ(h.t.seat_count(), 1);
  CHECK(h.got_kind(harness::client(s), "win"));
}

TEST(the_button_stays_with_the_same_player_when_someone_leaves){
  for(int leaver = 0; leaver < 4; leaver++){
    friends h(4);
    h.start();
    h.play_out();
    int dealer = h.t.get_dealer();
    if(leaver == dealer) continue;
    std::string name = h.t.seat(dealer).name;
    h.send(harness::client(leaver), {{"type", "leave"}});
    CHECK_EQ(h.t.seat_count(), 3);
    CHECK(h.t.seat(h.t.get_dealer()).name == name);
  }
}

// ---------------------------------------------------------------- the server removes someone

TEST(kick_removes_a_player_and_tells_them){
  friends h;
  h.start();
  int s = h.t.get_turn();
  std::string name = h.t.seat(s).name;
  std::string error;
  CHECK(!h.t.kick("Nobody", error));
  CHECK(error.find("Nobody") != std::string::npos);
  CHECK(h.t.kick(name, error));
  CHECK(h.last(harness::client(s), "left").value("reason", "") == "kicked");
  CHECK(h.got_kind(harness::client((s + 1) % 3), "kicked"));
  CHECK(h.t.get_turn() != s);
  CHECK(!h.t.kick(name, error));                  // already on the way out
  h.play_out();
  CHECK(h.seat_named(name) < 0);
  CHECK(h.t.roster().find(name) == std::string::npos);
  CHECK(h.t.roster().find("P") != std::string::npos);
}

TEST(kick_clears_the_seat_of_someone_who_disconnected){
  friends h(3, []{ table_config c = quiet_config(); c.disconnect_grace_seconds = 30; return c; }());
  h.t.on_disconnect(harness::client(2));
  CHECK_EQ(h.t.seat_count(), 3);                  // kept for them to come back
  std::string error;
  CHECK(h.t.kick("P2", error));
  CHECK_EQ(h.t.seat_count(), 2);
}

// ---------------------------------------------------------------- votes

TEST(ending_needs_everyone_present_to_agree){
  friends h;
  h.start();
  h.send(harness::client(0), {{"type", "end_game"}});
  json vote = h.state(harness::client(1))["vote"];
  CHECK(vote.value("kind", "") == "end");
  CHECK(vote.value("by", "") == "P0");
  CHECK_EQ(vote["agreed"].size(), 1);
  CHECK_EQ(vote["waiting"].size(), 2);
  CHECK_EQ(vote.value("seconds_left", 0), 60);
  CHECK(h.got_kind(harness::client(2), "vote_started"));

  h.vote(harness::client(1), true);
  h.vote(harness::client(1), true);               // counted once
  CHECK(h.state(harness::client(0))["vote"]["waiting"].size() == 1);
  CHECK(h.state(harness::client(0)).value("game_over", true) == false);
  h.vote(harness::client(2), true);
  CHECK(h.state(harness::client(0)).value("game_over", false) == true);
  CHECK(h.state(harness::client(0))["vote"].is_null());
  CHECK(h.t.get_stage() == WAITING);              // the hand was called off, bets returned
  CHECK_EQ(h.total_chips(), 600);
  CHECK(h.got_event(harness::client(0), "Everyone agreed: the game is over"));
}

TEST(one_no_drops_the_vote){
  friends h;
  h.send(harness::client(0), {{"type", "new_game"}});
  h.vote(harness::client(1), false);
  CHECK(h.state(harness::client(0))["vote"].is_null());
  CHECK(h.got_kind(harness::client(0), "vote_declined"));
  h.vote(harness::client(2), true);
  CHECK(h.error_code(harness::client(2)) == "no_vote");
}

TEST(a_vote_runs_out_after_a_minute){
  friends h;
  h.send(harness::client(0), {{"type", "end_game"}});
  h.now += 59 * 1000;
  h.t.tick();
  CHECK(!h.state(harness::client(0))["vote"].is_null());
  h.now += 1000;
  h.t.tick();
  CHECK(h.state(harness::client(0))["vote"].is_null());
  CHECK(h.got_kind(harness::client(1), "vote_expired"));
  CHECK(h.state(harness::client(0)).value("game_over", true) == false);
}

TEST(asking_for_the_same_thing_is_a_yes_asking_for_another_is_refused){
  friends h(2);
  h.send(harness::client(0), {{"type", "end_game"}});
  h.send(harness::client(1), {{"type", "new_game"}});
  CHECK(h.error_code(harness::client(1)) == "vote_open");
  h.send(harness::client(1), {{"type", "end_game"}});
  CHECK(h.state(harness::client(0)).value("game_over", false) == true);
}

TEST(restart_by_vote_gives_everyone_fresh_chips){
  friends h(2);
  h.start();
  int first = h.t.get_turn();
  h.act(first, "raise", 10);
  h.act(1 - first, "fold");
  CHECK(h.t.seat(0).stack != 200);
  h.send(harness::client(1), {{"type", "new_game"}});
  h.vote(harness::client(0), true);
  CHECK_EQ(h.t.seat(0).stack, 200);
  CHECK_EQ(h.t.seat(1).stack, 200);
  CHECK_EQ(h.t.hand_number(), 0);
  CHECK(h.got_event(harness::client(0), "Everyone agreed to a new game: everyone has 200 chips"));
  // Once a game is over, a new one needs no vote.
  h.send(harness::client(0), {{"type", "end_game"}});
  h.vote(harness::client(1), true);
  h.send(harness::client(1), {{"type", "new_game"}});
  CHECK(h.state(harness::client(0)).value("game_over", true) == false);
}

TEST(people_who_leave_or_drop_do_not_hold_up_a_vote){
  friends h;
  h.send(harness::client(0), {{"type", "end_game"}});
  h.vote(harness::client(1), true);
  h.send(harness::client(2), {{"type", "leave"}});       // the last one not to agree gets up
  CHECK(h.state(harness::client(0)).value("game_over", false) == true);

  friends g;
  g.send(harness::client(0), {{"type", "new_game"}});
  g.vote(harness::client(1), true);
  g.t.on_disconnect(harness::client(2));
  CHECK(g.got_kind(harness::client(0), "new_game"));

  // The one who asked leaves and nobody else agreed: nothing to vote on.
  friends f;
  f.send(harness::client(0), {{"type", "end_game"}});
  f.send(harness::client(0), {{"type", "leave"}});
  CHECK(f.state(harness::client(1))["vote"].is_null());
  CHECK(f.state(harness::client(1)).value("game_over", true) == false);
}

TEST(someone_who_joins_during_a_vote_is_asked_too){
  friends h(2);
  h.send(harness::client(0), {{"type", "end_game"}});
  h.join(301, "Late");
  CHECK_EQ(h.state(301)["vote"]["waiting"].size(), 2);
  h.vote(harness::client(1), true);
  CHECK(h.state(301).value("game_over", true) == false);
  h.vote(301, true);
  CHECK(h.state(301).value("game_over", false) == true);
}

TEST(a_reloaded_page_is_not_a_yes){
  table_config c = quiet_config();
  c.disconnect_grace_seconds = 30;
  friends h(2, c);
  h.send(harness::client(0), {{"type", "new_game"}});
  h.t.on_disconnect(harness::client(1));          // P1 reloads the page
  CHECK(!h.state(harness::client(0))["vote"].is_null());
  CHECK_EQ(h.state(harness::client(0))["vote"]["waiting"].size(), 1);
  h.now += 2000;
  h.join(harness::client(1), "P1", h.token_of(harness::client(1)));
  h.vote(harness::client(1), false);
  CHECK(h.got_kind(harness::client(0), "vote_declined"));

  // Gone for good: after the time to come back, the vote no longer waits.
  h.send(harness::client(0), {{"type", "end_game"}});
  h.t.on_disconnect(harness::client(1));
  h.now += 29 * 1000;
  h.t.tick();
  CHECK(h.state(harness::client(0)).value("game_over", true) == false);
  h.now += 1000;
  h.t.tick();
  CHECK(h.state(harness::client(0)).value("game_over", false) == true);
}

TEST(a_vote_whose_backers_all_left_is_dropped){
  friends h;
  h.send(harness::client(0), {{"type", "end_game"}});
  h.send(harness::client(0), {{"type", "leave"}});
  CHECK(h.got_kind(harness::client(1), "vote_dropped"));
}
