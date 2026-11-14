#include <algorithm>
#include <map>
#include <mutex>
#include <tuple>
#include "equity.h"
#include "hand_evaluator.h"

static int card_id(const Card& c){
  return (c.get_number() - 2) * 4 + (c.get_suit() - 1);
}

// Pair / trips / quads from the board alone, as a hand category.
static int board_category(const Card board[], int count){
  if(count >= 5){
    const Card* cards[5];
    for(int i = 0; i < 5; i++) cards[i] = &board[i];
    return hand_category_of(evaluate_fast(cards, 5));
  }
  int rank_count[15] = {0};
  int pairs = 0, most = 0;
  for(int i = 0; i < count; i++) rank_count[board[i].get_number()]++;
  for(int r = 2; r <= 14; r++){
    pairs += rank_count[r] == 2;
    most = std::max(most, rank_count[r]);
  }
  if(most == 4) return FOUR_OF_A_KIND;
  if(most == 3) return THREE_OF_A_KIND;
  if(pairs == 2) return TWO_PAIR;
  return pairs == 1 ? ONE_PAIR : HIGH_CARD;
}

// The board as the range check needs it, worked out once per simulation.
struct board_info{
  const Card* cards;
  int count;
  int category;    // what the board alone makes
  int top;         // highest rank on the board
};

static bool in_range_on(const Card& a, const Card& b, const board_info& board, opponent_range range){
  if(range == opponent_range::ANY){
    return true;
  }
  if(board.count < 5){
    for(int s = 1; s <= 4; s++){
      int in_hole = (a.get_suit() == s) + (b.get_suit() == s);
      int total = in_hole;
      for(int i = 0; i < board.count; i++) total += board.cards[i].get_suit() == s;
      if(in_hole > 0 && total == 4) return true;    // flush draw
    }
  }
  const Card* cards[7] = {&a, &b};
  for(int i = 0; i < board.count; i++) cards[2 + i] = &board.cards[i];
  int category = hand_category_of(evaluate_fast(cards, 2 + board.count));
  if(range == opponent_range::CONNECTED){
    return category > board.category;               // the hole cards play
  }
  if(category > board.category && (category >= THREE_OF_A_KIND ||
                                   (category == TWO_PAIR && board.category == HIGH_CARD))){
    return true;
  }
  // Top pair or an overpair.
  int ra = a.get_number(), rb = b.get_number();
  if(ra == rb) return ra >= board.top;
  for(int i = 0; i < board.count; i++){
    if(board.cards[i].get_number() == board.top && (ra == board.top || rb == board.top)) return true;
  }
  return false;
}

static board_info describe_board(const Card cards[], int count){
  board_info info = {cards, count, board_category(cards, count), 0};
  for(int i = 0; i < count; i++) info.top = std::max(info.top, cards[i].get_number());
  return info;
}

bool in_range(const Card& a, const Card& b, const std::vector<Card>& board, opponent_range range){
  return in_range_on(a, b, describe_board(board.data(), board.size()), range);
}

double estimate_equity(const std::vector<Card>& hole, const std::vector<Card>& board,
                       int opponents, int iterations, std::mt19937& rng, opponent_range range){
  if(hole.size() != 2 || board.size() > 5 || iterations <= 0) return 0;
  opponents = std::max(1, std::min(opponents, 9));

  // The cards we cannot see.
  bool known[52] = {false};
  for(const Card& c : hole) known[card_id(c)] = true;
  for(const Card& c : board) known[card_id(c)] = true;
  std::vector<Card> deck;
  for(card_suit suit : {Diamond, Spade, Heart, Club}){
    for(int rank = 2; rank <= 14; rank++){
      Card c(suit, rank);
      if(!known[card_id(c)]) deck.push_back(c);
    }
  }

  int board_missing = 5 - (int)board.size();
  int needed = board_missing + 2 * opponents;
  Card full_board[5];
  for(size_t i = 0; i < board.size(); i++) full_board[i] = board[i];
  if(board.size() < 3) range = opponent_range::ANY;
  board_info now = describe_board(full_board, board.size());
  int deck_size = deck.size();
  auto take = [&](int i){   // move a random unused card to position i
    std::uniform_int_distribution<int> pick(i, deck_size - 1);
    std::swap(deck[i], deck[pick(rng)]);
  };

  double won = 0;
  for(int it = 0; it < iterations; it++){
    // Partial shuffle: only the first `needed` cards get used. Opponents'
    // cards come first, so a hand that does not connect can be dealt again.
    for(int o = 0; o < opponents; o++){
      int i = 2 * o;
      for(int attempt = 0; attempt < 50; attempt++){
        take(i);
        take(i + 1);
        if(in_range_on(deck[i], deck[i + 1], now, range)) break;
      }
    }
    for(int i = 2 * opponents; i < needed; i++) take(i);
    for(int i = 0; i < board_missing; i++) full_board[board.size() + i] = deck[2 * opponents + i];

    const Card* seven[7];
    for(int i = 0; i < 5; i++) seven[2 + i] = &full_board[i];
    seven[0] = &hole[0];
    seven[1] = &hole[1];
    int mine = evaluate_fast(seven, 7);

    int tied = 0;
    bool lost = false;
    for(int o = 0; o < opponents && !lost; o++){
      seven[0] = &deck[2 * o];
      seven[1] = &deck[2 * o + 1];
      int theirs = evaluate_fast(seven, 7);
      if(theirs > mine) lost = true;
      else if(theirs == mine) tied++;
    }
    if(!lost) won += 1.0 / (tied + 1);
  }
  return won / iterations;
}

double preflop_equity(const Card& a, const Card& b, int opponents){
  opponents = std::max(1, std::min(opponents, 9));
  int hi = std::max(a.get_number(), b.get_number());
  int lo = std::min(a.get_number(), b.get_number());
  bool suited = a.get_suit() == b.get_suit();
  auto key = std::make_tuple(hi, lo, suited, opponents);

  static std::mutex cache_mutex;
  static std::map<std::tuple<int, int, bool, int>, double> cache;
  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto found = cache.find(key);
    if(found != cache.end()) return found->second;
  }

  // Every hand of the same kind has the same equity, so use one fixed
  // example and a seed made from the key: the answer never changes.
  Card first(Spade, hi);
  Card second(suited ? Spade : Heart, lo);
  std::mt19937 rng(hi * 1000 + lo * 10 + suited + opponents * 100000);
  double equity = estimate_equity({first, second}, {}, opponents, 4000, rng);

  std::lock_guard<std::mutex> lock(cache_mutex);
  cache[key] = equity;
  return equity;
}
