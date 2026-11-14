#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "table.h"
#include "transport.h"

static void usage(const char* program){
  printf("Usage: %s [options]\n"
         "  --port N        TCP port (default 4000)\n"
         "  --turn N        seconds per turn, 0 = no limit (default 300)\n"
         "  --chips N       starting chips (default 200)\n"
         "  --next-hand N   start the next hand by itself after N seconds\n"
         "                  (default 0: someone types s to start each hand)\n"
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
  table_config config;
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
    else if(arg == "--turn" && has_value){
      config.turn_seconds = number(argv[0], argv[++i]);
    }
    else if(arg == "--chips" && has_value){
      config.starting_stack = number(argv[0], argv[++i]);
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

  std::string turn = config.turn_seconds > 0 ? std::to_string(config.turn_seconds) + " s per turn" : "no turn limit";
  std::string next = config.next_hand_seconds > 0
                       ? "next hand starts by itself after " + std::to_string(config.next_hand_seconds) + " s"
                       : "a player types s to start each hand";
  printf("Texas Hold'em server: port %d, %s, %d starting chips, blinds %d/%d, %s\n",
         port, turn.c_str(), config.starting_stack, config.small_blind, config.big_blind, next.c_str());

  // Turn time limit and next-hand countdown. Same lock as message handling.
  while(true){
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tp->run_locked([&]{ game.tick(); });
  }
}
