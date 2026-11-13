#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "table.h"
#include "transport.h"

// Usage: ./thserver [port] [seconds per turn, 0 = unlimited]
int main(int argc, char* argv[]){
  int port = argc > 1 ? atoi(argv[1]) : 4000;
  table_config config;
  if(argc > 2){
    config.turn_seconds = atoi(argv[2]);
  }
  if(argc > 3){
    config.starting_stack = atoi(argv[3]);
  }

  setvbuf(stdout, NULL, _IOLBF, 0);  // show log lines right away, even when redirected
  transport* tp = transport::get_instance();
  tp->set_verbose(getenv("TH_VERBOSE") != NULL);

  table game(tp, config);
  tp->set_listener(&game);
  tp->start_with_thread(port);

  printf("Texas Hold'em server: port %d, %d s per turn, %d starting chips, blinds %d/%d\n",
         port, config.turn_seconds, config.starting_stack, config.small_blind, config.big_blind);

  // Enforce the turn time limit. Uses the same lock as message handling.
  while(true){
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tp->run_locked([&]{ game.tick(); });
  }
}
