#ifndef _TABLE_H_
#define _TABLE_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "third_party/json.hpp"
#include "observer.h"
#include "player.h"
#include "card.h"
#include "deck_of_card.h"
#include "opponent_stats.h"

using json = nlohmann::json;

#define MAX_PLAYERS 12

struct table_config{
  int starting_stack = 200;
  int small_blind = 1;
  int big_blind = 2;
  int turn_seconds = 300;  // 5 minutes; 0 = no time limit
  int next_hand_seconds = 0; // 0 = a player starts each hand with "start";
                             // N = start the next hand by itself after N seconds
  int bot_delay_ms = 1000;  // a bot the server runs waits this long before each move
  int disconnect_grace_seconds = 30; // a player who disconnects during a hand keeps their
                                     // cards this long before folding (0 = folds at once)
  bool log_events = true;  // print events on the server console
  std::string history_path; // append every finished hand here as one JSON line ("" = off)
  unsigned seed = 0;       // 0 = shuffle at random; N = the same decks every run (bot matches, tests)
  int table_id = 0;        // in a lobby with several tables: which one (saved with each hand)
  std::string table_name;
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
//   {"type":"join","name":"Robo","bot":true}       a program, not a person (thbot says so)
//   {"type":"start"}
//   {"type":"action","action":"fold|check|call|raise","amount":20}
//     amount = total bet for this round after the raise ("raise to")
//   {"type":"end_game"}   stop, show who is up and down
//   {"type":"new_game"}   everyone back to the starting chips, hand 1 next
//     Both take effect only once every person connected agrees: the first one
//     opens a vote (at once for a person alone with bots). Once the game is
//     over, new_game needs no vote.
//   {"type":"vote","agree":true}   answer the open vote; one "false" drops it
//   {"type":"leave"}      get up: fold now if in a hand, the seat goes when it is over.
//                         The chips are kept for this game: joining again with the
//                         same name and token brings them back.
//   {"type":"rebuy"}      out of chips: buy in again for the starting chips
//   {"type":"kick","seat":2}   the host removes someone from the table
//   The host is the first person to sit down; when they leave, or stay away
//   longer than the grace period, the next person in seat order takes over.
//   Only the host deals ("start") and adds or removes bots. With no person at
//   the table (bots only) anyone may deal.
//   {"type":"chat","text":"nice hand"}   say something to everyone at the table
//   {"type":"chat","phrase":"gg"}        a quick phrase: good_luck, nice_hand,
//                                        well_played, thinking, wow, gg
//     Up to 200 characters, at most 5 messages in 10 seconds; bots do not chat.
//   {"type":"mute","seat":2,"muted":true}   the host stops (or, false, lets again)
//                                           someone's messages reach the table
//   {"type":"add_bot","style":"rl","count":2}   a person seats bots that the server runs
//   {"type":"remove_bot","seat":3}               between hands, one of those bots leaves
//
// Server -> client messages:
//   {"type":"welcome","seat":0,"name":"Bryan","token":"..."}
//     keep the token: it is needed to take the seat back after a disconnect
//   {"type":"state", ...}   full table as this player may see it, after every change
//   {"type":"event","text":"Bob raises to 20","kind":"raise","name":"Bob","amount":20,"allin":false}
//     text is ready to print; the other fields say the same for clients that
//     word the line themselves (the web client, in two languages)
//   {"type":"error","message":"...","code":"not_your_turn"}
//   {"type":"hand_summary", ...}  the hand that just ended, as this player may see it
//   {"type":"left","reason":"left|kicked"}  you are no longer seated (join again to play)
//   {"type":"chat","name":"Ann","text":"nice hand"}   someone at the table said it;
//     a quick phrase also has "phrase":"nice_hand" (text is then its English words)
//   {"type":"chat_history","messages":[...]}  right after "welcome": the table's
//     last 50 chat messages, oldest first
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
    // Full record of the last finished hand, including every hole card.
    const json& last_hand() const { return last_record; }

    // The server's own commands (typed into thserver). kick: false and why
    // when there is nobody of that name.
    bool kick(const std::string& name, std::string& error);
    std::string roster() const;

    // For the lobby's list of tables.
    const std::string& host_name() const { return host; }
    int people_seated() const;   // people (not bots) with a seat, here or not
    int people_here() const;     // people connected right now
    const table_config& settings() const { return config; }

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
    bool game_over = false;             // ended by a player: nothing is dealt until a new game
    int64_t bot_move_at = 0;            // when the house bot whose turn it is moves
    std::mt19937 bot_rng;               // the house bots' dice
    opponent_tracker house_reads;       // what the house bots have seen of everyone (public actions only)
    std::vector<hand_result_entry> last_result;
    json record;                        // the hand being played (see hand history)
    json last_record;                   // the last finished hand

    // A proposal to end or restart the game while other people are here:
    // it passes once every connected person agrees.
    std::string host;                   // name of the person who deals; "" = nobody
    // People who left during this game, with what they had: they get it back
    // if they return (with their token), and the standings still count them.
    struct departed_player{
      std::string name, token;
      int stack, buy_in;
      bool bot;
    };
    std::vector<departed_player> departed;

    // What was said at the table: the last CHAT_KEEP lines for people who sit
    // down later, when each person last spoke (to slow down a flood), and who
    // the host has muted (by name, so getting up and back does not undo it).
    std::deque<json> chat_log;
    std::map<std::string, std::deque<int64_t>> chat_times;
    std::set<std::string> muted;
    static constexpr int CHAT_KEEP = 50;
    static constexpr int CHAT_MAX_CHARS = 200;
    static constexpr int CHAT_BURST = 5;            // messages ...
    static constexpr int CHAT_WINDOW_MS = 10000;    // ... in this long

    std::string vote_kind;              // "" = none, "end" or "new"
    std::string vote_by;
    std::vector<std::string> vote_yes;
    int64_t vote_deadline = 0;
    static constexpr int VOTE_SECONDS = 60;

    // Message handlers
    void handle_join(int client, const json& msg);
    void handle_start(int client);
    void handle_action(int client, const json& msg);
    void handle_end_game(int client);
    void handle_new_game(int client);
    void handle_add_bot(int client, const json& msg);
    void handle_remove_bot(int client, const json& msg);
    void handle_vote(int client, const json& msg);
    void handle_kick(int client, const json& msg);
    void handle_rebuy(int client);
    void handle_chat(int client, const json& msg);
    void handle_mute(int client, const json& msg);
    void welcome(int client, int seat, const std::string& name, const std::string& token);
    bool leads(int seat, int client, const std::string& what);
    bool update_host();

    // Game flow
    bool start_hand();
    bool apply_action(int seat, const std::string& action, int amount, std::string& error,
                      const std::string& reason = "");
    void record_action(int seat, const std::string& action, int amount, int to, const std::string& reason);
    void save_record();
    json summary_for(int viewer) const;
    void continue_hand(int from);
    void deal_next_street();
    void finish_hand();
    void begin_turn(int seat);
    void fold_seat(int seat);
    bool fold_absent(int seat);
    void cancel_hand();
    void reset_game(const std::string& by, bool voted = false);
    void end_game(const std::string& by, bool voted);
    void propose(int seat, const std::string& kind);
    bool check_vote();
    bool votes(const player& p) const;
    void clear_vote();
    void remove_player(int seat, bool kicked, const std::string& by = "");
    void erase_seat(int seat);
    void purge_leavers();
    bool alone_with_bots(int seat) const;
    json standings() const;
    int take_seat();
    std::string house_bot_name(const std::string& style) const;
    void play_house_bot(int seat);

    int next_seat(int from, const std::function<bool(const player&)>& pred) const;
    bool needs_to_act(const player& p) const;
    int count_in_hand() const;
    int count_can_act() const;
    int to_call(const player& p) const;

    // Output
    json state_for(int viewer) const;
    void broadcast_state();
    void event(const std::string& text, json data = json());
    void send(int client, const json& msg);
    void send_error(int client, const std::string& message, const std::string& code);
};

#endif
