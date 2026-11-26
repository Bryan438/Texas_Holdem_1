// Talking at the table: messages and quick phrases, what is turned away (too
// long, too many, from a bot), the last lines for newcomers, the host's mute.
#include <string>
#include "test_framework.h"
#include "table_harness.h"
#include "lobby.h"

namespace {

struct talk: harness{
  explicit talk(int players = 3) : harness(players){}
  void say(int c, const std::string& text){ send(c, {{"type", "chat"}, {"text", text}}); }
  std::string error_code(int c){ return last(c, "error").value("code", ""); }
  int count(int c, const std::string& type){
    int n = 0;
    for(const json& m : out.got[c]) n += m["type"] == type;
    return n;
  }
  json state(int c){ return last(c, "state"); }
};

std::string repeat(const std::string& piece, int times){
  std::string s;
  for(int i = 0; i < times; i++) s += piece;
  return s;
}

}

TEST(everyone_at_the_table_hears_what_is_said){
  talk h;
  h.say(harness::client(0), "  hello there ");
  for(int seat = 0; seat < 3; seat++){
    json line = h.last(harness::client(seat), "chat");
    CHECK(line.value("name", "") == "P0");
    CHECK(line.value("text", "") == "hello there");
    CHECK(!line.contains("phrase"));
  }
}

TEST(a_message_is_one_line_of_at_most_200_characters){
  talk h;
  int c = harness::client(1);
  h.say(c, "two\nlines");
  CHECK(h.last(c, "chat").value("text", "") == "two lines");
  h.say(c, " \t ");
  CHECK(h.error_code(c) == "chat_empty");
  h.say(c, repeat("a", 201));
  CHECK(h.error_code(c) == "chat_long");
  h.now += 20000;
  // Characters, not bytes: 200 Chinese characters are 600 bytes and fine.
  h.say(c, repeat("好", 200));
  CHECK(h.last(c, "chat").value("text", "") == repeat("好", 200));
  CHECK_EQ(h.count(harness::client(0), "chat"), 2);
}

TEST(a_quick_phrase_comes_with_its_id_and_english_words){
  talk h;
  h.send(harness::client(2), {{"type", "chat"}, {"phrase", "nice_hand"}});
  json line = h.last(harness::client(0), "chat");
  CHECK(line.value("phrase", "") == "nice_hand");
  CHECK(line.value("text", "") == "Nice hand!");
  h.send(harness::client(2), {{"type", "chat"}, {"phrase", "rude_word"}});
  CHECK(h.error_code(harness::client(2)) == "bad_phrase");
}

TEST(at_most_five_messages_in_ten_seconds){
  talk h;
  int c = harness::client(0);
  for(int i = 0; i < 5; i++){
    h.say(c, "msg " + std::to_string(i));
  }
  CHECK_EQ(h.count(harness::client(1), "chat"), 5);
  h.say(c, "one too many");
  CHECK(h.error_code(c) == "chat_fast");
  CHECK_EQ(h.count(harness::client(1), "chat"), 5);
  h.now += 10000;
  h.say(c, "calm again");
  CHECK_EQ(h.count(harness::client(1), "chat"), 6);
  // Each person has their own allowance.
  h.say(harness::client(1), "me too");
  CHECK_EQ(h.count(harness::client(0), "chat"), 7);
}

TEST(someone_who_sits_down_later_gets_the_last_50_lines){
  talk h(2);
  for(int i = 0; i < 55; i++){
    h.now += 2000;
    h.say(harness::client(i % 2), "line " + std::to_string(i));
  }
  h.join(301, "Ann");
  // The history comes right after the welcome.
  std::vector<json>& got = h.out.got[301];
  size_t w = 0;
  while(w < got.size() && got[w]["type"] != "welcome") w++;
  CHECK(w + 1 < got.size() && got[w + 1]["type"] == "chat_history");
  json lines = h.last(301, "chat_history")["messages"];
  CHECK_EQ(lines.size(), 50);
  CHECK(lines[0].value("text", "") == "line 5");
  CHECK(lines[49].value("text", "") == "line 54");
  // Coming back after a disconnect brings it again.
  std::string token = h.token_of(301);
  h.t.on_disconnect(301);
  h.join(302, "Ann", token);
  CHECK_EQ(h.last(302, "chat_history")["messages"].size(), 50);
}

TEST(bots_do_not_chat){
  talk h(1);
  h.t.on_connect(201);
  h.send(201, {{"type", "join"}, {"name", "Robo"}, {"bot", true}});
  h.say(201, "beep");
  CHECK(h.error_code(201) == "bots_cannot");
  CHECK_EQ(h.count(harness::client(0), "chat"), 0);
}

TEST(the_host_mutes_and_unmutes_someone){
  talk h;
  int host = harness::client(0), p1 = harness::client(1), p2 = harness::client(2);
  h.send(p1, {{"type", "mute"}, {"seat", 2}});
  CHECK(h.error_code(p1) == "not_host");
  h.send(host, {{"type", "mute"}, {"seat", 0}});
  CHECK(h.error_code(host) == "mute_self");
  h.send(host, {{"type", "mute"}, {"seat", 2}});
  CHECK(h.state(p1)["seats"][2].value("muted", false));
  CHECK(!h.state(p1)["seats"][1].contains("muted"));
  CHECK(h.got_event(p2, "P2 was muted by P0"));

  h.say(p2, "you cannot hear me");
  CHECK(h.error_code(p2) == "muted");
  CHECK_EQ(h.count(host, "chat"), 0);
  CHECK_EQ(h.count(p2, "chat"), 0);

  // Getting up and sitting down again does not undo it.
  std::string token = h.token_of(p2);
  h.send(p2, {{"type", "leave"}});
  h.join(303, "P2", token);
  CHECK_EQ(h.t.seat_of(303), 2);
  h.say(303, "still?");
  CHECK(h.error_code(303) == "muted");

  h.send(host, {{"type", "mute"}, {"seat", 2}, {"muted", false}});
  CHECK(!h.state(p1)["seats"][2].contains("muted"));
  CHECK(h.got_event(p1, "P2 was unmuted by P0"));
  h.say(303, "back");
  CHECK(h.last(host, "chat").value("text", "") == "back");
}

TEST(chat_stays_at_its_table){
  fake_sender out;
  int64_t now = 0;
  lobby l(&out, quiet_config(), lobby_limits(), [&]{ return now; });
  auto send = [&](int c, const json& m){ l.on_message(c, m.dump()); };
  for(int c : {1, 2, 3}) l.on_connect(c);
  send(1, {{"type", "join"}, {"name", "Ann"}});
  send(2, {{"type", "create_table"}, {"name", "Back room"}});
  send(2, {{"type", "join"}, {"name", "Bob"}, {"table", 2}});
  send(3, {{"type", "chat"}, {"text", "from the lobby"}});
  send(1, {{"type", "chat"}, {"text", "main table only"}});
  auto chats = [&](int c){
    int n = 0;
    for(const json& m : out.got[c]) n += m["type"] == "chat";
    return n;
  };
  CHECK_EQ(chats(1), 1);
  CHECK_EQ(chats(2), 0);
  CHECK_EQ(chats(3), 0);
  CHECK(out.got[3].back().value("code", "") == "join_first");
}
