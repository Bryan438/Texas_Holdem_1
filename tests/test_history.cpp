// Hand history: what the table records for each hand, what each player is
// sent afterwards, the history file, and the stats computed from it.
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <unistd.h>
#include "test_framework.h"
#include "table_harness.h"
#include "stats.h"

static const json* find_player(const json& hand, int seat){
  for(const json& p : hand["players"]){
    if(p["seat"].get<int>() == seat) return &p;
  }
  return nullptr;
}

TEST(history_records_blinds_actions_and_winner){
  harness h(3);
  h.start();                       // dealer 0, SB 1, BB 2
  h.act(0, "fold");
  h.act(1, "fold");
  const json& hand = h.t.last_hand();

  CHECK_EQ(hand["hand"].get<int>(), 1);
  CHECK(hand["started_at"].is_string());
  CHECK_EQ(hand["dealer"].get<int>(), 0);
  CHECK_EQ(hand["players"].size(), 3);
  CHECK_EQ(hand["players"][0]["cards"].size(), 2);
  CHECK_EQ(hand["players"][0]["stack"].get<int>(), 200);   // stack before the blinds

  const json& a = hand["actions"];
  CHECK_EQ(a.size(), 4);
  CHECK(a[0]["action"] == "small_blind" && a[0]["seat"] == 1 && a[0]["amount"] == 1);
  CHECK(a[1]["action"] == "big_blind" && a[1]["seat"] == 2 && a[1]["amount"] == 2);
  CHECK(a[2]["action"] == "fold" && a[2]["seat"] == 0 && a[2]["street"] == "preflop");
  CHECK(a[3]["action"] == "fold" && a[3]["seat"] == 1);

  CHECK_EQ(hand["pot"].get<int>(), 3);
  CHECK(hand["board"].empty());
  CHECK(hand["showdown"].empty());
  CHECK_EQ(hand["winners"].size(), 1);
  CHECK(hand["winners"][0]["seat"] == 2 && hand["winners"][0]["won"] == 3);
  CHECK_EQ((*find_player(hand, 2))["net"].get<int>(), 1);
  CHECK_EQ((*find_player(hand, 1))["net"].get<int>(), -1);
  CHECK_EQ((*find_player(hand, 2))["end_stack"].get<int>(), 201);
  CHECK(check_hand(hand) == "");
}

TEST(history_summary_hides_other_players_cards){
  harness h(3);
  h.start();
  h.act(0, "fold");
  h.act(1, "fold");
  for(int seat = 0; seat < 3; seat++){
    json summary = h.last(harness::client(seat), "hand_summary");
    CHECK(!summary.is_null());
    CHECK_EQ(summary["you"].get<int>(), seat);
    for(const json& p : summary["players"]){
      bool mine = p["seat"].get<int>() == seat;
      CHECK(p.contains("cards") == mine);
    }
    CHECK(summary["winners"][0]["won"] == 3);
  }
}

TEST(history_showdown_cards_are_public){
  harness h(2);
  h.start();
  h.play_out();                    // check / call down to showdown
  const json& hand = h.t.last_hand();
  CHECK_EQ(hand["board"].size(), 5);
  CHECK_EQ(hand["showdown"].size(), 2);
  CHECK(hand["showdown"][0]["hand"].is_string());
  CHECK(check_hand(hand) == "");

  json summary = h.last(harness::client(1), "hand_summary");
  CHECK_EQ(summary["showdown"].size(), 2);
  CHECK(summary["showdown"][0]["cards"] == hand["showdown"][0]["cards"]);

  // Each street's actions are labelled.
  bool saw_river = false;
  for(const json& a : hand["actions"]) saw_river = saw_river || a["street"] == "river";
  CHECK(saw_river);
}

TEST(history_records_raise_timeout_and_disconnect){
  harness h(3, quiet_config(10));
  h.start();
  h.now += 10000;
  h.t.tick();                      // seat 0 runs out of time and folds
  h.act(1, "raise", 10);           // seat 1 has 1 in: puts in 9 more
  h.t.on_disconnect(harness::client(2));
  const json& a = h.t.last_hand()["actions"];
  CHECK_EQ(a.size(), 5);
  CHECK(a[2]["action"] == "fold" && a[2]["reason"] == "timeout");
  CHECK(a[3]["action"] == "raise" && a[3]["to"] == 10 && a[3]["amount"] == 9);
  CHECK(a[4]["action"] == "fold" && a[4]["reason"] == "disconnected" && a[4]["seat"] == 2);
  CHECK(check_hand(h.t.last_hand()) == "");
}

TEST(history_all_in_is_marked){
  harness h(2);
  h.t.set_stack(0, 30);
  h.start();
  h.act(0, "raise", 30);
  h.act(1, "call");
  const json& hand = h.t.last_hand();
  bool marked = false;
  for(const json& a : hand["actions"]) marked = marked || (a["seat"] == 0 && a.value("allin", false));
  CHECK(marked);
  CHECK_EQ(hand["board"].size(), 5);
  CHECK(check_hand(hand) == "");
}

// Many hands with random choices: every record must add up and no chip may
// appear or vanish.
TEST(history_random_hands_always_add_up){
  harness h(4);
  std::mt19937 rng(12345);
  int checked = 0;
  for(int hand = 0; hand < 40; hand++){
    int with_chips = 0;
    for(int i = 0; i < 4; i++) with_chips += h.t.seat(i).stack > 0;
    if(with_chips < 2){
      for(int i = 0; i < 4; i++) h.t.set_stack(i, 200);
    }
    int chips_before = h.total_chips();
    h.start();
    for(int guard = 0; guard < 200 && h.t.get_stage() != WAITING; guard++){
      int s = h.t.get_turn();
      json actions = h.last(harness::client(s), "state")["actions"];
      int roll = rng() % 10;
      bool can_raise = actions["raise"].is_object();
      if(roll < 2){
        h.act(s, "fold");
      }
      else if(roll < 7 || !can_raise){
        h.act(s, actions["check"].get<bool>() ? "check" : "call");
      }
      else{
        int lo = actions["raise"]["min"].get<int>();
        int hi = actions["raise"]["max"].get<int>();
        h.act(s, "raise", roll == 9 ? hi : lo + (int)(rng() % (hi - lo + 1)));
      }
    }
    CHECK_EQ(h.t.get_stage(), WAITING);
    std::string problem = check_hand(h.t.last_hand());
    if(!problem.empty()) printf("    hand %d: %s\n", hand + 1, problem.c_str());
    CHECK(problem.empty());
    CHECK_EQ(h.total_chips(), chips_before);
    checked++;
  }
  CHECK_EQ(checked, 40);
}

TEST(history_file_gets_one_line_per_hand){
  std::string path = "/tmp/th_history_test_" + std::to_string(getpid()) + ".jsonl";
  std::remove(path.c_str());
  table_config config = quiet_config();
  config.history_path = path;
  {
    harness h(2, config);
    h.start();
    h.act(0, "fold");
    h.start();
    h.play_out();
  }
  std::ifstream in(path);
  std::vector<json> hands;
  std::string line;
  while(std::getline(in, line)){
    hands.push_back(json::parse(line));
  }
  CHECK_EQ(hands.size(), 2);
  if(hands.size() == 2){
    CHECK_EQ(hands[0]["hand"].get<int>(), 1);
    CHECK_EQ(hands[1]["hand"].get<int>(), 2);
    CHECK(hands[1]["players"][0].contains("cards"));   // the file keeps every hole card
    CHECK(check_hand(hands[0]) == "");
    CHECK(check_hand(hands[1]) == "");
  }
  std::remove(path.c_str());
}

TEST(history_no_file_when_disabled){
  harness h(2);                    // quiet_config: history_path is ""
  h.start();
  h.act(0, "fold");
  CHECK(!h.t.last_hand().is_null());
}

static json made_up_hand(){
  return json::parse(R"({
    "hand": 1, "pot": 24,
    "players": [
      {"seat":0,"name":"Alice","stack":100,"end_stack":112,"net":12},
      {"seat":1,"name":"Bob","stack":100,"end_stack":88,"net":-12},
      {"seat":2,"name":"Cat","stack":100,"end_stack":100,"net":0}
    ],
    "actions": [
      {"street":"preflop","seat":1,"action":"small_blind","amount":1},
      {"street":"preflop","seat":2,"action":"big_blind","amount":2},
      {"street":"preflop","seat":0,"action":"raise","amount":6,"to":6},
      {"street":"preflop","seat":1,"action":"call","amount":5},
      {"street":"preflop","seat":2,"action":"fold","amount":0},
      {"street":"flop","seat":1,"action":"check","amount":0},
      {"street":"flop","seat":0,"action":"bet","amount":6,"to":6},
      {"street":"flop","seat":1,"action":"call","amount":6}
    ],
    "showdown": [{"seat":0},{"seat":1}],
    "winners": [{"seat":0,"won":24}]
  })");
}

TEST(stats_count_vpip_pfr_net_and_showdowns){
  json hand = made_up_hand();
  // Cat put in 2 as the big blind and lost it: fix the made-up numbers.
  hand["players"][2]["end_stack"] = 98;
  hand["players"][2]["net"] = -2;
  hand["pot"] = 26;
  hand["winners"][0]["won"] = 26;
  hand["players"][0]["end_stack"] = 114;
  hand["players"][0]["net"] = 14;
  CHECK(check_hand(hand) == "");

  auto stats = compute_stats({hand, hand});
  CHECK_EQ(stats["Alice"].hands, 2);
  CHECK_EQ(stats["Alice"].net, 28);
  CHECK_EQ(stats["Alice"].vpip, 2);       // raised
  CHECK_EQ(stats["Alice"].pfr, 2);
  CHECK_EQ(stats["Alice"].showdowns_won, 2);
  CHECK_EQ(stats["Alice"].biggest_win, 26);
  CHECK_EQ(stats["Bob"].vpip, 2);         // called (the small blind itself does not count)
  CHECK_EQ(stats["Bob"].pfr, 0);
  CHECK_EQ(stats["Bob"].showdowns, 2);
  CHECK_EQ(stats["Bob"].showdowns_won, 0);
  CHECK_EQ(stats["Cat"].vpip, 0);         // only posted the big blind
  CHECK_EQ(stats["Cat"].net, -4);

  std::string table = format_stats(stats);
  CHECK(table.find("Alice") < table.find("Bob"));   // sorted by net, best first
  CHECK(table.find("100%") != std::string::npos);
}

TEST(stats_check_hand_finds_bad_records){
  json hand = made_up_hand();             // as written, Cat's blind is unaccounted for
  CHECK(!check_hand(hand).empty());
  hand["winners"][0]["won"] = 999;
  CHECK(check_hand(hand).find("paid out") != std::string::npos);
}
