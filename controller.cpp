#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <string.h>
#include <stdlib.h>

#include "table.h"
#include "player.h"
#include "transport.h"

// The hand-ranking test cases that used to live here (behind #if 0) are now
// real unit tests in tests/test_hand_evaluator.cpp. Run them with `make test`.

int main(){
  table* ob = new table();
  transport::get_instance()->set_listener(ob);
  transport::get_instance()->start_with_thread();

  char player_name[50];
  memset(player_name, 0, 50);

  // Debug console: pick a player by name, then type messages to send them.
  while(true){
    printf("Which player? \n");
    if(!(std::cin >> std::setw(50) >> player_name)){
      break;
    }
    player* p = ob->search_player_by_name(player_name);
    if(p == NULL){
      continue;
    }
    p->send();
  }
  // stdin closed (e.g. running in the background): keep serving clients.
  while(true){
    std::this_thread::sleep_for(std::chrono::hours(1));
  }
}
