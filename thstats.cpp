#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include "stats.h"

// Usage: ./thstats [hands.jsonl]
int main(int argc, char* argv[]){
  std::string path = argc > 1 ? argv[1] : "hands.jsonl";
  if(path == "--help" || path == "-h"){
    printf("Usage: %s [hands.jsonl]\nPer-player stats from the server's hand history.\n", argv[0]);
    return 0;
  }
  std::ifstream in(path);
  if(!in){
    printf("Cannot open %s\n", path.c_str());
    return 1;
  }

  std::vector<json> hands;
  std::string line;
  int bad_lines = 0, line_number = 0;
  while(std::getline(in, line)){
    line_number++;
    if(line.empty()) continue;
    json hand = json::parse(line, nullptr, false);
    if(hand.is_discarded() || !hand.is_object()){
      bad_lines++;
      continue;
    }
    std::string problem = check_hand(hand);
    if(!problem.empty()){
      printf("Warning: line %d (hand #%d) does not add up: %s\n", line_number, hand.value("hand", 0), problem.c_str());
    }
    hands.push_back(hand);
  }

  printf("%zu hands from %s", hands.size(), path.c_str());
  if(bad_lines > 0) printf(" (%d unreadable lines skipped)", bad_lines);
  printf("\n\n%s", format_stats(compute_stats(hands)).c_str());
  printf("\nVPIP: put money in preflop by choice. PFR: raised preflop. Won: hands won.\n"
         "Showdowns: won/reached. Best: biggest single win.\n");
  return 0;
}
