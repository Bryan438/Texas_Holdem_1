#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "match.h"
#include "rl_policy.h"
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
         "  --history FILE also save the hands here, for thstats\n"
         "  --model FILE   the network the \"rl\" style plays with, from rl/train.py\n"
         "                 (default: the one built into this program)\n\n"
         "A seat written rl=FILE plays with that network, so that two versions can\n"
         "play each other: %s rl=rl/models/new.txt rl=rl/models/old.txt\n\n"
         "A seat written STYLE/LATER changes style without warning after --switch N\n"
         "hands, keeping its name (rules/calling: rules, then a calling station).\n"
         "The hands after the switch are also scored on their own.\n"
         "  --switch N     hand from which STYLE/LATER seats play LATER\n"
         "  --no-reads     rl seats do not keep statistics on the others\n\n"
         "Example: %s --hands 5000 rules calling random\n",
         program, bot_style_list(), program, program);
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
  std::deque<rl_model> loaded;    // networks named on the command line (rl=FILE)
  std::vector<bot_style> later;   // each seat's style after --switch
  bool switching = false;
  for(int i = 1; i < argc; i++){
    std::string arg = argv[i];
    bool has_value = i + 1 < argc;
    int number = 0;
    if(arg == "--help" || arg == "-h"){
      usage(argv[0]);
      return 0;
    }
    else if(arg == "--no-reads"){
      options.track_opponents = false;
    }
    else if((arg == "--hands" || arg == "--chips" || arg == "--seed" || arg == "--switch") && has_value){
      if(!read_number(argv[++i], number)){
        printf("%s needs a positive whole number, not \"%s\"\n", arg.c_str(), argv[i]);
        return 1;
      }
      if(arg == "--hands") options.hands = number;
      else if(arg == "--chips") options.chips = number;
      else if(arg == "--switch") options.switch_after = number;
      else options.seed = number;
    }
    else if(arg == "--history" && has_value){
      options.history_path = argv[++i];
    }
    else if(arg == "--model" && has_value){
      std::string error;
      if(!rl_use_model_file(argv[++i], error)){
        printf("%s\n", error.c_str());
        return 1;
      }
    }
    else if(arg.rfind("rl=", 0) == 0){
      std::string path = arg.substr(3), error;
      loaded.emplace_back();
      if(!loaded.back().load_file(path, error)){
        printf("%s\n", error.c_str());
        return 1;
      }
      // Name the seat after the file: models/sp-gen12.txt -> sp-gen12-3
      std::string stem = path.substr(path.find_last_of('/') + 1);
      stem = stem.substr(0, stem.rfind('.'));
      std::string name = stem.substr(0, 16) + "-" + std::to_string(styles.size() + 1);
      options.models.resize(styles.size() + 1, nullptr);
      options.names.resize(styles.size() + 1);
      options.models[styles.size()] = &loaded.back();
      options.names[styles.size()] = name;
      styles.push_back(bot_style::RL);
      later.push_back(bot_style::RL);
    }
    else{
      size_t slash = arg.find('/');
      bot_style style, then;
      if(!parse_bot_style(arg.substr(0, slash), style) ||
         !parse_bot_style(slash == std::string::npos ? arg : arg.substr(slash + 1), then)){
        printf("Unknown style or option \"%s\". Styles: %s. See --help.\n", arg.c_str(), bot_style_list());
        return 1;
      }
      if(slash != std::string::npos) switching = true;
      styles.push_back(style);
      later.push_back(then);
    }
  }
  if(styles.size() < 2 || styles.size() > 12){
    usage(argv[0]);
    return 1;
  }
  for(bot_style style : styles){
    if(style == bot_style::RL && rl_current_model() == nullptr){
      printf("The \"rl\" style needs a trained network: pass --model FILE (see rl/train.py).\n");
      return 1;
    }
  }

  if(switching != (options.switch_after > 0)){
    printf("A seat written STYLE/LATER and --switch N go together. See --help.\n");
    return 1;
  }
  for(bot_style style : later){
    if(style == bot_style::RL && rl_current_model() == nullptr){
      printf("The \"rl\" style needs a trained network: pass --model FILE (see rl/train.py).\n");
      return 1;
    }
  }
  options.styles_after = later;
  match_result result = run_match(styles, options);
  auto stats = compute_stats(result.records);

  printf("%d hands, %zu bots, every hand starts at %d chips (%d big blinds), seed %u\n\n",
         result.hands, styles.size(), options.chips, options.chips / result.big_blind, options.seed);
  printf("%-20s %8s %10s %9s %6s %6s\n", "Bot", "Net", "bb/100", "+/-95%", "VPIP", "PFR");
  for(const match_seat& seat : result.seats){
    const player_stats& s = stats[seat.name];
    auto percent = [&](int part){ return s.hands ? (int)(100.0 * part / s.hands + 0.5) : 0; };
    printf("%-20s %+8lld %+10.1f %9.1f %5d%% %5d%%\n", seat.name.c_str(), seat.net,
           bb_per_100(seat, result), bb_per_100_margin(seat, result), percent(s.vpip), percent(s.pfr));
  }
  if(options.switch_after > 0 && result.hands > options.switch_after){
    // The same numbers over the hands after the switch only.
    match_result after = result;
    for(match_seat& seat : after.seats){
      seat.net = 0;
      seat.sum_squares = 0;
    }
    after.hands = 0;
    for(size_t h = options.switch_after; h < result.records.size(); h++){
      for(const json& p : result.records[h]["players"]){
        int net = p.value("net", 0);
        match_seat& seat = after.seats[p.value("seat", 0)];
        seat.net += net;
        seat.sum_squares += (double)net * net;
      }
      after.hands++;
    }
    printf("\nAfter the switch (hands %d to %d):\n", options.switch_after + 1, result.hands);
    for(const match_seat& seat : after.seats){
      printf("%-20s %+8lld %+10.1f %9.1f\n", seat.name.c_str(), seat.net, bb_per_100(seat, after),
             bb_per_100_margin(seat, after));
    }
  }
  printf("\nbb/100: big blinds won per 100 hands. +/-95%%: 95%% confidence interval;\n"
         "two bots whose ranges overlap may not really differ. Play more hands to narrow it.\n");
  if(result.illegal_moves > 0){
    printf("Warning: the table rejected %d bot moves.\n", result.illegal_moves);
    return 2;
  }
  return 0;
}
