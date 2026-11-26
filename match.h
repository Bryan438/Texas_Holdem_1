#ifndef _MATCH_H_
#define _MATCH_H_

#include <map>
#include <string>
#include <vector>
#include "bot.h"
#include "observer.h"

class rl_model;

// Bots playing each other on a real table, in one process with no network:
// each bot gets the same state messages a networked player would get.
struct match_options{
  int hands = 1000;
  int chips = 200;        // every bot starts every hand with this many chips
  unsigned seed = 1;      // same seed, same cards and same bot choices
  std::string history_path;   // also save the hands here ("" = don't)
  // Per seat, optional: the network an RL seat plays with (null or missing:
  // the one in use), and a name instead of "<style>-<seat>".
  std::vector<const rl_model*> models;
  std::vector<std::string> names;
  // RL seats see the others' statistics from earlier hands of the match
  // (false: every opponent looks like a stranger, every hand).
  bool track_opponents = true;
  // From hand `switch_after` on (0: never), seat i plays styles_after[i]
  // instead, under the same name: a player who changes style without warning.
  int switch_after = 0;
  std::vector<bot_style> styles_after;
};

struct match_seat{
  std::string name;
  bot_style style;
  long long net = 0;          // chips won over all hands
  double sum_squares = 0;     // of each hand's net, for the spread
};

struct match_result{
  std::vector<match_seat> seats;
  int hands = 0;
  int big_blind = 2;
  int illegal_moves = 0;      // moves the table rejected (should be 0)
  std::vector<json> records;  // every hand, as in the history file
};

// A table's output when nobody is on a network: keeps the last state message
// each client was sent, unparsed (only the player to act needs theirs parsed),
// and counts the errors.
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

match_result run_match(const std::vector<bot_style>& styles, const match_options& options);

// Big blinds won per 100 hands, and the half-width of its 95% confidence interval.
double bb_per_100(const match_seat& seat, const match_result& result);
double bb_per_100_margin(const match_seat& seat, const match_result& result);

#endif
