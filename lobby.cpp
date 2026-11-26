#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
#include "lobby.h"

static int64_t steady_ms(){
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// The blinds a new table may have.
static const int BLIND_CHOICES[][2] = {{1, 2}, {2, 5}, {5, 10}};

lobby::lobby(sender* o, table_config b, lobby_limits l, clock_fn clock)
  : out(o), base(b), limits(l), now_ms(clock ? clock : steady_ms){
  std::string name = base.table_name.empty() ? "Main table" : base.table_name;
  open_table(name, base.small_blind, base.big_blind, base.starting_stack, "", true);
}

int lobby::open_table(const std::string& name, int small_blind, int big_blind, int chips,
                      const std::string& password, bool permanent){
  int id = next_id++;
  table_config c = base;
  c.table_id = id;
  c.table_name = name;
  c.small_blind = small_blind;
  c.big_blind = big_blind;
  c.starting_stack = chips;
  if(c.seed != 0){
    c.seed += id - 1;   // tests: every table its own, still repeatable, decks
  }
  entry& e = tables[id];
  e.id = id;
  e.password = password;
  e.permanent = permanent;
  e.game.reset(new table(out, c, now_ms));
  e.shown = describe(e);
  return id;
}

table* lobby::find(int id){
  auto it = tables.find(id);
  return it == tables.end() ? nullptr : it->second.game.get();
}

int lobby::table_of(int client) const{
  auto it = seated_at.find(client);
  return it == seated_at.end() ? -1 : it->second;
}

// ---------------------------------------------------------------- connections

void lobby::on_connect(int client){
  clients.insert(client);
  send(client, {{"type", "hello"}, {"lobby", true},
                {"message", "Send {\"type\":\"join\",\"name\":\"<your name>\"} to sit down at the main "
                            "table, or pick a table from the list"}});
  send(client, lobby_message());
}

void lobby::on_message(int client, const std::string& line){
  json msg = json::parse(line, nullptr, false);
  if(msg.is_discarded() || !msg.is_object() || !msg.contains("type") || !msg["type"].is_string()){
    send_error(client, "bad message: expected a JSON object with a \"type\" field", "bad_message");
    return;
  }
  std::string type = msg["type"].get<std::string>();
  if(type == "list_tables"){
    send(client, lobby_message());
    return;
  }

  int at = table_of(client);
  if(at >= 0){
    if(type == "create_table"){
      send_error(client, "leave your table first", "at_table");
      return;
    }
    if(type == "close_table"){
      handle_close(client, at);
      return;
    }
    // Everything else is the table's business, a second join included (it says so).
    tables[at].game->on_message(client, line);
    sync(at);
    return;
  }

  if(type == "create_table"){
    handle_create(client, msg);
  }
  else if(type == "join"){
    handle_join(client, msg);
  }
  else{
    send_error(client, "sit down at a table first", "join_first");
  }
}

void lobby::on_disconnect(int client){
  clients.erase(client);
  int at = table_of(client);
  if(at < 0){
    return;
  }
  seated_at.erase(client);
  // The table keeps the seat for them to come back to, as it always has.
  tables[at].game->on_disconnect(client);
  sync(at);
}

// ---------------------------------------------------------------- lobby requests

static bool printable_name(std::string& name){
  name.erase(0, name.find_first_not_of(' '));
  name.erase(name.find_last_not_of(' ') + 1);
  bool printable = std::all_of(name.begin(), name.end(), [](char c){ return (unsigned char)c >= 0x20; });
  return !name.empty() && name.size() <= 24 && printable;
}

void lobby::handle_create(int client, const json& msg){
  if((int)tables.size() >= limits.max_tables){
    send_error(client, "the lobby is full: at most " + std::to_string(limits.max_tables) + " tables",
               "lobby_full");
    return;
  }
  std::string name = msg.contains("name") && msg["name"].is_string() ? msg["name"].get<std::string>() : "";
  if(!printable_name(name)){
    send_error(client, "a table's name must be 1-24 printable characters", "table_name");
    return;
  }
  int small_blind = 1, big_blind = 2;
  if(msg.contains("blinds")){
    const json& b = msg["blinds"];
    bool known = false;
    if(b.is_array() && b.size() == 2 && b[0].is_number_integer() && b[1].is_number_integer()){
      for(const auto& choice : BLIND_CHOICES){
        if(b[0].get<int>() == choice[0] && b[1].get<int>() == choice[1]){
          small_blind = choice[0];
          big_blind = choice[1];
          known = true;
        }
      }
    }
    if(!known){
      send_error(client, "blinds must be 1/2, 2/5 or 5/10", "bad_blinds");
      return;
    }
  }
  int chips = 100 * big_blind;
  if(msg.contains("chips")){
    if(!msg["chips"].is_number_integer() || msg["chips"].get<int>() < 20 * big_blind ||
       msg["chips"].get<int>() > 500 * big_blind){
      send_error(client, "starting chips must be 20 to 500 big blinds (" + std::to_string(20 * big_blind) +
                         " to " + std::to_string(500 * big_blind) + ")", "bad_chips");
      return;
    }
    chips = msg["chips"].get<int>();
  }
  std::string password = msg.contains("password") && msg["password"].is_string()
                           ? msg["password"].get<std::string>() : "";
  if(password.size() > 32){
    send_error(client, "a password can be at most 32 characters", "bad_password");
    return;
  }

  int id = open_table(name, small_blind, big_blind, chips, password, false);
  if(base.log_events) printf("[lobby] table %d \"%s\" opened (blinds %d/%d, %d chips%s)\n", id, name.c_str(),
         small_blind, big_blind, chips, password.empty() ? "" : ", private");
  send(client, {{"type", "table_created"}, {"table", id}});
  broadcast_lobby();
}

void lobby::handle_join(int client, const json& msg){
  int id = MAIN;
  if(msg.contains("table")){
    if(!msg["table"].is_number_integer()){
      send_error(client, "\"table\" must be a table's number", "no_table");
      return;
    }
    id = msg["table"].get<int>();
  }
  auto it = tables.find(id);
  if(it == tables.end()){
    send_error(client, "there is no table " + std::to_string(id) + " (it may have closed)", "no_table");
    send(client, lobby_message());
    return;
  }
  entry& e = it->second;
  if(!e.password.empty()){
    std::string given = msg.contains("password") && msg["password"].is_string()
                          ? msg["password"].get<std::string>() : "";
    if(given != e.password){
      send_error(client, given.empty() ? "this table is private: it needs a password" : "wrong password",
                 "bad_password");
      return;
    }
  }
  seated_at[client] = id;
  e.game->on_message(client, msg.dump());
  sync(id);   // a join the table refused (name taken, full) leaves them in the lobby
}

// After the table had its say: whoever no longer has a seat there is back in
// the lobby, and the list is sent again if what it shows changed.
void lobby::sync(int id){
  auto it = tables.find(id);
  if(it == tables.end()){
    return;
  }
  entry& e = it->second;
  std::vector<int> gone;
  for(const auto& [client, at] : seated_at){
    if(at == id && e.game->seat_of(client) < 0){
      gone.push_back(client);
    }
  }
  for(int client : gone){
    seated_at.erase(client);
  }
  json now = describe(e);
  if(now != e.shown){
    e.shown = now;
    broadcast_lobby();          // reaches those who just got up too
  }
  else{
    for(int client : gone){
      send(client, lobby_message());
    }
  }
}

// ---------------------------------------------------------------- the clock

void lobby::tick(){
  std::vector<int> ids;
  for(const auto& [id, e] : tables){
    ids.push_back(id);
  }
  for(int id : ids){
    entry& e = tables[id];
    e.game->tick();
    sync(id);
    if(e.permanent){
      continue;
    }
    // A table with nobody at it (people, that is) closes after a while.
    if(e.game->people_here() > 0){
      e.empty_since = 0;
    }
    else if(e.empty_since == 0){
      e.empty_since = now_ms();
    }
    else if(now_ms() - e.empty_since >= limits.idle_close_seconds * 1000LL){
      close_table(id);
    }
  }
}

// The host is done with their table: everyone there goes back to the lobby.
void lobby::handle_close(int client, int id){
  entry& e = tables[id];
  table& t = *e.game;
  const player& p = t.seat(t.seat_of(client));
  if(p.bot){
    send_error(client, "bots cannot close a table", "bots_cannot");
  }
  else if(e.permanent){
    send_error(client, "the main table stays open", "main_table");
  }
  else if(t.host_name() != p.name){
    send_error(client, "only the host (" + t.host_name() + ") can close the table", "not_host");
  }
  else{
    close_table(id, p.name, "by its host, " + p.name);
  }
}

void lobby::close_table(int id, const std::string& by, const std::string& why){
  entry& e = tables[id];
  if(base.log_events){
    printf("[lobby] table %d \"%s\" closed: %s\n", id, e.game->settings().table_name.c_str(), why.c_str());
  }
  // Bots that joined over the network may still be sitting there.
  std::vector<int> seated;
  for(const auto& [client, at] : seated_at){
    if(at == id) seated.push_back(client);
  }
  json left = {{"type", "left"}, {"reason", "closed"}};
  if(!by.empty()){
    left["by"] = by;
  }
  tables.erase(id);   // first, so the list each of them gets no longer has it
  for(int client : seated){
    seated_at.erase(client);
    send(client, left);
  }
  broadcast_lobby();  // to everyone in the lobby, those just sent back included
}

// The server's console: close a table, whoever is at it.
bool lobby::close(int table_id, std::string& error){
  auto it = tables.find(table_id);
  if(it == tables.end()){
    error = "there is no table " + std::to_string(table_id) + " (type tables to see them)";
    return false;
  }
  if(it->second.permanent){
    error = "the main table stays open";
    return false;
  }
  close_table(table_id, "", "from the console");
  return true;
}

// ---------------------------------------------------------------- the list

json lobby::describe(const entry& e) const{
  const table& t = *e.game;
  const table_config& c = t.settings();
  return {{"id", e.id}, {"name", c.table_name}, {"private", !e.password.empty()},
          {"main", e.permanent}, {"blinds", {c.small_blind, c.big_blind}}, {"chips", c.starting_stack},
          {"seats", t.seat_count()}, {"people", t.people_seated()}, {"max", MAX_PLAYERS},
          {"playing", t.get_stage() != WAITING}, {"host", t.host_name()}};
}

json lobby::lobby_message() const{
  json list = json::array();
  for(const auto& [id, e] : tables){
    list.push_back(e.shown);
  }
  return {{"type", "lobby"}, {"tables", list}, {"max_tables", limits.max_tables},
          {"can_create", (int)tables.size() < limits.max_tables}};
}

// Only to those in the lobby: people at a table have the table to look at.
void lobby::broadcast_lobby(){
  json msg = lobby_message();
  for(int client : clients){
    if(!seated_at.count(client)){
      send(client, msg);
    }
  }
}

void lobby::send(int client, const json& msg){
  out->send_line(client, msg.dump(-1, ' ', false, json::error_handler_t::replace));
}

void lobby::send_error(int client, const std::string& message, const std::string& code){
  send(client, {{"type", "error"}, {"message", message}, {"code", code}});
}

// ---------------------------------------------------------------- console

std::string lobby::list() const{
  std::string text;
  for(const auto& [id, e] : tables){
    const table& t = *e.game;
    const table_config& c = t.settings();
    char line[200];
    snprintf(line, sizeof(line), "  %2d  %-24s %d/%-3d %5d chips  %2d people, %2d seats  %-8s host %s%s\n",
             id, c.table_name.c_str(), c.small_blind, c.big_blind, c.starting_stack,
             t.people_seated(), t.seat_count(), t.get_stage() != WAITING ? "playing" : "waiting",
             t.host_name().empty() ? "-" : t.host_name().c_str(), e.password.empty() ? "" : "  [private]");
    text += line;
  }
  return text;
}

std::string lobby::roster() const{
  std::string text;
  for(const auto& [id, e] : tables){
    text += "Table " + std::to_string(id) + " \"" + e.game->settings().table_name + "\":\n";
    text += e.game->roster();
  }
  return text;
}

bool lobby::kick(const std::string& name, int table_id, std::string& error){
  std::vector<int> found;
  for(const auto& [id, e] : tables){
    if(table_id > 0 && id != table_id) continue;
    const table& t = *e.game;
    for(int i = 0; i < t.seat_count(); i++){
      if(t.seat(i).name == name && !t.seat(i).leaving){
        found.push_back(id);
        break;
      }
    }
  }
  if(found.empty()){
    error = "nobody called \"" + name + "\" is seated" +
            (table_id > 0 ? " at table " + std::to_string(table_id) : "") + " (type players to see who is)";
    return false;
  }
  if(found.size() > 1){
    error = "\"" + name + "\" sits at more than one table: kick " + name + " TABLE, e.g. kick " + name + " " +
            std::to_string(found[0]);
    return false;
  }
  bool done = tables[found[0]].game->kick(name, error);
  sync(found[0]);
  return done;
}
