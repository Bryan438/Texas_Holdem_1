#include <cerrno>
#include <sys/stat.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "lobby.h"
#include "table.h"
#include "transport.h"

static void usage(const char* program){
  printf("Usage: %s [options]\n"
         "  --port N        TCP port for the terminal client and bots (default 4000)\n"
         "  --web-port N    port for browsers: open http://localhost:N (default 8080, 0 = off)\n"
         "  --web DIR       folder with the web client's files (default: web, next to this program)\n"
         "  --turn N        seconds per turn, 0 = no limit (default 300)\n"
         "  --chips N       starting chips (default 200)\n"
         "  --next-hand N   start the next hand by itself after N seconds\n"
         "                  (default 0: someone types s to start each hand)\n"
         "  --bot-delay S   seconds a bot added from the web page waits before each move\n"
         "                  (default 1)\n"
         "  --grace N       a player who disconnects during a hand keeps their cards for\n"
         "                  N seconds before folding (default 30, 0 = folds at once)\n"
         "  --history FILE  append every finished hand to FILE as JSON lines\n"
         "                  (default hands.jsonl; it holds every player's cards)\n"
         "  --no-history    do not save hands\n"
         "  --verbose       print every message sent and received\n"
         "On the internet:\n"
         "  --bind ADDR     listen for terminal clients and bots on ADDR only (e.g. 127.0.0.1)\n"
         "  --web-bind ADDR listen for browsers on ADDR only (127.0.0.1 behind a reverse proxy)\n"
         "  --trust-proxy   browsers come through a proxy on this machine that sets\n"
         "                  X-Forwarded-For: count their connections under that address\n"
         "  --max-connections N  open connections in all (default 200)\n"
         "  --max-per-ip N  open connections from one address (default 20)\n"
         "  --console-fifo PATH  also take console commands from this named pipe\n"
         "                  (made if missing), for a server with no terminal\n"
         "While it runs, type into it: players (who is seated), kick NAME (remove\n"
         "someone from the table), help.\n"
         "Old style still works: %s [port] [seconds per turn] [starting chips]\n",
         program, program);
}

static const char* console_help =
  "Commands:\n"
  "  tables            the tables that are open\n"
  "  players           who is seated at each table, with their chips\n"
  "  kick NAME [TABLE] remove someone from their table (folds their hand if one is\n"
  "                    running); TABLE only when the name is at more than one table\n"
  "  close TABLE       close a table (not the main one): everyone there goes back\n"
  "                    to the lobby\n"
  "  help        this list\n";

// What the person running the server types. Runs until the input ends, so a
// server started with no terminal (input from /dev/null) simply has no console.
static void console_lines(transport* tp, lobby* game, std::istream& input){
  std::string line;
  while(std::getline(input, line)){
    line.erase(0, line.find_first_not_of(" \t"));
    line.erase(line.find_last_not_of(" \t\r") + 1);
    if(line.empty()){
      continue;
    }
    std::string word = line.substr(0, line.find(' '));
    std::string rest = word.size() < line.size() ? line.substr(word.size() + 1) : "";
    rest.erase(0, rest.find_first_not_of(' '));
    if(word == "players" || word == "list"){
      std::string text;
      tp->run_locked([&]{ text = game->roster(); });
      printf("%s", text.c_str());
    }
    else if(word == "tables"){
      std::string text;
      tp->run_locked([&]{ text = game->list(); });
      printf("%s", text.c_str());
    }
    else if(word == "close"){
      if(rest.empty() || rest.find_first_not_of("0123456789") != std::string::npos || rest.size() > 6){
        printf("Usage: close TABLE (its number: type tables to see them)\n");
        continue;
      }
      std::string error;
      bool done = false;
      tp->run_locked([&]{ done = game->close(std::stoi(rest), error); });
      if(!done){
        printf("%s\n", error.c_str());
      }
    }
    else if(word == "kick"){
      if(rest.empty()){
        printf("Usage: kick NAME [TABLE]\n");
        continue;
      }
      std::string error;
      bool done = false;
      // "kick RL 1" is the bot called "RL 1"; "kick Ann 3" is Ann at table 3,
      // when nobody is called "Ann 3".
      tp->run_locked([&]{
        done = game->kick(rest, 0, error);
        size_t space = rest.find_last_of(' ');
        if(!done && space != std::string::npos &&
           rest.find_first_not_of("0123456789", space + 1) == std::string::npos &&
           space + 1 < rest.size() && rest.size() - space - 1 <= 6){
          std::string retry;
          if(game->kick(rest.substr(0, space), std::stoi(rest.substr(space + 1)), retry)){
            done = true;
          }
          else if(error.find("nobody called") == 0){
            error = retry;
          }
        }
      });
      if(!done){
        printf("%s\n", error.c_str());
      }
    }
    else{
      printf("%s", console_help);
    }
  }
}

// Typed into the terminal thserver runs in. Ends with the input, so a server
// started with no terminal (input from /dev/null) simply has no console.
static void console(transport* tp, lobby* game){
  console_lines(tp, game, std::cin);
}

// Under a service manager there is no terminal: commands come through a named
// pipe instead (echo "kick Ann" > PATH; the answer goes to the log).
static void console_fifo(transport* tp, lobby* game, std::string path){
  while(true){
    std::ifstream input(path);   // waits until someone writes
    if(!input){
      printf("Cannot read the console pipe %s\n", path.c_str());
      return;
    }
    console_lines(tp, game, input);
  }
}

// Parse a non-negative number; exits with usage on anything else.
static int number(const char* program, const char* text){
  char* end = nullptr;
  long value = strtol(text, &end, 10);
  if(text[0] == '\0' || *end != '\0' || value < 0 || value > 1000000){
    printf("Not a valid number: %s\n\n", text);
    usage(program);
    exit(1);
  }
  return (int)value;
}

int main(int argc, char* argv[]){
  int port = 4000;
  int web_port = 8080;
  // The web client's files sit next to the program, wherever it is started from.
  std::string program = argv[0];
  size_t slash = program.rfind('/');
  std::string web_root = (slash == std::string::npos ? "" : program.substr(0, slash + 1)) + "web";
  table_config config;
  config.history_path = "hands.jsonl";
  bool verbose = getenv("TH_VERBOSE") != NULL;
  std::string bind_address, web_bind_address;
  bool trust_proxy = false;
  int max_connections = 200, max_per_ip = 20;
  std::string console_path;

  int positional = 0;
  for(int i = 1; i < argc; i++){
    std::string arg = argv[i];
    bool has_value = i + 1 < argc;
    if(arg == "--help" || arg == "-h"){
      usage(argv[0]);
      return 0;
    }
    else if(arg == "--verbose"){
      verbose = true;
    }
    else if(arg == "--port" && has_value){
      port = number(argv[0], argv[++i]);
    }
    else if(arg == "--web-port" && has_value){
      web_port = number(argv[0], argv[++i]);
    }
    else if(arg == "--web" && has_value){
      web_root = argv[++i];
    }
    else if(arg == "--bind" && has_value){
      bind_address = argv[++i];
    }
    else if(arg == "--web-bind" && has_value){
      web_bind_address = argv[++i];
    }
    else if(arg == "--console-fifo" && has_value){
      console_path = argv[++i];
    }
    else if(arg == "--trust-proxy"){
      trust_proxy = true;
    }
    else if(arg == "--max-connections" && has_value){
      max_connections = std::max(1, number(argv[0], argv[++i]));
    }
    else if(arg == "--max-per-ip" && has_value){
      max_per_ip = std::max(1, number(argv[0], argv[++i]));
    }
    else if(arg == "--turn" && has_value){
      config.turn_seconds = number(argv[0], argv[++i]);
    }
    else if(arg == "--chips" && has_value){
      config.starting_stack = number(argv[0], argv[++i]);
    }
    else if(arg == "--history" && has_value){
      config.history_path = argv[++i];
    }
    else if(arg == "--no-history"){
      config.history_path = "";
    }
    else if(arg == "--bot-delay" && has_value){
      config.bot_delay_ms = number(argv[0], argv[++i]) * 1000;
    }
    else if(arg == "--grace" && has_value){
      config.disconnect_grace_seconds = number(argv[0], argv[++i]);
    }
    else if(arg == "--next-hand" && has_value){
      config.next_hand_seconds = number(argv[0], argv[++i]);
    }
    else if(arg.rfind("--", 0) != 0 && positional < 3){
      int value = number(argv[0], argv[i]);
      if(positional == 0) port = value;
      else if(positional == 1) config.turn_seconds = value;
      else config.starting_stack = value;
      positional++;
    }
    else{
      printf("Unknown or incomplete option: %s\n\n", arg.c_str());
      usage(argv[0]);
      return 1;
    }
  }
  if(config.starting_stack < config.big_blind){
    printf("Starting chips must be at least the big blind (%d)\n", config.big_blind);
    return 1;
  }

  setvbuf(stdout, NULL, _IOLBF, 0);  // show log lines right away, even when redirected
  transport* tp = transport::get_instance();
  tp->set_verbose(verbose);
  tp->set_bind(bind_address, web_bind_address);
  tp->set_trust_proxy(trust_proxy);
  tp->limits.max_total = max_connections;
  tp->limits.max_per_address = max_per_ip;

  lobby game(tp, config);
  tp->set_listener(&game);
  // Both ports must open, or there is no point running: say so and stop.
  if(!tp->start_with_thread(port)){
    return 1;
  }
  if(web_port > 0 && web_port != port && !tp->start_web_with_thread(web_port, web_root)){
    return 1;
  }

  std::string turn = config.turn_seconds > 0 ? std::to_string(config.turn_seconds) + " s per turn" : "no turn limit";
  std::string next = config.next_hand_seconds > 0
                       ? "next hand starts by itself after " + std::to_string(config.next_hand_seconds) + " s"
                       : "a player types s to start each hand";
  printf("Texas Hold'em server: port %d, %s, %d starting chips, blinds %d/%d, %s\n",
         port, turn.c_str(), config.starting_stack, config.small_blind, config.big_blind, next.c_str());
  if(web_port > 0 && web_port != port){
    if(std::ifstream(web_root + "/index.html")){
      printf("Play in a browser: http://localhost:%d\n", web_port);
    }
    else{
      printf("Web client not found in %s (pass --web DIR, or --web-port 0 to turn it off)\n", web_root.c_str());
    }
  }
  if(!config.history_path.empty()){
    printf("Saving hands to %s (contains every player's cards: keep it to yourself)\n",
           config.history_path.c_str());
  }
  printf("Type tables to see the tables, players to see who is seated, kick NAME to remove someone, help for more\n");
  std::thread(console, tp, &game).detach();
  if(!console_path.empty()){
    struct stat info;
    if(stat(console_path.c_str(), &info) != 0 && mkfifo(console_path.c_str(), 0600) != 0){
      printf("Cannot make the console pipe %s: %s\n", console_path.c_str(), strerror(errno));
      return 1;
    }
    std::thread(console_fifo, tp, &game, console_path).detach();
  }

  // Turn time limit and next-hand countdown. Same lock as message handling.
  while(true){
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tp->run_locked([&]{ game.tick(); });
  }
}
