#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

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
         "Old style still works: %s [port] [seconds per turn] [starting chips]\n",
         program, program);
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

  table game(tp, config);
  tp->set_listener(&game);
  tp->start_with_thread(port);
  if(web_port > 0 && web_port != port){
    tp->start_web_with_thread(web_port, web_root);
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

  // Turn time limit and next-hand countdown. Same lock as message handling.
  while(true){
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tp->run_locked([&]{ game.tick(); });
  }
}
