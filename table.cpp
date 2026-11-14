#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include "table.h"
#include "hand_evaluator.h"
#include "pot_calculator.h"
#include <numeric>

static int64_t steady_ms(){
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// 128 random bits as hex. Proves a reconnecting client is the original player.
static std::string make_token(){
  static const char digits[] = "0123456789abcdef";
  std::random_device rd;
  std::string token;
  for(int i = 0; i < 32; i++){
    token += digits[rd() % 16];
  }
  return token;
}

static std::string utc_now(){
  std::time_t t = std::time(nullptr);
  std::tm tm;
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

static const char* stage_name(stage s){
  switch(s){
    case PREFLOP: return "preflop";
    case FLOP: return "flop";
    case TURN: return "turn";
    case RIVER: return "river";
    default: return "waiting";
  }
}

table::table(sender* o, table_config c, clock_fn clock)
  : out(o), config(c), now_ms(clock ? clock : steady_ms){
  if(config.seed != 0){
    deck->seed(config.seed);
  }
}

table::~table(){
  delete deck;
}

// ---------------------------------------------------------------- messages

void table::on_connect(int client){
  send(client, {{"type", "hello"},
                {"message", "Send {\"type\":\"join\",\"name\":\"<your name>\"} to sit down"}});
}

void table::on_message(int client, const std::string& line){
  json msg = json::parse(line, nullptr, false);
  if(msg.is_discarded() || !msg.is_object() || !msg.contains("type") || !msg["type"].is_string()){
    send_error(client, "bad message: expected a JSON object with a \"type\" field");
    return;
  }
  std::string type = msg["type"].get<std::string>();

  if(type == "join"){
    handle_join(client, msg);
    return;
  }
  if(seat_of(client) < 0){
    send_error(client, "join the table first");
    return;
  }
  if(type == "start"){
    handle_start(client);
  }
  else if(type == "action"){
    handle_action(client, msg);
  }
  else{
    send_error(client, "unknown message type: " + type);
  }
}

void table::on_disconnect(int client){
  int s = seat_of(client);
  if(s < 0){
    return;
  }
  player& p = *seats[s];
  p.connected = false;
  p.socket = -1;
  event(p.name + " disconnected");

  if(current_stage != WAITING && p.in_hand){
    fold_seat(s);
    record_action(s, "fold", 0, 0, "disconnected");
    event(p.name + " folds (disconnected)");
    if(turn == s || count_in_hand() <= 1){
      continue_hand(s);
      return;
    }
  }
  broadcast_state();
}

void table::handle_join(int client, const json& msg){
  if(seat_of(client) >= 0){
    send_error(client, "you are already seated");
    return;
  }
  if(!msg.contains("name") || !msg["name"].is_string()){
    send_error(client, "join needs a \"name\"");
    return;
  }
  std::string name = msg["name"].get<std::string>();
  name.erase(0, name.find_first_not_of(' '));
  name.erase(name.find_last_not_of(' ') + 1);
  bool printable = std::all_of(name.begin(), name.end(), [](char c){ return (unsigned char)c >= 0x20; });
  if(name.empty() || name.size() > 24 || !printable){
    send_error(client, "name must be 1-24 printable characters");
    return;
  }

  // Same name as a disconnected player: take that seat (and its chips) back,
  // but only with the token that player was given, so nobody else can.
  for(size_t i = 0; i < seats.size(); i++){
    if(seats[i]->name == name){
      if(seats[i]->connected){
        send_error(client, "name \"" + name + "\" is taken");
        return;
      }
      std::string token = msg.contains("token") && msg["token"].is_string() ? msg["token"].get<std::string>() : "";
      if(token != seats[i]->token){
        send_error(client, "name \"" + name + "\" belongs to a player who disconnected; "
                           "only their client can take the seat back");
        return;
      }
      seats[i]->socket = client;
      seats[i]->connected = true;
      send(client, {{"type", "welcome"}, {"seat", (int)i}, {"name", name}, {"token", token}});
      event(name + " reconnected");
      broadcast_state();
      return;
    }
  }

  int s = -1;
  if(seats.size() < MAX_PLAYERS){
    seats.emplace_back(new player());
    s = seats.size() - 1;
  }
  else if(current_stage == WAITING){
    for(size_t i = 0; i < seats.size(); i++){
      if(!seats[i]->connected){
        seats[i].reset(new player());
        s = i;
        break;
      }
    }
  }
  if(s < 0){
    send_error(client, "the table is full");
    return;
  }

  player& p = *seats[s];
  p.socket = client;
  p.name = name;
  p.stack = config.starting_stack;
  p.token = make_token();
  send(client, {{"type", "welcome"}, {"seat", s}, {"name", name}, {"token", p.token}});
  event(name + " joined" + (current_stage == WAITING ? "" : " (plays from the next hand)"));
  broadcast_state();
}

void table::handle_start(int client){
  if(current_stage != WAITING){
    send_error(client, "a hand is already running");
    return;
  }
  if(!start_hand()){
    send_error(client, "need at least 2 connected players with chips");
  }
}

void table::handle_action(int client, const json& msg){
  int s = seat_of(client);
  if(current_stage == WAITING){
    send_error(client, "no hand is running");
    return;
  }
  if(s != turn){
    send_error(client, "it is not your turn");
    return;
  }
  std::string action = msg.contains("action") && msg["action"].is_string() ? msg["action"].get<std::string>() : "";
  int amount = msg.contains("amount") && msg["amount"].is_number_integer() ? msg["amount"].get<int>() : 0;

  std::string error;
  if(!apply_action(s, action, amount, error)){
    send_error(client, error);
    send(client, state_for(s));
    return;
  }
  continue_hand(s);
}

void table::tick(){
  if(current_stage == WAITING){
    if(next_hand_at > 0 && now_ms() >= next_hand_at){
      next_hand_at = 0;
      if(!start_hand()){
        event("Not enough players with chips. Waiting for someone to join.");
        broadcast_state();
      }
    }
    return;
  }
  if(turn < 0 || config.turn_seconds <= 0){
    return;
  }
  if(now_ms() < turn_deadline){
    return;
  }
  int s = turn;
  player& p = *seats[s];
  event(p.name + " ran out of time");
  std::string error;
  apply_action(s, to_call(p) == 0 ? "check" : "fold", 0, error, "timeout");
  continue_hand(s);
}

// ---------------------------------------------------------------- game flow

bool table::start_hand(){
  auto eligible = [](const player& p){ return p.connected && p.stack > 0; };
  int ready = std::count_if(seats.begin(), seats.end(), [&](const std::unique_ptr<player>& p){ return eligible(*p); });
  if(ready < 2){
    return false;
  }
  next_hand_at = 0;

  for(auto& p : seats){
    p->reset_for_hand();
    if(eligible(*p)){
      p->dealt = true;
      p->in_hand = true;
    }
  }
  for(int i = 0; i < 5; i++){
    board[i] = nullptr;
  }
  board_count = 0;
  card_index = 0;
  reveal_cards = false;
  last_result.clear();
  hand_count++;

  auto dealt_in = [](const player& p){ return p.in_hand; };
  dealer = next_seat(dealer < 0 ? (int)seats.size() - 1 : dealer, dealt_in);
  // Heads-up: the dealer posts the small blind.
  small_blind_seat = ready == 2 ? dealer : next_seat(dealer, dealt_in);
  big_blind_seat = next_seat(small_blind_seat, dealt_in);

  deck->shuffle();
  for(auto& p : seats){
    if(p->in_hand){
      p->cards[0] = deck->get_card(card_index++);
      p->cards[1] = deck->get_card(card_index++);
    }
  }

  current_stage = PREFLOP;
  record = {{"hand", hand_count}, {"started_at", utc_now()},
            {"blinds", {config.small_blind, config.big_blind}},
            {"dealer", dealer}, {"small_blind", small_blind_seat}, {"big_blind", big_blind_seat},
            {"players", json::array()}, {"actions", json::array()}};
  for(size_t i = 0; i < seats.size(); i++){
    const player& p = *seats[i];
    if(p.dealt){
      record["players"].push_back({{"seat", (int)i}, {"name", p.name}, {"stack", p.stack},
                                   {"cards", {p.cards[0]->code(), p.cards[1]->code()}}});
    }
  }

  int sb = seats[small_blind_seat]->put_in(config.small_blind);
  int bb = seats[big_blind_seat]->put_in(config.big_blind);
  record_action(small_blind_seat, "small_blind", sb, 0, "");
  record_action(big_blind_seat, "big_blind", bb, 0, "");
  current_bet = config.big_blind;
  last_raise_size = config.big_blind;

  event("Hand #" + std::to_string(hand_count) + " - dealer " + seats[dealer]->name);
  event(seats[small_blind_seat]->name + " posts small blind " + std::to_string(sb));
  event(seats[big_blind_seat]->name + " posts big blind " + std::to_string(bb));

  continue_hand(big_blind_seat);
  return true;
}

bool table::apply_action(int s, const std::string& action, int amount, std::string& error,
                         const std::string& reason){
  player& p = *seats[s];
  int call_amount = to_call(p);

  if(action == "fold"){
    fold_seat(s);
    record_action(s, "fold", 0, 0, reason);
    event(p.name + " folds");
    return true;
  }
  if(action == "check"){
    if(call_amount > 0){
      error = "cannot check: " + std::to_string(call_amount) + " to call";
      return false;
    }
    p.acted = true;
    record_action(s, "check", 0, 0, reason);
    event(p.name + " checks");
    return true;
  }
  if(action == "call"){
    if(call_amount == 0){
      error = "nothing to call, check instead";
      return false;
    }
    int put = p.put_in(call_amount);
    p.acted = true;
    record_action(s, "call", put, 0, reason);
    event(p.name + " calls " + std::to_string(put) + (p.all_in ? " and is all-in" : ""));
    return true;
  }
  if(action == "raise"){
    int max_to = p.round_bet + p.stack;
    if(p.stack <= call_amount){
      error = "not enough chips to raise: call (all-in) or fold";
      return false;
    }
    int min_to = std::min(get_min_raise_to(), max_to);
    if(amount < min_to || amount > max_to){
      error = "raise must be to between " + std::to_string(min_to) + " and " + std::to_string(max_to);
      return false;
    }
    bool was_bet = current_bet == 0;
    int raise_size = amount - current_bet;
    int put = p.put_in(amount - p.round_bet);
    p.acted = true;
    record_action(s, was_bet ? "bet" : "raise", put, amount, reason);
    // A full raise reopens the betting for everyone else. A short all-in does
    // not, though players facing it still have to call the difference.
    if(raise_size >= last_raise_size){
      last_raise_size = raise_size;
      for(auto& other : seats){
        if(other.get() != &p && other->in_hand && !other->all_in){
          other->acted = false;
        }
      }
    }
    current_bet = amount;
    event(p.name + (was_bet ? " bets " : " raises to ") + std::to_string(amount) + (p.all_in ? " (all-in)" : ""));
    return true;
  }
  error = "unknown action \"" + action + "\" (use fold, check, call or raise)";
  return false;
}

// After any change: give the turn to the next player, deal the next street,
// or end the hand.
void table::continue_hand(int from){
  if(count_in_hand() <= 1){
    finish_hand();
    return;
  }
  int next = next_seat(from, [this](const player& p){ return needs_to_act(p); });
  if(next >= 0){
    begin_turn(next);
    broadcast_state();
    return;
  }
  // Betting round is over.
  while(true){
    if(current_stage == RIVER){
      finish_hand();
      return;
    }
    deal_next_street();
    if(count_can_act() >= 2){
      begin_turn(next_seat(dealer, [this](const player& p){ return needs_to_act(p); }));
      broadcast_state();
      return;
    }
    // Everyone (or all but one) is all-in: no betting, keep dealing.
  }
}

void table::deal_next_street(){
  for(auto& p : seats){
    p->reset_for_round();
  }
  current_bet = 0;
  last_raise_size = config.big_blind;
  turn = -1;

  if(current_stage == PREFLOP){
    for(int i = 0; i < 3; i++){
      board[board_count++] = deck->get_card(card_index++);
    }
    current_stage = FLOP;
    event("Flop: " + board[0]->text() + " " + board[1]->text() + " " + board[2]->text());
  }
  else{
    board[board_count++] = deck->get_card(card_index++);
    current_stage = current_stage == FLOP ? TURN : RIVER;
    event(std::string(current_stage == TURN ? "Turn: " : "River: ") + board[board_count - 1]->text());
  }
}

void table::finish_hand(){
  int n = seats.size();
  std::vector<int> contributions(n, 0);
  std::vector<bool> in_hand(n, false);
  std::vector<int> scores(n, 0);

  bool showdown = count_in_hand() > 1 && board_count == 5;
  for(int i = 0; i < n; i++){
    player& p = *seats[i];
    contributions[i] = p.total_bet;
    in_hand[i] = p.in_hand;
    if(p.in_hand && showdown){
      const Card* seven[7] = {board[0], board[1], board[2], board[3], board[4], p.cards[0], p.cards[1]};
      p.score = evaluate_best(seven, 7);
      scores[i] = p.score;
      event(p.name + " shows " + p.cards[0]->text() + " " + p.cards[1]->text() + " - " +
            hand_category_name(hand_category_of(p.score)));
    }
  }

  std::vector<int> payout = distribute_pot(contributions, in_hand, scores, (dealer + 1) % n);

  last_result.clear();
  for(int i = 0; i < n; i++){
    if(payout[i] <= 0){
      continue;
    }
    seats[i]->stack += payout[i];
    std::string hand = showdown && in_hand[i] ? hand_category_name(hand_category_of(scores[i])) : "";
    last_result.push_back({i, payout[i], hand});
    event(seats[i]->name + " wins " + std::to_string(payout[i]) + (hand.empty() ? "" : " with " + hand));
  }

  // Complete the hand record.
  json board_codes = json::array();
  for(int i = 0; i < board_count; i++){
    board_codes.push_back(board[i]->code());
  }
  record["board"] = board_codes;
  record["pot"] = std::accumulate(contributions.begin(), contributions.end(), 0);
  record["showdown"] = json::array();
  if(showdown){
    for(int i = 0; i < n; i++){
      if(in_hand[i]){
        record["showdown"].push_back({{"seat", i}, {"name", seats[i]->name},
                                      {"cards", {seats[i]->cards[0]->code(), seats[i]->cards[1]->code()}},
                                      {"hand", hand_category_name(hand_category_of(scores[i]))}});
      }
    }
  }
  record["winners"] = json::array();
  for(const hand_result_entry& r : last_result){
    record["winners"].push_back({{"seat", r.seat}, {"name", seats[r.seat]->name}, {"won", r.amount}});
  }
  for(json& pl : record["players"]){
    int seat = pl["seat"].get<int>();
    pl["end_stack"] = seats[seat]->stack;
    pl["net"] = seats[seat]->stack - pl["stack"].get<int>();
  }
  last_record = record;
  save_record();
  for(size_t i = 0; i < seats.size(); i++){
    if(seats[i]->connected){
      send(seats[i]->socket, summary_for(i));
    }
  }

  for(auto& p : seats){
    p->reset_for_round();   // no bets left on the table between hands
  }
  reveal_cards = showdown;
  current_stage = WAITING;
  turn = -1;
  current_bet = 0;
  if(config.next_hand_seconds > 0){
    next_hand_at = now_ms() + config.next_hand_seconds * 1000LL;
    event("Hand over. Next hand in " + std::to_string(config.next_hand_seconds) + " s.");
  }
  else{
    event("Hand over. Send start for the next hand.");
  }
  broadcast_state();
}

void table::begin_turn(int s){
  turn = s;
  turn_deadline = config.turn_seconds > 0 ? now_ms() + config.turn_seconds * 1000LL : 0;
}

void table::fold_seat(int s){
  seats[s]->in_hand = false;
  seats[s]->acted = true;
}

// ---------------------------------------------------------------- hand history

void table::record_action(int s, const std::string& action, int amount, int to, const std::string& reason){
  json a = {{"street", stage_name(current_stage)}, {"seat", s}, {"name", seats[s]->name},
            {"action", action}, {"amount", amount}};
  if(action == "bet" || action == "raise"){
    a["to"] = to;
  }
  if(seats[s]->all_in && amount > 0){
    a["allin"] = true;
  }
  if(!reason.empty()){
    a["reason"] = reason;
  }
  record["actions"].push_back(a);
}

void table::save_record(){
  if(config.history_path.empty()){
    return;
  }
  std::ofstream out(config.history_path, std::ios::app);
  out << last_record.dump(-1, ' ', false, json::error_handler_t::replace) << "\n";
  if(!out){
    printf("Could not write hand history to %s\n", config.history_path.c_str());
  }
}

// The finished hand as one player may see it: their own cards, plus the
// cards that were shown at showdown. Never anyone else's folded cards.
json table::summary_for(int viewer) const{
  json summary = last_record;
  summary["type"] = "hand_summary";
  summary["you"] = viewer;
  for(json& pl : summary["players"]){
    if(pl["seat"].get<int>() != viewer){
      pl.erase("cards");
    }
  }
  return summary;
}

// ---------------------------------------------------------------- helpers

int table::next_seat(int from, const std::function<bool(const player&)>& pred) const{
  int n = seats.size();
  for(int k = 1; k <= n; k++){
    int s = ((from + k) % n + n) % n;
    if(pred(*seats[s])){
      return s;
    }
  }
  return -1;
}

bool table::needs_to_act(const player& p) const{
  return p.in_hand && !p.all_in && (!p.acted || p.round_bet < current_bet);
}

int table::count_in_hand() const{
  return std::count_if(seats.begin(), seats.end(), [](const std::unique_ptr<player>& p){ return p->in_hand; });
}

int table::count_can_act() const{
  return std::count_if(seats.begin(), seats.end(), [](const std::unique_ptr<player>& p){ return p->in_hand && !p->all_in; });
}

int table::to_call(const player& p) const{
  return std::max(0, current_bet - p.round_bet);
}

int table::get_pot() const{
  if(current_stage == WAITING){
    return 0;
  }
  int pot = 0;
  for(auto& p : seats){
    pot += p->total_bet;
  }
  return pot;
}

int table::get_min_raise_to() const{
  return current_bet + last_raise_size;
}

int table::seat_of(int client) const{
  for(size_t i = 0; i < seats.size(); i++){
    if(seats[i]->connected && seats[i]->socket == client){
      return i;
    }
  }
  return -1;
}

// ---------------------------------------------------------------- output

json table::state_for(int viewer) const{
  json st;
  st["type"] = "state";
  st["hand"] = hand_count;
  st["stage"] = stage_name(current_stage);
  st["you"] = viewer;
  st["pot"] = get_pot();
  st["current_bet"] = current_bet;
  st["dealer"] = dealer;
  st["small_blind"] = current_stage == WAITING ? -1 : small_blind_seat;
  st["big_blind"] = current_stage == WAITING ? -1 : big_blind_seat;
  st["turn"] = turn;
  st["blinds"] = {config.small_blind, config.big_blind};
  if(current_stage == WAITING && next_hand_at > 0){
    st["next_hand_in"] = std::max<int64_t>(0, (next_hand_at - now_ms() + 999) / 1000);
  }
  else{
    st["next_hand_in"] = nullptr;
  }

  if(turn >= 0 && config.turn_seconds > 0){
    st["turn_time_left"] = std::max<int64_t>(0, (turn_deadline - now_ms() + 999) / 1000);
  }
  else{
    st["turn_time_left"] = nullptr;
  }

  json board_codes = json::array();
  for(int i = 0; i < board_count; i++){
    board_codes.push_back(board[i]->code());
  }
  st["board"] = board_codes;

  json seat_list = json::array();
  for(size_t i = 0; i < seats.size(); i++){
    const player& p = *seats[i];
    std::string status;
    if(!p.connected){
      status = "disconnected";
    }
    else if(current_stage != WAITING){
      status = !p.dealt ? "sitting_out" : !p.in_hand ? "folded" : p.all_in ? "allin" : "active";
    }
    else{
      status = p.stack > 0 ? "ready" : "busted";
    }

    json s = {{"seat", (int)i}, {"name", p.name}, {"stack", p.stack}, {"bet", p.round_bet},
              {"total_bet", p.total_bet}, {"status", status}};
    bool show = p.cards[0] != nullptr && ((int)i == viewer || (reveal_cards && p.in_hand));
    if(show){
      s["cards"] = {p.cards[0]->code(), p.cards[1]->code()};
    }
    else{
      s["cards"] = nullptr;
    }
    if(reveal_cards && p.in_hand && p.score > 0){
      s["hand"] = hand_category_name(hand_category_of(p.score));
    }
    seat_list.push_back(s);
  }
  st["seats"] = seat_list;

  // The viewer's current best hand, once there is a flop.
  if(viewer >= 0 && viewer < (int)seats.size() && seats[viewer]->cards[0] && board_count >= 3){
    const player& p = *seats[viewer];
    const Card* cards[7] = {p.cards[0], p.cards[1]};
    for(int i = 0; i < board_count; i++){
      cards[2 + i] = board[i];
    }
    st["your_hand"] = hand_category_name(hand_category_of(evaluate_best(cards, 2 + board_count)));
  }

  if(current_stage != WAITING && turn == viewer && viewer >= 0){
    const player& p = *seats[viewer];
    int call_amount = to_call(p);
    json actions = {{"fold", true}, {"check", call_amount == 0},
                    {"call", call_amount > 0 ? std::min(call_amount, p.stack) : 0}};
    if(p.stack > call_amount){
      int max_to = p.round_bet + p.stack;
      actions["raise"] = {{"min", std::min(get_min_raise_to(), max_to)}, {"max", max_to}};
    }
    else{
      actions["raise"] = nullptr;
    }
    st["actions"] = actions;
  }

  if(current_stage == WAITING && !last_result.empty()){
    json result = json::array();
    for(const hand_result_entry& r : last_result){
      result.push_back({{"seat", r.seat}, {"name", seats[r.seat]->name}, {"amount", r.amount}, {"hand", r.hand}});
    }
    st["result"] = result;
  }
  return st;
}

void table::broadcast_state(){
  for(size_t i = 0; i < seats.size(); i++){
    if(seats[i]->connected){
      send(seats[i]->socket, state_for(i));
    }
  }
}

void table::event(const std::string& text){
  if(config.log_events){
    printf("[table] %s\n", text.c_str());
  }
  json msg = {{"type", "event"}, {"text", text}};
  for(auto& p : seats){
    if(p->connected){
      send(p->socket, msg);
    }
  }
}

void table::send(int client, const json& msg){
  if(out != nullptr && client >= 0){
    // replace invalid UTF-8 instead of throwing
    out->send_line(client, msg.dump(-1, ' ', false, json::error_handler_t::replace));
  }
}

void table::send_error(int client, const std::string& message){
  send(client, {{"type", "error"}, {"message", message}});
}
