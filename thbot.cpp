// A bot that joins a running server over TCP like any other player and uses
// the same JSON protocol as the client. The server does not know it is a bot.
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <netdb.h>
#include <poll.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "bot.h"
#include "line_buffer.h"

struct bot_config{
  std::string host = "127.0.0.1";
  int port = 4000;
  std::string name = "Robo";
  bot_style style = bot_style::EQUITY;
  double delay = 1.5;      // seconds to "think" before each move
  bool starts_hands = false;
  unsigned seed = 0;       // 0 = random
};

static std::mutex print_mutex;

static void say(const bot_config& cfg, const std::string& text){
  std::lock_guard<std::mutex> lock(print_mutex);
  printf("[%s] %s\n", cfg.name.c_str(), text.c_str());
  fflush(stdout);
}

static int64_t now_ms(){
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static int connect_to(const std::string& host, int port, std::string& error){
  addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* found = nullptr;
  int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found);
  if(rc != 0){
    error = std::string("cannot resolve ") + host + ": " + gai_strerror(rc);
    return -1;
  }
  int sock = -1;
  for(addrinfo* a = found; a != nullptr && sock < 0; a = a->ai_next){
    int s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if(s < 0) continue;
    if(connect(s, a->ai_addr, a->ai_addrlen) == 0) sock = s;
    else close(s);
  }
  freeaddrinfo(found);
  if(sock < 0){
    error = "cannot connect to " + host + ":" + std::to_string(port) + " (is the server running?)";
  }
  return sock;
}

static bool send_line(int sock, const json& msg){
  std::string data = msg.dump() + "\n";
  size_t sent = 0;
  while(sent < data.size()){
    ssize_t n = send(sock, data.data() + sent, data.size() - sent, 0);
    if(n <= 0) return false;
    sent += n;
  }
  return true;
}

static bool my_turn(const json& state){
  return state.value("stage", "") != "waiting" && state.value("turn", -1) == state.value("you", -2) &&
         state.contains("actions") && state["actions"].is_object();
}

// Identifies one decision point, so the bot never answers the same one twice.
static std::string decision_key(const json& state){
  return std::to_string(state.value("hand", 0)) + "/" + state.value("stage", "") + "/" +
         std::to_string(state.value("pot", 0)) + "/" + std::to_string(state.value("current_bet", 0));
}

static int ready_players(const json& state){
  int ready = 0;
  for(const json& s : state.value("seats", json::array())){
    ready += s.value("status", "") == "ready";
  }
  return ready;
}

static std::string describe(const bot_move& move, const json& state){
  if(move.action != "raise") return move.action;
  return (state.value("current_bet", 0) == 0 ? "bet " : "raise to ") + std::to_string(move.amount);
}

// One connection, until it drops. Returns false if the server turned the bot
// away (a name that is taken, a full table): trying again would not help.
static bool play(const bot_config& cfg, int sock, std::string& token, std::mt19937& rng){
  json join = {{"type", "join"}, {"name", cfg.name}};
  if(!token.empty()) join["token"] = token;
  if(!send_line(sock, join)) return true;

  bool seated = false;
  json latest;
  std::string answered;     // decision_key of the last state the bot moved on
  int64_t move_at = -1;     // when to make the pending move (-1: none pending)
  bool play_safe = false;   // the last move was rejected: check or fold instead
  int64_t start_at = -1;   // when to send start (-1: not planned)
  line_buffer buffer;
  char chunk[4096];

  auto think_time = [&]{
    std::uniform_real_distribution<double> jitter(0.6, 1.4);
    return (int64_t)(cfg.delay * jitter(rng) * 1000);
  };

  while(true){
    int64_t now = now_ms();
    int64_t next = 1000;
    if(move_at >= 0) next = std::min(next, std::max<int64_t>(0, move_at - now));
    if(start_at >= 0) next = std::min(next, std::max<int64_t>(0, start_at - now));
    pollfd p = {sock, POLLIN, 0};
    int ready = poll(&p, 1, (int)next);
    if(ready > 0){
      int n = recv(sock, chunk, sizeof(chunk), 0);
      if(n <= 0){
        say(cfg, "lost the connection");
        return true;
      }
      std::vector<std::string> lines;
      if(!buffer.feed(chunk, n, lines)) return true;
      for(const std::string& line : lines){
        json msg = json::parse(line, nullptr, false);
        if(msg.is_discarded() || !msg.is_object()) continue;
        std::string type = msg.value("type", "");
        if(type == "welcome"){
          seated = true;
          token = msg.value("token", token);
          say(cfg, "seated at seat " + std::to_string(msg.value("seat", -1)) + ", playing " +
                   bot_style_name(cfg.style));
        }
        else if(type == "error"){
          std::string text = msg.value("message", "");
          if(!seated){
            say(cfg, "the server said: " + text);
            return false;
          }
          if(text.find("need at least 2") != std::string::npos) continue;   // a start that was too early
          say(cfg, "move rejected (" + text + "), playing safe");
          play_safe = true;
          answered.clear();
          if(my_turn(latest)) move_at = now_ms();
        }
        else if(type == "hand_summary"){
          for(const json& w : msg.value("winners", json::array())){
            say(cfg, "hand #" + std::to_string(msg.value("hand", 0)) + ": " + w.value("name", "?") +
                     " wins " + std::to_string(w.value("won", 0)));
          }
        }
        else if(type == "state"){
          latest = msg;
          if(my_turn(msg)){
            if(move_at < 0 && decision_key(msg) != answered) move_at = now_ms() + think_time();
          }
          else{
            move_at = -1;
          }
          bool idle = msg.value("stage", "") == "waiting" && msg["next_hand_in"].is_null();
          if(cfg.starts_hands && idle && ready_players(msg) >= 2){
            if(start_at < 0) start_at = now_ms() + 3000 + think_time();
          }
          else{
            start_at = -1;
          }
        }
      }
    }
    else if(ready < 0 && errno != EINTR){
      return true;
    }

    now = now_ms();
    if(move_at >= 0 && now >= move_at){
      move_at = -1;
      if(my_turn(latest)){
        bot_move move;
        if(play_safe){
          move.action = latest["actions"].value("check", false) ? "check" : "fold";
          play_safe = false;
        }
        else{
          move = decide(latest, cfg.style, rng);
        }
        answered = decision_key(latest);
        say(cfg, "hand #" + std::to_string(latest.value("hand", 0)) + " " + latest.value("stage", "") +
                 ": " + describe(move, latest));
        if(!send_line(sock, move.to_message())) return true;
      }
    }
    if(start_at >= 0 && now >= start_at){
      start_at = -1;
      if(!send_line(sock, {{"type", "start"}})) return true;
    }
  }
}

static void run_bot(bot_config cfg){
  std::mt19937 rng(cfg.seed ? cfg.seed : std::random_device{}());
  std::string token;
  int failures = 0;
  while(true){
    std::string error;
    int sock = connect_to(cfg.host, cfg.port, error);
    if(sock < 0){
      if(++failures >= 30){
        say(cfg, error + "; giving up");
        return;
      }
      if(failures == 1) say(cfg, error + "; retrying every 2 s");
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }
    failures = 0;
    bool keep_going = play(cfg, sock, token, rng);
    close(sock);
    if(!keep_going) return;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    say(cfg, "reconnecting");
  }
}

static void usage(const char* program){
  printf("Usage: %s [host] [port] [options]\n"
         "Joins a server as a player and plays by itself.\n\n"
         "  --name NAME    player name (default Robo; with --bots N: Robo1, Robo2, ...)\n"
         "  --style STYLE  %s (default equity)\n"
         "  --bots N       run N bots at once (default 1)\n"
         "  --delay S      seconds to think before each move (default 1.5)\n"
         "  --start        start each hand when 2+ players are ready, like typing s\n"
         "                 (by default the bot waits for a person to start)\n"
         "  --seed N       repeat the same choices\n\n"
         "Example: %s 127.0.0.1 4000 --bots 2 --style rules\n",
         program, bot_style_list(), program);
}

int main(int argc, char* argv[]){
  signal(SIGPIPE, SIG_IGN);
  bot_config cfg;
  int bots = 1;
  int positional = 0;
  for(int i = 1; i < argc; i++){
    std::string arg = argv[i];
    bool has_value = i + 1 < argc;
    if(arg == "--help" || arg == "-h"){
      usage(argv[0]);
      return 0;
    }
    else if(arg == "--name" && has_value){
      cfg.name = argv[++i];
    }
    else if(arg == "--style" && has_value){
      if(!parse_bot_style(argv[++i], cfg.style)){
        printf("Unknown style \"%s\". Styles: %s\n", argv[i], bot_style_list());
        return 1;
      }
    }
    else if(arg == "--bots" && has_value){
      bots = atoi(argv[++i]);
      if(bots < 1 || bots > 11){
        printf("--bots needs 1 to 11\n");
        return 1;
      }
    }
    else if(arg == "--delay" && has_value){
      cfg.delay = atof(argv[++i]);
      if(cfg.delay < 0 || cfg.delay > 60){
        printf("--delay needs 0 to 60 seconds\n");
        return 1;
      }
    }
    else if(arg == "--seed" && has_value){
      cfg.seed = (unsigned)strtoul(argv[++i], nullptr, 10);
    }
    else if(arg == "--start"){
      cfg.starts_hands = true;
    }
    else if(arg.rfind("--", 0) != 0 && positional == 0){
      cfg.host = arg;
      positional++;
    }
    else if(arg.rfind("--", 0) != 0 && positional == 1){
      cfg.port = atoi(arg.c_str());
      if(cfg.port < 1 || cfg.port > 65535){
        printf("Port must be 1-65535, not \"%s\"\n", arg.c_str());
        return 1;
      }
      positional++;
    }
    else{
      printf("Unknown option \"%s\". See --help.\n", arg.c_str());
      return 1;
    }
  }

  std::vector<std::thread> threads;
  for(int b = 0; b < bots; b++){
    bot_config one = cfg;
    if(bots > 1) one.name = cfg.name + std::to_string(b + 1);
    if(cfg.seed) one.seed = cfg.seed + b;
    one.starts_hands = cfg.starts_hands && b == 0;   // one starter is enough
    threads.emplace_back(run_bot, one);
  }
  for(std::thread& t : threads) t.join();
  return 0;
}
