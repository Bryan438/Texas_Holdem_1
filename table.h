#ifndef _TABLE_H_
#define _TABLE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "third_party/json.hpp"
#include "observer.h"
#include "player.h"
#include "card.h"
#include "deck_of_card.h"

using json = nlohmann::json;

#define MAX_PLAYERS 12

struct table_config{
  int starting_stack = 200;
  int small_blind = 1;
  int big_blind = 2;
  int turn_seconds = 300;  // 5 minutes; 0 = no time limit
  int next_hand_seconds = 0; // 0 = a player starts each hand with "start";
                             // N = start the next hand by itself after N seconds
  bool log_events = true;  // print events on the server console
};

enum stage{
  WAITING = 0,  // no hand running; any seated player can send "start",
                // or the next one starts by itself after next_hand_seconds
  PREFLOP,
  FLOP,
  TURN,
  RIVER
};

struct hand_result_entry{
  int seat;
  int amount;
  std::string hand;  // hand name if there was a showdown
};

// Client -> server messages (one JSON object per line):
//   {"type":"join","name":"Bryan"}
//   {"type":"join","name":"Bryan","token":"..."}   reconnect to a seat after a disconnect
//   {"type":"start"}
//   {"type":"action","action":"fold|check|call|raise","amount":20}
//     amount = total bet for this round after the raise ("raise to")
//
// Server -> client messages:
//   {"type":"welcome","seat":0,"name":"Bryan","token":"..."}
//     keep the token: it is needed to take the seat back after a disconnect
//   {"type":"state", ...}   full table as this player may see it, after every change
//   {"type":"event","text":"Bob raises to 20"}
//   {"type":"error","message":"..."}
class table: public observer{
  public:
    using clock_fn = std::function<int64_t()>;  // milliseconds, monotonic

    table(sender* out, table_config config = table_config(), clock_fn clock = nullptr);
    ~table();

    // observer
    void on_connect(int client) override;
    void on_message(int client, const std::string& line) override;
    void on_disconnect(int client) override;

    // Call regularly (e.g. every 500 ms): enforces the turn time limit and
    // starts the next hand when its countdown runs out.
    void tick();

    // Read-only view, used by tests
    stage get_stage() const { return current_stage; }
    int get_turn() const { return turn; }
    int get_dealer() const { return dealer; }
    int get_current_bet() const { return current_bet; }
    int get_pot() const;
    int get_min_raise_to() const;
    int seat_count() const { return seats.size(); }
    const player& seat(int i) const { return *seats[i]; }
    int seat_of(int client) const;
    int hand_number() const { return hand_count; }
    int board_size() const { return board_count; }

    // Set a seat's chips between hands (tests; could back an admin command).
    void set_stack(int seat, int chips){ if(current_stage == WAITING) seats[seat]->stack = chips; }

  private:
    sender* out;
    table_config config;
    clock_fn now_ms;
    deck_of_card* deck = new deck_of_card();

    std::vector<std::unique_ptr<player>> seats;
    const Card* board[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    int board_count = 0;
    int card_index = 0;

    stage current_stage = WAITING;
    int hand_count = 0;
    int dealer = -1;
    int small_blind_seat = -1;
    int big_blind_seat = -1;
    int turn = -1;
    int64_t turn_deadline = 0;
    int64_t next_hand_at = 0;           // 0 = no automatic start pending
    int current_bet = 0;
    int last_raise_size = 0;
    bool reveal_cards = false;          // showdown: everyone sees hands still in
    std::vector<hand_result_entry> last_result;

    // Message handlers
    void handle_join(int client, const json& msg);
    void handle_start(int client);
    void handle_action(int client, const json& msg);

    // Game flow
    bool start_hand();
    bool apply_action(int seat, const std::string& action, int amount, std::string& error);
    void continue_hand(int from);
    void deal_next_street();
    void finish_hand();
    void begin_turn(int seat);
    void fold_seat(int seat);

    int next_seat(int from, const std::function<bool(const player&)>& pred) const;
    bool needs_to_act(const player& p) const;
    int count_in_hand() const;
    int count_can_act() const;
    int to_call(const player& p) const;

    // Output
    json state_for(int viewer) const;
    void broadcast_state();
    void event(const std::string& text);
    void send(int client, const json& msg);
    void send_error(int client, const std::string& message);
};

#endif
