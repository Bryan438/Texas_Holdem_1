#include <algorithm>
#include "rl_env.h"

static int client_of(int seat){ return 1000 + seat; }

rl_env::rl_env(const rl_env_options& opts): options(opts), rng(opts.seed * 2654435761u + 7){
  options.players = std::max(2, std::min(options.players, MAX_PLAYERS));
  if(options.opponents.empty()) options.opponents = {{bot_style::EQUITY, 1}};
  for(auto& choice : options.opponents){
    choice.second = std::max(1, choice.second);
    total_weight += choice.second;
  }
  table_config config;
  config.log_events = false;
  config.turn_seconds = 0;
  config.next_hand_seconds = 0;
  config.starting_stack = options.chips;
  config.seed = options.seed == 0 ? 1 : options.seed;
  game.reset(new table(&out, config, []{ return (int64_t)0; }));
  for(int i = 0; i < options.players; i++){
    game->on_connect(client_of(i));
    game->on_message(client_of(i), json({{"type", "join"}, {"name", "seat-" + std::to_string(i)}}).dump());
  }
  styles.assign(options.players, bot_style::EQUITY);
  models.assign(options.players, nullptr);
  rl_step ignored;
  play_to_learner(ignored);
}

int rl_env::hands_played() const{
  return game->hand_number();
}

void rl_env::start_hand(){
  for(int i = 0; i < options.players; i++){
    game->set_stack(i, options.chips);
  }
  if(options.session_hands <= 1){
    draw_opponents();      // new opponents every hand, and nothing to learn about them
  }
  else{
    if(session_left <= 0){
      reads.clear();
      draw_opponents();
      session_left = options.session_hands / 2 + (int)(rng() % (options.session_hands + 1));
      switch_at = (int)(rng() % 100) < options.switch_percent ? session_left / 2 : -1;
    }
    else if(session_left == switch_at){
      draw_opponents();
    }
    session_left--;
  }
  learner_chose = false;
  hand_running = true;
  game->on_message(client_of(0), R"({"type":"start"})");
}

void rl_env::draw_opponents(){
  for(int i = 1; i < options.players; i++){
    int roll = rng() % total_weight;
    for(const auto& choice : options.opponents){
      if(roll < choice.second){
        styles[i] = choice.first;
        break;
      }
      roll -= choice.second;
    }
    models[i] = nullptr;
    if(styles[i] == bot_style::RL && !options.pool.empty()){
      models[i] = options.pool[rng() % options.pool.size()].get();
    }
  }
}

void rl_env::play(int seat, const bot_move& move, bool can_check){
  int errors_before = out.errors;
  game->on_message(client_of(seat), move.to_message().dump());
  if(out.errors != errors_before){
    // Should not happen; play on with a move that is always allowed.
    game->on_message(client_of(seat), json({{"type", "action"}, {"action", can_check ? "check" : "fold"}}).dump());
  }
}

void rl_env::play_to_learner(rl_step& result){
  for(int guard = 0; guard < 100000; guard++){
    if(game->get_stage() == WAITING){
      if(hand_running){
        float net = 0;
        for(const json& p : game->last_hand()["players"]){
          if(p.value("seat", -1) == 0) net = (float)p.value("net", 0) / table_config().big_blind;
        }
        if(learner_chose){
          result.reward = net;
          result.done = true;
        }
        else{
          result.other_hands++;
          result.other_net += net;
        }
        hand_running = false;
        if(options.session_hands > 1) reads.add_hand(game->last_hand());
      }
      start_hand();
      if(game->get_stage() == WAITING) return;   // the table refused to start: give up
      continue;
    }
    int seat = game->get_turn();
    json state = json::parse(out.last_state[client_of(seat)]);
    if(seat == 0){
      view = read_bot_view(state);
      seen = rl_observe(view, rng, &reads);
      return;
    }
    bot_move move = decide(state, styles[seat], rng, models[seat], &reads);
    play(seat, move, state["actions"].value("check", false));
  }
}

rl_step rl_env::step(int action){
  rl_step result;
  if(action < 0 || action >= RL_ACTIONS || !seen.allowed[action]){
    action = RL_CALL;
  }
  learner_chose = true;
  play(0, rl_move(view, action), view.can_check);
  play_to_learner(result);
  return result;
}
