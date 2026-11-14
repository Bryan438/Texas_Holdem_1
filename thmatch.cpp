#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "match.h"
#include "stats.h"

static void usage(const char* program){
  printf("Usage: %s [options] STYLE STYLE [STYLE...]\n"
         "Bots play each other on one table, with no network, and the result is\n"
         "printed in big blinds won per 100 hands.\n\n"
         "Styles: %s\n\n"
         "Options:\n"
         "  --hands N      hands to play (default 2000)\n"
         "  --chips N      chips every bot starts every hand with (default 200)\n"
         "  --seed N       same seed, same cards and same result (default 1)\n"
         "  --history FILE also save the hands here, for thstats\n\n"
         "Example: %s --hands 5000 rules calling random\n",
         program, bot_style_list(), program);
}

static bool read_number(const char* text, int& out){
  char* end = nullptr;
  long value = strtol(text, &end, 10);
  if(*text == '\0' || *end != '\0' || value < 1 || value > 100000000) return false;
  out = (int)value;
  return true;
}

int main(int argc, char* argv[]){
  match_options options;
  options.hands = 2000;
  std::vector<bot_style> styles;
  for(int i = 1; i < argc; i++){
    std::string arg = argv[i];
    bool has_value = i + 1 < argc;
    int number = 0;
    if(arg == "--help" || arg == "-h"){
      usage(argv[0]);
      return 0;
    }
    else if((arg == "--hands" || arg == "--chips" || arg == "--seed") && has_value){
      if(!read_number(argv[++i], number)){
        printf("%s needs a positive whole number, not \"%s\"\n", arg.c_str(), argv[i]);
        return 1;
      }
      if(arg == "--hands") options.hands = number;
      else if(arg == "--chips") options.chips = number;
      else options.seed = number;
    }
    else if(arg == "--history" && has_value){
      options.history_path = argv[++i];
    }
    else{
      bot_style style;
      if(!parse_bot_style(arg, style)){
        printf("Unknown style or option \"%s\". Styles: %s. See --help.\n", arg.c_str(), bot_style_list());
        return 1;
      }
      styles.push_back(style);
    }
  }
  if(styles.size() < 2 || styles.size() > 12){
    usage(argv[0]);
    return 1;
  }

  match_result result = run_match(styles, options);
  auto stats = compute_stats(result.records);

  printf("%d hands, %zu bots, every hand starts at %d chips (%d big blinds), seed %u\n\n",
         result.hands, styles.size(), options.chips, options.chips / result.big_blind, options.seed);
  printf("%-12s %8s %10s %9s %6s %6s\n", "Bot", "Net", "bb/100", "+/-95%", "VPIP", "PFR");
  for(const match_seat& seat : result.seats){
    const player_stats& s = stats[seat.name];
    auto percent = [&](int part){ return s.hands ? (int)(100.0 * part / s.hands + 0.5) : 0; };
    printf("%-12s %+8lld %+10.1f %9.1f %5d%% %5d%%\n", seat.name.c_str(), seat.net,
           bb_per_100(seat, result), bb_per_100_margin(seat, result), percent(s.vpip), percent(s.pfr));
  }
  printf("\nbb/100: big blinds won per 100 hands. +/-95%%: 95%% confidence interval;\n"
         "two bots whose ranges overlap may not really differ. Play more hands to narrow it.\n");
  if(result.illegal_moves > 0){
    printf("Warning: the table rejected %d bot moves.\n", result.illegal_moves);
    return 2;
  }
  return 0;
}
