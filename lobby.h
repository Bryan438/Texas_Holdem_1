#ifndef _LOBBY_H_
#define _LOBBY_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include "table.h"

struct lobby_limits{
  int max_tables = 20;          // tables open at once, the main table included
  int idle_close_seconds = 600; // a table nobody is at closes after this long (not the main one)
};

// Several tables behind one server. A connection starts in the lobby: it sees
// the list of tables, may open a new one, and sits down at one table at a
// time. Everything it sends while seated goes to that table, which runs the
// game exactly as on its own; getting up (leave, kick) brings it back here.
//
// Lobby messages (client -> server):
//   {"type":"list_tables"}
//   {"type":"create_table","name":"Friday","blinds":[2,5],"chips":500,"password":"..."}
//       blinds one of 1/2, 2/5, 5/10; chips 20 to 500 big blinds (default 100);
//       password optional (a private table). Answer: {"type":"table_created","table":3}
//   {"type":"join","table":3,"name":"Ann","password":"...","token":"..."}
//       without "table": the main table, so older clients and thbot still work
//   {"type":"close_table"}   the host closes the table they sit at (not the main
//       one): everyone there gets {"type":"left","reason":"closed","by":"Ann"}
//       ("by" is left out when the server closed it)
//       and is back in the lobby; a hand being played is dropped
// Server -> client: {"type":"lobby","tables":[...],"max_tables":20,"can_create":true},
// sent on connecting, on getting up from a table, and whenever the list changes.
class lobby: public observer{
  public:
    using clock_fn = table::clock_fn;

    lobby(sender* out, table_config base, lobby_limits limits = lobby_limits(), clock_fn clock = nullptr);

    void on_connect(int client) override;
    void on_message(int client, const std::string& line) override;
    void on_disconnect(int client) override;
    void tick();

    // The server's console.
    std::string list() const;
    std::string roster() const;
    bool kick(const std::string& name, int table_id, std::string& error);
    bool close(int table_id, std::string& error);

    // Tests.
    int table_count() const { return (int)tables.size(); }
    table* find(int id);
    int table_of(int client) const;   // -1 = in the lobby
    static const int MAIN = 1;

  private:
    struct entry{
      int id = 0;
      std::string password;           // "" = anyone may sit down
      bool permanent = false;
      int64_t empty_since = 0;        // nobody here since (lobby clock), 0 = someone is
      std::unique_ptr<table> game;
      json shown;                     // what the lobby list last said about it
    };

    sender* out;
    table_config base;
    lobby_limits limits;
    clock_fn now_ms;
    std::map<int, entry> tables;      // by id: oldest first
    int next_id = MAIN;
    std::set<int> clients;            // every connection
    std::map<int, int> seated_at;     // connection -> table id

    int open_table(const std::string& name, int small_blind, int big_blind, int chips,
                   const std::string& password, bool permanent);
    // by: the person who closed it ("" = the server); why: for the server's log.
    void close_table(int id, const std::string& by = "", const std::string& why = "nobody was at it");
    void handle_close(int client, int id);
    void handle_create(int client, const json& msg);
    void handle_join(int client, const json& msg);
    void sync(int id);
    json describe(const entry& e) const;
    json lobby_message() const;
    void broadcast_lobby();
    void send(int client, const json& msg);
    void send_error(int client, const std::string& message, const std::string& code);
};

#endif
