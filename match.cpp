#include <algorithm>
#include <cmath>
#include <map>
#include "match.h"
#include "table.h"

namespace {

// Keeps the last state message each client was sent, unparsed: only the
// player to act needs theirs parsed.
class capture_sender: public sender{
  public:
    std::map<int, std::string> last_state;
    int errors = 0;
    void send_line(int client, const std::string& line) override{
      if(line.find("\"type\":\"state\"") != std::string::npos){
        last_state[client] = line;
      }
      else if(line.find("\"type\":\"error\"") != std::string::npos){
        errors++;
      }
    }
};

int client_of(int seat){ return 1000 + seat; }

}  // namespace

match_result run_match(const std::vector<bot_style>& styles, const match_options& options){
  match_result result;
  table_config config;
  config.log_events = false;
  config.turn_seconds = 0;
  config.next_hand_seconds = 0;
  config.starting_stack = options.chips;
  config.history_path = options.history_path;
  config.seed = options.seed;
  result.big_blind = config.big_blind;

  capture_sender out;
  table t(&out, config, []{ return (int64_t)0; });
  for(size_t i = 0; i < styles.size(); i++){
    match_seat seat;
    seat.name = std::string(bot_style_name(styles[i])) + "-" + std::to_string(i + 1);
    seat.style = styles[i];
    result.seats.push_back(seat);
    t.on_connect(client_of(i));
    t.on_message(client_of(i), json({{"type", "join"}, {"name", seat.name}}).dump());
  }

  std::mt19937 rng(options.seed * 2654435761u + 1);
  for(int hand = 0; hand < options.hands; hand++){
    for(size_t i = 0; i < styles.size(); i++){
      t.set_stack(i, options.chips);
    }
    t.on_message(client_of(0), R"({"type":"start"})");
    for(int guard = 0; guard < 1000 && t.get_stage() != WAITING; guard++){
      int s = t.get_turn();
      json state = json::parse(out.last_state[client_of(s)]);
      bot_move move = decide(state, styles[s], rng);
      int errors_before = out.errors;
      t.on_message(client_of(s), move.to_message().dump());
      if(out.errors != errors_before){
        // Count it and play on with a move that is always allowed.
        result.illegal_moves++;
        bool can_check = state["actions"].value("check", false);
        t.on_message(client_of(s), json({{"type", "action"}, {"action", can_check ? "check" : "fold"}}).dump());
      }
    }
    if(t.get_stage() != WAITING){
      break;    // cannot happen unless the table is stuck
    }
    const json& record = t.last_hand();
    for(const json& p : record["players"]){
      int net = p.value("net", 0);
      match_seat& seat = result.seats[p.value("seat", 0)];
      seat.net += net;
      seat.sum_squares += (double)net * net;
    }
    result.records.push_back(record);
    result.hands++;
  }
  return result;
}

double bb_per_100(const match_seat& seat, const match_result& result){
  if(result.hands == 0) return 0;
  return 100.0 * seat.net / result.hands / result.big_blind;
}

double bb_per_100_margin(const match_seat& seat, const match_result& result){
  int n = result.hands;
  if(n < 2) return 0;
  double mean = (double)seat.net / n;
  double variance = (seat.sum_squares - n * mean * mean) / (n - 1);
  double standard_error = std::sqrt(std::max(0.0, variance) / n);
  return 1.96 * standard_error * 100.0 / result.big_blind;
}
