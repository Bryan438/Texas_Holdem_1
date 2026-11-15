// The poker table as a training environment for rl/train.py. It runs several
// tables at once and talks to the trainer over stdin and stdout:
//
//   at the start   one text line: thenv 1 envs N features F actions A
//   then           one block of N rows, each 4 + F + A little-endian floats:
//                  reward, done, other_hands, other_net (see rl_env.h),
//                  the F features, and 1 or 0 for each of the A actions
//   for every step the trainer writes N bytes (one action per table)
//                  and gets the next block back
//
// It stops when stdin closes.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "rl_env.h"

static void usage(const char* program){
  fprintf(stderr,
          "Usage: %s [options]\n"
          "Poker tables for rl/train.py to learn on; not meant to be run by hand.\n\n"
          "  --envs N          tables to run at once (default 8)\n"
          "  --players LIST    seats per table, learner included; a list such as 2,2,3\n"
          "                    is used in turn for the tables (default 2)\n"
          "  --opponents LIST  styles to draw the opponents from, with weights:\n"
          "                    equity:3,rules:1 (default equity). Styles: %s\n"
          "  --model FILE      the network that \"rl\" opponents use\n"
          "  --chips N         chips every seat starts every hand with (default 200)\n"
          "  --seed N          same seed, same cards (default 1)\n",
          program, bot_style_list());
}

static std::vector<std::string> split(const std::string& text, char by){
  std::vector<std::string> parts;
  std::stringstream in(text);
  std::string part;
  while(std::getline(in, part, by)){
    if(!part.empty()) parts.push_back(part);
  }
  return parts;
}

int main(int argc, char* argv[]){
  int count = 8;
  std::vector<int> players = {2};
  rl_env_options options;
  for(int i = 1; i < argc; i++){
    std::string arg = argv[i];
    if(arg == "--help" || arg == "-h" || i + 1 >= argc){
      usage(argv[0]);
      return arg == "--help" || arg == "-h" ? 0 : 1;
    }
    std::string value = argv[++i];
    if(arg == "--envs") count = atoi(value.c_str());
    else if(arg == "--chips") options.chips = atoi(value.c_str());
    else if(arg == "--seed") options.seed = (unsigned)strtoul(value.c_str(), nullptr, 10);
    else if(arg == "--players"){
      players.clear();
      for(const std::string& part : split(value, ',')) players.push_back(atoi(part.c_str()));
    }
    else if(arg == "--opponents"){
      options.opponents.clear();
      for(const std::string& part : split(value, ',')){
        std::vector<std::string> pair = split(part, ':');
        bot_style style;
        if(pair.empty() || !parse_bot_style(pair[0], style)){
          fprintf(stderr, "Unknown style in --opponents: \"%s\". Styles: %s\n", part.c_str(), bot_style_list());
          return 1;
        }
        options.opponents.push_back({style, pair.size() > 1 ? atoi(pair[1].c_str()) : 1});
      }
    }
    else if(arg == "--model"){
      std::string error;
      if(!rl_use_model_file(value, error)){
        fprintf(stderr, "%s\n", error.c_str());
        return 1;
      }
    }
    else{
      usage(argv[0]);
      return 1;
    }
  }
  if(count < 1 || count > 4096 || players.empty() || options.chips < 4){
    usage(argv[0]);
    return 1;
  }
  for(const auto& choice : options.opponents){
    if(choice.first == bot_style::RL && rl_current_model() == nullptr){
      fprintf(stderr, "\"rl\" opponents need a trained network: pass --model FILE\n");
      return 1;
    }
  }

  std::vector<std::unique_ptr<rl_env>> envs;
  for(int i = 0; i < count; i++){
    rl_env_options mine = options;
    mine.players = players[i % players.size()];
    mine.seed = options.seed * 1000003u + i + 1;
    envs.push_back(std::unique_ptr<rl_env>(new rl_env(mine)));
  }

  const int row = 4 + RL_FEATURES + RL_ACTIONS;
  std::vector<float> block((size_t)count * row);
  std::vector<rl_step> results(count);
  auto write_block = [&]{
    for(int i = 0; i < count; i++){
      float* out = &block[(size_t)i * row];
      const rl_observation& seen = envs[i]->observation();
      *out++ = results[i].reward;
      *out++ = results[i].done ? 1 : 0;
      *out++ = (float)results[i].other_hands;
      *out++ = results[i].other_net;
      for(float f : seen.features) *out++ = f;
      for(bool allowed : seen.allowed) *out++ = allowed ? 1 : 0;
    }
    return fwrite(block.data(), sizeof(float), block.size(), stdout) == block.size() && fflush(stdout) == 0;
  };

  printf("thenv 1 envs %d features %d actions %d\n", count, RL_FEATURES, RL_ACTIONS);
  if(!write_block()) return 0;
  std::vector<unsigned char> actions(count);
  while(fread(actions.data(), 1, count, stdin) == (size_t)count){
    for(int i = 0; i < count; i++){
      results[i] = envs[i]->step(actions[i]);
    }
    if(!write_block()) break;
  }
  int illegal = 0;
  for(const auto& env : envs) illegal += env->illegal_moves();
  if(illegal > 0){
    fprintf(stderr, "thenv: the tables rejected %d moves\n", illegal);
    return 2;
  }
  return 0;
}
