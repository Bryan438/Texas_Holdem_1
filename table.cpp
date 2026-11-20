#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include "table.h"
#include "bot.h"
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
    bot_rng.seed(config.seed + 1);
  }
  else{
    bot_rng.seed(std::random_device()());
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
    send_error(client, "bad message: expected a JSON object with a \"type\" field", "bad_message");
    return;
  }
  std::string type = msg["type"].get<std::string>();

  if(type == "join"){
    handle_join(client, msg);
    return;
  }
  if(seat_of(client) < 0){
    send_error(client, "join the table first", "join_first");
    return;
  }
  if(type == "start"){
    handle_start(client);
  }
  else if(type == "action"){
    handle_action(client, msg);
  }
  else if(type == "end_game"){
    handle_end_game(client);
  }
  else if(type == "new_game"){
    handle_new_game(client);
  }
  else if(type == "add_bot"){
    handle_add_bot(client, msg);
  }
  else if(type == "remove_bot"){
    handle_remove_bot(client, msg);
  }
  else if(type == "vote"){
    handle_vote(client, msg);
  }
  else if(type == "leave"){
    remove_player(seat_of(client), false);
  }
  else{
    send_error(client, "unknown message type: " + type, "bad_message");
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
  p.gone_since = now_ms();
  // With no time to come back, they no longer vote: if they were the last one
  // not to agree yet, the vote passes now (and settles the hand).
  if(check_vote()){
    return;
  }
  bool playing = current_stage != WAITING && p.in_hand;

  // A reloaded page or a network hiccup should not cost the hand: keep the
  // cards for a while. A player who is all-in has nothing left to decide and
  // simply stays in.
  if(playing && config.disconnect_grace_seconds > 0){
    if(p.all_in){
      event(p.name + " disconnected", {{"kind", "disconnected"}, {"name", p.name}});
    }
    else{
      p.away_until = now_ms() + config.disconnect_grace_seconds * 1000LL;
      event(p.name + " disconnected (" + std::to_string(config.disconnect_grace_seconds) + " s to come back)",
            {{"kind", "disconnected"}, {"name", p.name}, {"grace", config.disconnect_grace_seconds}});
    }
    broadcast_state();
    return;
  }

  event(p.name + " disconnected", {{"kind", "disconnected"}, {"name", p.name}});
  if(playing && fold_absent(s)){
    return;
  }
  broadcast_state();
}

// Fold a player who is gone. True when that passed the turn on or ended the
// hand, in which case everyone has already been sent the new state.
bool table::fold_absent(int s){
  player& p = *seats[s];
  p.away_until = 0;
  fold_seat(s);
  record_action(s, "fold", 0, 0, "disconnected");
  event(p.name + " folds (disconnected)", {{"kind", "fold"}, {"name", p.name}, {"reason", "disconnected"}});
  if(turn == s || count_in_hand() <= 1){
    continue_hand(s);
    return true;
  }
  return false;
}

void table::handle_join(int client, const json& msg){
  if(seat_of(client) >= 0){
    send_error(client, "you are already seated", "already_seated");
    return;
  }
  if(!msg.contains("name") || !msg["name"].is_string()){
    send_error(client, "join needs a \"name\"", "name_invalid");
    return;
  }
  std::string name = msg["name"].get<std::string>();
  name.erase(0, name.find_first_not_of(' '));
  name.erase(name.find_last_not_of(' ') + 1);
  bool printable = std::all_of(name.begin(), name.end(), [](char c){ return (unsigned char)c >= 0x20; });
  if(name.empty() || name.size() > 24 || !printable){
    send_error(client, "name must be 1-24 printable characters", "name_invalid");
    return;
  }

  // Same name as a disconnected player: take that seat (and its chips) back,
  // but only with the token that player was given, so nobody else can.
  for(size_t i = 0; i < seats.size(); i++){
    if(seats[i]->name == name){
      if(seats[i]->leaving){
        send_error(client, "\"" + name + "\" has just left; the seat is free once this hand is over",
                   "name_leaving");
        return;
      }
      if(seats[i]->connected){
        send_error(client, "name \"" + name + "\" is taken", "name_taken");
        return;
      }
      std::string token = msg.contains("token") && msg["token"].is_string() ? msg["token"].get<std::string>() : "";
      if(token != seats[i]->token){
        send_error(client, "name \"" + name + "\" belongs to a player who disconnected; "
                           "only their client can take the seat back", "name_reserved");
        return;
      }
      seats[i]->socket = client;
      seats[i]->connected = true;
      seats[i]->away_until = 0;   // back in time: the hand goes on with their cards
      send(client, {{"type", "welcome"}, {"seat", (int)i}, {"name", name}, {"token", token}});
      event(name + " reconnected", {{"kind", "reconnected"}, {"name", name}});
      broadcast_state();
      return;
    }
  }

  int s = take_seat();
  if(s < 0){
    send_error(client, "the table is full", "table_full");
    return;
  }

  player& p = *seats[s];
  p.socket = client;
  p.name = name;
  p.stack = config.starting_stack;
  p.buy_in = config.starting_stack;
  p.bot = msg.contains("bot") && msg["bot"].is_boolean() && msg["bot"].get<bool>();
  p.token = make_token();
  send(client, {{"type", "welcome"}, {"seat", s}, {"name", name}, {"token", p.token}});
  event(name + " joined" + (current_stage == WAITING ? "" : " (plays from the next hand)"),
        {{"kind", "joined"}, {"name", name}, {"next_hand", current_stage != WAITING}});
  broadcast_state();
}

void table::handle_start(int client){
  if(game_over){
    // Dealing again after the game ended begins a new one, so that a client
    // that only knows "start" is never stuck. Bots do not decide that.
    const player& p = *seats[seat_of(client)];
    if(p.bot){
      send_error(client, "the game is over: a player starts a new one", "game_over");
      return;
    }
    reset_game(p.name);
    broadcast_state();
  }
  if(current_stage != WAITING){
    send_error(client, "a hand is already running", "hand_running");
    return;
  }
  if(!start_hand()){
    send_error(client, "need at least 2 connected players with chips", "not_enough_players");
  }
}

// A free seat, made empty: a new one, or between hands the seat of someone
// who left. -1 when the table is full.
int table::take_seat(){
  if(seats.size() < MAX_PLAYERS){
    seats.emplace_back(new player());
    return seats.size() - 1;
  }
  if(current_stage == WAITING){
    for(size_t i = 0; i < seats.size(); i++){
      if(!seats[i]->connected){
        seats[i].reset(new player());
        return i;
      }
    }
  }
  return -1;
}

// "RL 1", "Equity 2": the style and the smallest number nobody uses.
std::string table::house_bot_name(const std::string& style) const{
  std::string label = style == "rl" ? "RL" : style;
  if(style != "rl" && !label.empty()) label[0] = std::toupper((unsigned char)label[0]);
  for(int n = 1; ; n++){
    std::string name = label + " " + std::to_string(n);
    bool used = std::any_of(seats.begin(), seats.end(), [&](const std::unique_ptr<player>& p){ return p->name == name; });
    if(!used) return name;
  }
}

// A person seats bots that the server plays itself: no program to start, no
// connection. They join like anyone else (from the next hand if one is running).
void table::handle_add_bot(int client, const json& msg){
  const player& by = *seats[seat_of(client)];
  if(by.bot){
    send_error(client, "bots cannot add or remove bots", "bots_cannot");
    return;
  }
  std::string style = msg.contains("style") && msg["style"].is_string() ? msg["style"].get<std::string>() : "rl";
  bot_style parsed;
  if(!parse_bot_style(style, parsed)){
    send_error(client, "unknown bot style \"" + style + "\" (use " + bot_style_list() + ")", "bad_style");
    return;
  }
  style = bot_style_name(parsed);
  int count = msg.contains("count") && msg["count"].is_number_integer() ? msg["count"].get<int>() : 1;
  count = std::max(1, std::min(count, MAX_PLAYERS));

  int added = 0;
  for(int k = 0; k < count; k++){
    int s = take_seat();
    if(s < 0){
      break;
    }
    player& p = *seats[s];
    p.name = house_bot_name(style);
    p.stack = config.starting_stack;
    p.buy_in = config.starting_stack;
    p.bot = true;
    p.house = true;
    p.style = style;
    p.token = make_token();   // never handed out: nobody can take this seat over
    added++;
    event(by.name + " added the bot " + p.name + (current_stage == WAITING ? "" : " (plays from the next hand)"),
          {{"kind", "bot_added"}, {"name", p.name}, {"by", by.name}, {"style", style},
           {"next_hand", current_stage != WAITING}});
  }
  if(added == 0){
    send_error(client, "the table is full", "table_full");
    return;
  }
  broadcast_state();
}

// Between hands, a bot the server runs leaves the table with its chips.
void table::handle_remove_bot(int client, const json& msg){
  const player& by = *seats[seat_of(client)];
  if(by.bot){
    send_error(client, "bots cannot add or remove bots", "bots_cannot");
    return;
  }
  int s = msg.contains("seat") && msg["seat"].is_number_integer() ? msg["seat"].get<int>() : -1;
  if(s < 0 || s >= (int)seats.size() || !seats[s]->house){
    send_error(client, "only a bot added from the table can be removed", "not_house_bot");
    return;
  }
  if(current_stage != WAITING){
    send_error(client, "wait until the hand is over", "hand_running");
    return;
  }
  std::string name = seats[s]->name;
  std::string by_name = by.name;
  erase_seat(s);
  event(by_name + " removed the bot " + name, {{"kind", "bot_removed"}, {"name", name}, {"by", by_name}});
  broadcast_state();
}

// A house bot's move, decided from what its seat may see, like any bot.
void table::play_house_bot(int s){
  player& p = *seats[s];
  bot_style style = bot_style::RL;
  parse_bot_style(p.style, style);
  bot_move move = decide(state_for(s), style, bot_rng);
  std::string error;
  if(!apply_action(s, move.action, move.amount, error)){
    // Cannot happen (decide keeps to the allowed moves), but never stall the table.
    apply_action(s, to_call(p) == 0 ? "check" : "fold", 0, error);
  }
  continue_hand(s);
}

// Is this seat a person, with no other person connected? Then the game is
// theirs to end or restart: nobody else is affected.
bool table::alone_with_bots(int s) const{
  if(s < 0 || s >= (int)seats.size() || seats[s]->bot || !seats[s]->connected){
    return false;
  }
  for(int i = 0; i < (int)seats.size(); i++){
    if(i != s && seats[i]->connected && !seats[i]->bot){
      return false;
    }
  }
  return true;
}

void table::handle_end_game(int client){
  int s = seat_of(client);
  if(seats[s]->bot){
    send_error(client, "bots cannot end or restart the game", "bots_cannot");
    return;
  }
  if(game_over){
    send_error(client, "the game is already over", "game_over");
    return;
  }
  propose(s, "end");
}

void table::handle_new_game(int client){
  int s = seat_of(client);
  if(seats[s]->bot){
    send_error(client, "bots cannot end or restart the game", "bots_cannot");
    return;
  }
  // Once the game is over nothing is lost by starting the next one.
  if(game_over){
    clear_vote();
    reset_game(seats[s]->name);
    broadcast_state();
    return;
  }
  propose(s, "new");
}

// Ending or restarting throws away everyone's chips, so every person at the
// table has to agree. A person alone with bots agrees with themselves at once.
void table::propose(int s, const std::string& kind){
  const std::string name = seats[s]->name;
  if(!vote_kind.empty()){
    if(vote_kind != kind){
      send_error(seats[s]->socket, "another vote is open: answer it first", "vote_open");
      return;
    }
    json agree = {{"type", "vote"}, {"agree", true}};
    handle_vote(seats[s]->socket, agree);   // asking for the same thing is a yes
    return;
  }
  vote_kind = kind;
  vote_by = name;
  vote_yes = {name};
  vote_deadline = now_ms() + VOTE_SECONDS * 1000LL;
  if(check_vote()){
    return;
  }
  std::string what = kind == "end" ? "end the game" : "start a new game";
  event(name + " wants to " + what + ": everyone at the table must agree within " +
        std::to_string(VOTE_SECONDS) + " s",
        {{"kind", "vote_started"}, {"vote", kind}, {"name", name}, {"seconds", VOTE_SECONDS}});
  broadcast_state();
}

void table::handle_vote(int client, const json& msg){
  int s = seat_of(client);
  if(seats[s]->bot){
    send_error(client, "bots cannot end or restart the game", "bots_cannot");
    return;
  }
  if(vote_kind.empty()){
    send_error(client, "there is no vote to answer", "no_vote");
    return;
  }
  if(!msg.contains("agree") || !msg["agree"].is_boolean()){
    send_error(client, "vote needs \"agree\": true or false", "bad_message");
    return;
  }
  const std::string name = seats[s]->name;
  if(!msg["agree"].get<bool>()){
    std::string kind = vote_kind;
    clear_vote();
    event(name + " said no: the game goes on", {{"kind", "vote_declined"}, {"vote", kind}, {"name", name}});
    broadcast_state();
    return;
  }
  if(std::find(vote_yes.begin(), vote_yes.end(), name) != vote_yes.end()){
    return;   // already counted
  }
  vote_yes.push_back(name);
  if(check_vote()){
    return;
  }
  event(name + " agreed", {{"kind", "vote_agreed"}, {"vote", vote_kind}, {"name", name}});
  broadcast_state();
}

// Has every person still here agreed? Then do it (and tell everyone) and
// return true. A vote nobody present backs any more is dropped.
bool table::check_vote(){
  if(vote_kind.empty()){
    return false;
  }
  bool backed = false;
  bool everyone = true;
  for(const auto& p : seats){
    if(!votes(*p)){
      continue;
    }
    bool yes = std::find(vote_yes.begin(), vote_yes.end(), p->name) != vote_yes.end();
    backed = backed || yes;
    everyone = everyone && yes;
  }
  if(!backed){
    std::string kind = vote_kind;
    clear_vote();
    event("Whoever asked has left: the game goes on", {{"kind", "vote_dropped"}, {"vote", kind}});
    return false;
  }
  if(!everyone){
    return false;
  }
  std::string kind = vote_kind, by = vote_by;
  bool voted = vote_yes.size() > 1;
  clear_vote();
  if(kind == "end"){
    end_game(by, voted);
  }
  else{
    reset_game(by, voted);
  }
  broadcast_state();
  return true;
}

// Who has a say: every person at the table, including one whose connection
// dropped a moment ago (a reloaded page must not count as a yes). Someone
// gone for longer than the time to come back no longer holds a vote up.
bool table::votes(const player& p) const{
  if(p.bot || p.leaving){
    return false;
  }
  return p.connected || now_ms() - p.gone_since < config.disconnect_grace_seconds * 1000LL;
}

void table::clear_vote(){
  vote_kind.clear();
  vote_by.clear();
  vote_yes.clear();
  vote_deadline = 0;
}

void table::end_game(const std::string& by, bool voted){
  if(current_stage != WAITING){
    cancel_hand();
  }
  game_over = true;
  next_hand_at = 0;
  event(voted ? "Everyone agreed: the game is over" : by + " ended the game",
        {{"kind", "game_over"}, {"name", by}, {"vote", voted}});
}

// Get up from the table: a person who asked to, or anyone the server removes.
// Between hands the seat goes at once. During a hand they fold (all-in, they
// stay in for the showdown) and the seat goes once the hand is over.
void table::remove_player(int s, bool kicked){
  player& p = *seats[s];
  const std::string name = p.name;
  if(p.connected && p.socket >= 0){
    send(p.socket, {{"type", "left"}, {"reason", kicked ? "kicked" : "left"}});
  }
  p.connected = false;
  p.socket = -1;
  p.token.clear();
  p.away_until = 0;
  p.leaving = true;
  event(kicked ? name + " was removed by the server" : name + " left the table",
        {{"kind", kicked ? "kicked" : "left"}, {"name", name}});

  if(current_stage == WAITING){
    erase_seat(s);
    if(!check_vote()){
      broadcast_state();
    }
    return;
  }
  // A vote that passes now ends or restarts the game: the hand is settled.
  if(check_vote()){
    return;
  }
  if(p.in_hand && !p.all_in){
    std::string reason = kicked ? "kicked" : "left";
    fold_seat(s);
    record_action(s, "fold", 0, 0, reason);
    event(name + " folds", {{"kind", "fold"}, {"name", name}, {"reason", reason}});
    if(turn == s || count_in_hand() <= 1){
      continue_hand(s);
      return;
    }
  }
  broadcast_state();
}

// Take a seat away between hands. Everyone after it moves up one seat; the
// button stays with the same player, or, if it was this seat's, goes on to
// whoever sat after it.
void table::erase_seat(int s){
  seats.erase(seats.begin() + s);
  if(dealer >= s){
    dealer--;
  }
  std::vector<hand_result_entry> kept;
  for(hand_result_entry r : last_result){
    if(r.seat == s) continue;
    if(r.seat > s) r.seat--;
    kept.push_back(r);
  }
  last_result = kept;
}

// The hand is over: the seats of those who left during it go now.
void table::purge_leavers(){
  for(int i = (int)seats.size() - 1; i >= 0; i--){
    if(seats[i]->leaving){
      erase_seat(i);
    }
  }
}

bool table::kick(const std::string& name, std::string& error){
  for(int i = 0; i < (int)seats.size(); i++){
    if(seats[i]->name == name && !seats[i]->leaving){
      remove_player(i, true);
      return true;
    }
  }
  error = "nobody called \"" + name + "\" is seated (type players to see who is)";
  return false;
}

std::string table::roster() const{
  if(seats.empty()){
    return "Nobody is seated.\n";
  }
  std::string out;
  for(size_t i = 0; i < seats.size(); i++){
    const player& p = *seats[i];
    std::string what = p.house ? "bot (" + p.style + ")" : p.bot ? "bot" : "person";
    std::string where = p.leaving ? "leaving" : p.connected ? "here" : "disconnected";
    char line[160];
    snprintf(line, sizeof(line), "  %2zu  %-24s %6d chips  %-14s %s\n",
             i, p.name.c_str(), p.stack, what.c_str(), where.c_str());
    out += line;
  }
  return out;
}

// Stop the hand in progress as if it had not been dealt: every chip goes
// back to the player who put it in, and nothing is written to the history.
void table::cancel_hand(){
  for(auto& p : seats){
    p->stack += p->total_bet;
    p->reset_for_hand();
  }
  for(int i = 0; i < 5; i++){
    board[i] = nullptr;
  }
  board_count = 0;
  current_stage = WAITING;
  turn = -1;
  current_bet = 0;
  next_hand_at = 0;
  reveal_cards = false;
  last_result.clear();
  record = json();
  event("Hand #" + std::to_string(hand_count) + " cancelled, bets returned",
        {{"kind", "hand_cancelled"}, {"hand", hand_count}});
  purge_leavers();
}

// A fresh game: everyone back to the starting chips, the next hand is #1.
void table::reset_game(const std::string& by, bool voted){
  if(current_stage != WAITING){
    cancel_hand();
  }
  for(auto& p : seats){
    p->reset_for_hand();
    p->stack = config.starting_stack;
    p->buy_in = config.starting_stack;
  }
  for(int i = 0; i < 5; i++){
    board[i] = nullptr;
  }
  board_count = 0;
  hand_count = 0;
  dealer = -1;
  next_hand_at = 0;
  reveal_cards = false;
  game_over = false;
  last_result.clear();
  std::string chips = std::to_string(config.starting_stack) + " chips";
  event(voted ? "Everyone agreed to a new game: everyone has " + chips
              : by + " started a new game: everyone has " + chips,
        {{"kind", "new_game"}, {"name", by}, {"amount", config.starting_stack}, {"vote", voted}});
}

// Who is up and who is down since the game began, best first.
json table::standings() const{
  std::vector<int> order(seats.size());
  for(size_t i = 0; i < order.size(); i++){
    order[i] = i;
  }
  std::stable_sort(order.begin(), order.end(), [this](int a, int b){
    return seats[a]->stack - seats[a]->buy_in > seats[b]->stack - seats[b]->buy_in;
  });
  json list = json::array();
  for(int i : order){
    const player& p = *seats[i];
    list.push_back({{"seat", i}, {"name", p.name}, {"bot", p.bot}, {"stack", p.stack}, {"net", p.stack - p.buy_in}});
  }
  return list;
}

void table::handle_action(int client, const json& msg){
  int s = seat_of(client);
  if(current_stage == WAITING){
    send_error(client, "no hand is running", "no_hand");
    return;
  }
  if(s != turn){
    send_error(client, "it is not your turn", "not_your_turn");
    return;
  }
  std::string action = msg.contains("action") && msg["action"].is_string() ? msg["action"].get<std::string>() : "";
  int amount = msg.contains("amount") && msg["amount"].is_number_integer() ? msg["amount"].get<int>() : 0;

  std::string error;
  if(!apply_action(s, action, amount, error)){
    send_error(client, error, "bad_action");
    send(client, state_for(s));
    return;
  }
  continue_hand(s);
}

void table::tick(){
  if(!vote_kind.empty() && now_ms() >= vote_deadline){
    std::string kind = vote_kind;
    clear_vote();
    event("Not everyone agreed in time: the game goes on", {{"kind", "vote_expired"}, {"vote", kind}});
    broadcast_state();
  }
  // Someone who dropped out may just have run out of time to come back.
  if(!vote_kind.empty() && !check_vote() && vote_kind.empty()){
    broadcast_state();
  }
  // Players who disconnected during this hand and did not come back in time.
  if(current_stage != WAITING){
    for(size_t i = 0; i < seats.size(); i++){
      player& p = *seats[i];
      if(p.away_until == 0 || now_ms() < p.away_until){
        continue;
      }
      p.away_until = 0;
      if(!p.in_hand || p.connected){
        continue;   // folded on the turn timer meanwhile
      }
      if(fold_absent(i)){
        return;     // the turn moved on or the hand ended: the rest waits for the next tick
      }
      broadcast_state();
    }
  }
  if(current_stage == WAITING){
    if(next_hand_at > 0 && now_ms() >= next_hand_at){
      next_hand_at = 0;
      // Bots the server runs do not play on by themselves once every person has left.
      bool someone = std::any_of(seats.begin(), seats.end(), [](const std::unique_ptr<player>& p){
        return p->connected && !p->house;
      });
      if(!someone){
        return;
      }
      if(!start_hand()){
        event("Not enough players with chips. Waiting for someone to join.", {{"kind", "not_enough_players"}});
        broadcast_state();
      }
    }
    return;
  }
  if(turn >= 0 && seats[turn]->house){
    if(now_ms() >= bot_move_at){
      play_house_bot(turn);
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
  event(p.name + " ran out of time", {{"kind", "timeout"}, {"name", p.name}});
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

  event("Hand #" + std::to_string(hand_count) + " - dealer " + seats[dealer]->name,
        {{"kind", "hand_start"}, {"hand", hand_count}, {"name", seats[dealer]->name}});
  event(seats[small_blind_seat]->name + " posts small blind " + std::to_string(sb),
        {{"kind", "small_blind"}, {"name", seats[small_blind_seat]->name}, {"amount", sb}});
  event(seats[big_blind_seat]->name + " posts big blind " + std::to_string(bb),
        {{"kind", "big_blind"}, {"name", seats[big_blind_seat]->name}, {"amount", bb}});

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
    event(p.name + " folds", {{"kind", "fold"}, {"name", p.name}});
    return true;
  }
  if(action == "check"){
    if(call_amount > 0){
      error = "cannot check: " + std::to_string(call_amount) + " to call";
      return false;
    }
    p.acted = true;
    record_action(s, "check", 0, 0, reason);
    event(p.name + " checks", {{"kind", "check"}, {"name", p.name}});
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
    event(p.name + " calls " + std::to_string(put) + (p.all_in ? " and is all-in" : ""),
          {{"kind", "call"}, {"name", p.name}, {"amount", put}, {"allin", p.all_in}});
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
    event(p.name + (was_bet ? " bets " : " raises to ") + std::to_string(amount) + (p.all_in ? " (all-in)" : ""),
          {{"kind", was_bet ? "bet" : "raise"}, {"name", p.name}, {"amount", amount}, {"allin", p.all_in}});
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
    event("Flop: " + board[0]->text() + " " + board[1]->text() + " " + board[2]->text(),
          {{"kind", "flop"}, {"cards", {board[0]->code(), board[1]->code(), board[2]->code()}}});
  }
  else{
    board[board_count++] = deck->get_card(card_index++);
    current_stage = current_stage == FLOP ? TURN : RIVER;
    event(std::string(current_stage == TURN ? "Turn: " : "River: ") + board[board_count - 1]->text(),
          {{"kind", current_stage == TURN ? "turn" : "river"}, {"cards", {board[board_count - 1]->code()}}});
  }
}

void table::finish_hand(){
  int n = seats.size();

  // A bet that nobody matched goes back to the bettor before the pot is paid.
  // Those chips were never at risk, so they are not part of the pot and not
  // part of what the winner "wins". It is whatever the biggest contribution
  // exceeds the second biggest by.
  int top = -1, second = 0;
  for(int i = 0; i < n; i++){
    int put = seats[i]->total_bet;
    if(top < 0 || put > seats[top]->total_bet){
      if(top >= 0){
        second = std::max(second, seats[top]->total_bet);
      }
      top = i;
    }
    else{
      second = std::max(second, put);
    }
  }
  json returned = json::array();
  if(top >= 0 && seats[top]->in_hand && seats[top]->total_bet > second){
    player& p = *seats[top];
    int back = p.total_bet - second;
    p.total_bet -= back;
    p.stack += back;
    returned.push_back({{"seat", top}, {"name", p.name}, {"amount", back}});
    event("Uncalled bet of " + std::to_string(back) + " returned to " + p.name,
          {{"kind", "uncalled"}, {"name", p.name}, {"amount", back}});
  }
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
            hand_category_name(hand_category_of(p.score)),
            {{"kind", "show"}, {"name", p.name}, {"cards", {p.cards[0]->code(), p.cards[1]->code()}},
             {"hand", hand_category_name(hand_category_of(p.score))}});
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
    event(seats[i]->name + " wins " + std::to_string(payout[i]) + (hand.empty() ? "" : " with " + hand),
          {{"kind", "win"}, {"name", seats[i]->name}, {"amount", payout[i]}, {"hand", hand}});
  }

  // Complete the hand record.
  json board_codes = json::array();
  for(int i = 0; i < board_count; i++){
    board_codes.push_back(board[i]->code());
  }
  record["board"] = board_codes;
  record["pot"] = std::accumulate(contributions.begin(), contributions.end(), 0);
  record["returned"] = returned;   // uncalled bets handed back: in the actions, not in the pot
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
    p->away_until = 0;      // nothing left to come back to
  }
  reveal_cards = showdown;
  current_stage = WAITING;
  turn = -1;
  current_bet = 0;
  purge_leavers();
  if(config.next_hand_seconds > 0){
    next_hand_at = now_ms() + config.next_hand_seconds * 1000LL;
    event("Hand over. Next hand in " + std::to_string(config.next_hand_seconds) + " s.",
          {{"kind", "hand_over"}, {"next_hand_in", config.next_hand_seconds}});
  }
  else{
    event("Hand over. Send start for the next hand.", {{"kind", "hand_over"}, {"next_hand_in", 0}});
  }
  broadcast_state();
}

void table::begin_turn(int s){
  turn = s;
  bot_move_at = seats[s]->house ? now_ms() + config.bot_delay_ms : 0;
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
  st["game_over"] = game_over;
  st["solo"] = alone_with_bots(viewer);   // ending or restarting needs nobody else's yes
  if(vote_kind.empty()){
    st["vote"] = nullptr;
  }
  else{
    json waiting = json::array();
    for(const auto& p : seats){
      if(votes(*p) && std::find(vote_yes.begin(), vote_yes.end(), p->name) == vote_yes.end()){
        waiting.push_back(p->name);
      }
    }
    st["vote"] = {{"kind", vote_kind}, {"by", vote_by}, {"agreed", vote_yes}, {"waiting", waiting},
                  {"seconds_left", std::max<int64_t>(0, (vote_deadline - now_ms() + 999) / 1000)}};
  }
  if(game_over){
    st["standings"] = standings();
  }
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
    if(p.leaving){
      status = "left";
    }
    else if(!p.connected){
      status = "disconnected";
    }
    else if(current_stage != WAITING){
      status = !p.dealt ? "sitting_out" : !p.in_hand ? "folded" : p.all_in ? "allin" : "active";
    }
    else{
      status = p.stack > 0 ? "ready" : "busted";
    }

    json s = {{"seat", (int)i}, {"name", p.name}, {"stack", p.stack}, {"bet", p.round_bet},
              {"total_bet", p.total_bet}, {"status", status}, {"bot", p.bot}};
    if(p.house){
      s["house"] = true;     // run by the server: a person may remove it between hands
      s["style"] = p.style;
    }
    if(p.away_until > 0){
      // disconnected but still holding cards: seconds left to come back
      s["reconnect_in"] = std::max<int64_t>(0, (p.away_until - now_ms() + 999) / 1000);
    }
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

// `text` is the line the terminal client prints. `data` says the same thing
// in fields ("kind" plus who, how much, which cards), so that a client can
// word the line itself, for example in another language.
void table::event(const std::string& text, json data){
  if(config.log_events){
    printf("[table] %s\n", text.c_str());
  }
  json msg = data.is_object() ? data : json::object();
  msg["type"] = "event";
  msg["text"] = text;
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

// `code` names the kind of error for clients that word it themselves.
void table::send_error(int client, const std::string& message, const std::string& code){
  send(client, {{"type", "error"}, {"message", message}, {"code", code}});
}
