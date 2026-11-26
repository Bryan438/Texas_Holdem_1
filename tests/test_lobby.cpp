// Several tables behind one server: the list, opening tables, sitting down
// at one (with a password for a private one), getting up, closing.
#include <string>
#include "test_framework.h"
#include "table_harness.h"
#include "lobby.h"

namespace {

struct hall{
  fake_sender out;
  int64_t now = 0;
  lobby l;
  explicit hall(lobby_limits limits = lobby_limits())
    : l(&out, quiet_config(), limits, [this]{ return now; }){}

  void connect(int c){ l.on_connect(c); }
  void send(int c, const json& msg){ l.on_message(c, msg.dump()); }
  json last(int c, const std::string& type){
    std::vector<json>& msgs = out.got[c];
    for(auto it = msgs.rbegin(); it != msgs.rend(); ++it){
      if((*it)["type"] == type) return *it;
    }
    return json();
  }
  std::string error_code(int c){ return last(c, "error").value("code", ""); }
  int count(int c, const std::string& type){
    int n = 0;
    for(const json& m : out.got[c]) n += m["type"] == type;
    return n;
  }
  json tables(int c){ return last(c, "lobby")["tables"]; }
  int create(int c, const json& extra = json::object()){
    json msg = {{"type", "create_table"}, {"name", "Friday"}};
    msg.update(extra);
    int before = count(c, "table_created");
    send(c, msg);
    return count(c, "table_created") > before ? last(c, "table_created").value("table", -1) : -1;
  }
  void join(int c, const std::string& name, int table = 0, const std::string& password = ""){
    json msg = {{"type", "join"}, {"name", name}};
    if(table > 0) msg["table"] = table;
    if(!password.empty()) msg["password"] = password;
    send(c, msg);
  }
};

}

TEST(a_connection_starts_in_the_lobby_with_the_main_table){
  hall h;
  h.connect(1);
  CHECK(h.last(1, "hello").value("lobby", false));
  json tables = h.tables(1);
  CHECK_EQ(tables.size(), 1);
  CHECK(tables[0].value("main", false));
  CHECK_EQ(tables[0].value("id", 0), lobby::MAIN);
  CHECK(tables[0]["blinds"] == json({1, 2}));
  CHECK_EQ(tables[0].value("people", -1), 0);
  CHECK(h.last(1, "lobby").value("can_create", false));
  h.send(1, {{"type", "start"}});
  CHECK(h.error_code(1) == "join_first");
}

TEST(join_without_a_table_sits_at_the_main_one){
  // Older clients and thbot know nothing of tables.
  hall h;
  h.connect(1);
  h.join(1, "Ann");
  CHECK_EQ(h.l.table_of(1), lobby::MAIN);
  CHECK(!h.last(1, "welcome").is_null());
  CHECK_EQ(h.l.find(lobby::MAIN)->seat_count(), 1);
}

TEST(opening_a_table_with_its_own_blinds_and_chips){
  hall h;
  h.connect(1);
  h.connect(2);
  int id = h.create(1, {{"blinds", {2, 5}}, {"chips", 1000}});
  CHECK(id > lobby::MAIN);
  CHECK_EQ(h.l.table_of(1), -1);                 // opening is not sitting down
  json t = h.tables(2)[1];                       // everyone in the lobby sees it
  CHECK(t.value("name", "") == "Friday");
  CHECK(t["blinds"] == json({2, 5}));
  CHECK_EQ(t.value("chips", 0), 1000);
  CHECK(!t.value("private", true));

  h.join(1, "Ann", id);
  CHECK_EQ(h.l.table_of(1), id);
  CHECK_EQ(h.l.find(id)->seat(0).stack, 1000);
  CHECK(h.l.find(id)->host_name() == "Ann");
  CHECK_EQ(h.tables(2)[1].value("people", 0), 1);
  CHECK(h.tables(2)[1].value("host", "") == "Ann");

  // Default chips: 100 big blinds.
  int other = h.create(2, {{"blinds", {5, 10}}});
  CHECK_EQ(h.l.find(other)->settings().starting_stack, 1000);
}

TEST(opening_a_table_checks_what_it_is_given){
  lobby_limits small;
  small.max_tables = 2;
  hall h(small);
  h.connect(1);
  h.send(1, {{"type", "create_table"}, {"name", ""}});
  CHECK(h.error_code(1) == "table_name");
  h.create(1, {{"blinds", {3, 6}}});
  CHECK(h.error_code(1) == "bad_blinds");
  h.create(1, {{"chips", 10}});
  CHECK(h.error_code(1) == "bad_chips");
  h.create(1, {{"password", std::string(40, 'x')}});
  CHECK(h.error_code(1) == "bad_password");
  CHECK_EQ(h.l.table_count(), 1);
  CHECK(h.create(1) > 0);
  CHECK(!h.last(1, "lobby").value("can_create", true));
  h.create(1);
  CHECK(h.error_code(1) == "lobby_full");
  CHECK_EQ(h.l.table_count(), 2);
}

TEST(a_private_table_needs_its_password){
  hall h;
  h.connect(1);
  int id = h.create(1, {{"password", "sesame"}});
  CHECK(h.tables(1)[1].value("private", false));
  CHECK(h.tables(1)[1].dump().find("sesame") == std::string::npos);   // never shown
  h.join(1, "Ann", id);
  CHECK(h.error_code(1) == "bad_password");
  h.join(1, "Ann", id, "open");
  CHECK(h.error_code(1) == "bad_password");
  CHECK_EQ(h.l.table_of(1), -1);
  h.join(1, "Ann", id, "sesame");
  CHECK_EQ(h.l.table_of(1), id);
}

TEST(one_table_at_a_time_and_back_to_the_lobby_on_leaving){
  hall h;
  h.connect(1);
  int id = h.create(1);
  h.join(1, "Ann", id);
  h.send(1, {{"type", "create_table"}, {"name", "Two"}});
  CHECK(h.error_code(1) == "at_table");
  h.join(1, "Ann", lobby::MAIN);
  CHECK(h.error_code(1) == "already_seated");
  CHECK_EQ(h.l.table_of(1), id);

  int lists = h.count(1, "lobby");
  h.send(1, {{"type", "leave"}});
  CHECK(h.last(1, "left").value("reason", "") == "left");
  CHECK_EQ(h.l.table_of(1), -1);
  CHECK_EQ(h.count(1, "lobby"), lists + 1);      // the list again, to pick the next table
  h.join(1, "Ann", lobby::MAIN);
  CHECK_EQ(h.l.table_of(1), lobby::MAIN);
}

TEST(a_refused_join_stays_in_the_lobby){
  hall h;
  h.connect(1);
  h.connect(2);
  h.join(1, "Ann");
  h.join(2, "Ann");
  CHECK(h.error_code(2) == "name_taken");
  CHECK_EQ(h.l.table_of(2), -1);
  h.join(2, "Bob", 99);
  CHECK(h.error_code(2) == "no_table");
}

TEST(tables_play_on_their_own){
  hall h;
  for(int c = 1; c <= 4; c++) h.connect(c);
  int id = h.create(1);
  h.join(1, "Ann");
  h.join(2, "Bob");
  h.join(3, "Cat", id);
  h.join(4, "Dan", id);
  h.send(1, {{"type", "start"}});
  CHECK(h.l.find(lobby::MAIN)->get_stage() == PREFLOP);
  CHECK(h.l.find(id)->get_stage() == WAITING);
  // Messages from the other table never reach this one.
  CHECK(h.last(3, "state").value("hand", -1) == 0);
  h.send(3, {{"type", "start"}});
  CHECK(h.l.find(id)->get_stage() == PREFLOP);
  // A name may be used again at another table.
  h.connect(5);
  h.join(5, "Ann", id);
  CHECK_EQ(h.l.table_of(5), id);
}

TEST(a_dropped_connection_comes_back_to_its_seat){
  table_config c = quiet_config();
  hall h;
  h.connect(1);
  int id = h.create(1);
  h.join(1, "Ann", id);
  std::string token = h.last(1, "welcome").value("token", "");
  h.l.on_disconnect(1);
  CHECK_EQ(h.l.table_of(1), -1);
  CHECK_EQ(h.l.find(id)->seat_count(), 1);       // the seat waits
  h.connect(7);
  h.send(7, {{"type", "join"}, {"name", "Ann"}, {"table", id}, {"token", token}});
  CHECK_EQ(h.l.table_of(7), id);
  CHECK_EQ(h.l.find(id)->seat(0).socket, 7);
}

TEST(the_host_kicking_sends_the_player_back_to_the_lobby){
  hall h;
  h.connect(1);
  h.connect(2);
  int id = h.create(1);
  h.join(1, "Ann", id);
  h.join(2, "Bob", id);
  h.send(1, {{"type", "kick"}, {"seat", 1}});
  CHECK(h.last(2, "left").value("reason", "") == "kicked");
  CHECK_EQ(h.l.table_of(2), -1);
  CHECK(!h.last(2, "lobby").is_null());
}

TEST(an_empty_table_closes_after_ten_minutes_but_not_the_main_one){
  hall h;
  h.connect(1);
  h.connect(2);
  int id = h.create(1);
  h.join(1, "Ann", id);
  h.join(2, "Bob", lobby::MAIN);
  h.l.tick();
  h.now += 20 * 60 * 1000;
  h.l.tick();
  CHECK_EQ(h.l.table_count(), 2);                // Ann is there
  h.send(1, {{"type", "leave"}});
  h.send(2, {{"type", "leave"}});
  h.l.tick();
  h.now += 599 * 1000;
  h.l.tick();
  CHECK_EQ(h.l.table_count(), 2);
  h.now += 1000;
  h.l.tick();
  CHECK_EQ(h.l.table_count(), 1);
  CHECK(h.l.find(lobby::MAIN) != nullptr);
  CHECK_EQ(h.tables(1).size(), 1);               // the lobby heard
  h.join(1, "Ann", id);
  CHECK(h.error_code(1) == "no_table");
}

TEST(the_console_kicks_by_name_and_table){
  hall h;
  h.connect(1);
  h.connect(2);
  int id = h.create(1);
  h.join(1, "Ann", id);
  h.join(2, "Ann", lobby::MAIN);
  std::string error;
  CHECK(!h.l.kick("Ann", 0, error));
  CHECK(error.find("more than one table") != std::string::npos);
  CHECK(h.l.kick("Ann", id, error));
  CHECK_EQ(h.l.table_of(1), -1);
  CHECK_EQ(h.l.table_of(2), lobby::MAIN);
  CHECK(!h.l.kick("Nobody", 0, error));
  CHECK(h.l.list().find("Friday") != std::string::npos);
  CHECK(h.l.roster().find("Main table") != std::string::npos);
}

TEST(the_host_closes_their_table_and_everyone_goes_back_to_the_lobby){
  hall h;
  for(int c : {1, 2, 3}) h.connect(c);
  int id = h.create(1);
  h.join(1, "Ann", id);
  h.join(2, "Bob", id);
  h.join(3, "Cid", lobby::MAIN);
  h.send(1, {{"type", "add_bot"}, {"count", 2}});
  h.send(1, {{"type", "start"}});               // a hand is being played: it is dropped
  CHECK(h.l.find(id)->get_stage() != WAITING);

  h.send(2, {{"type", "close_table"}});
  CHECK(h.error_code(2) == "not_host");
  CHECK(h.last(2, "error").value("message", "").find("Ann") != std::string::npos);
  h.send(3, {{"type", "close_table"}});
  CHECK(h.error_code(3) == "main_table");

  int lists = h.count(2, "lobby");
  h.send(1, {{"type", "close_table"}});
  CHECK(h.l.find(id) == nullptr);
  for(int c : {1, 2}){
    json left = h.last(c, "left");
    CHECK(left.value("reason", "") == "closed");
    CHECK(left.value("by", "") == "Ann");
    CHECK_EQ(h.l.table_of(c), -1);
    CHECK_EQ(h.tables(c).size(), 1);           // back in the lobby, without that table
  }
  CHECK_EQ(h.count(2, "lobby"), lists + 1);      // the list once, not twice
  CHECK_EQ(h.l.table_of(3), lobby::MAIN);        // the main table plays on
  // In the lobby, there is no table to close.
  h.send(1, {{"type", "close_table"}});
  CHECK(h.error_code(1) == "join_first");
}

TEST(the_console_closes_any_table_but_the_main_one){
  hall h;
  h.connect(1);
  int id = h.create(1);
  h.join(1, "Ann", id);
  std::string error;
  CHECK(!h.l.close(lobby::MAIN, error));
  CHECK(error.find("main table") != std::string::npos);
  CHECK(!h.l.close(99, error));
  CHECK(error.find("no table 99") != std::string::npos);
  CHECK(h.l.close(id, error));
  CHECK(h.last(1, "left").value("reason", "") == "closed");
  CHECK(!h.last(1, "left").contains("by"));      // the server, not a person
  CHECK_EQ(h.l.table_count(), 1);
}

TEST(hands_are_saved_with_their_table){
  hall h;
  for(int c = 1; c <= 2; c++) h.connect(c);
  int id = h.create(1);
  h.join(1, "Ann", id);
  h.join(2, "Bob", id);
  h.send(1, {{"type", "start"}});
  table& t = *h.l.find(id);
  for(int guard = 0; guard < 50 && t.get_stage() != WAITING; guard++){
    int s = t.get_turn();
    h.send(s == 0 ? 1 : 2, {{"type", "action"}, {"action", "fold"}});
  }
  CHECK(t.get_stage() == WAITING);
  CHECK_EQ(t.last_hand()["table"].value("id", 0), id);
  CHECK(t.last_hand()["table"].value("name", "") == "Friday");
}
