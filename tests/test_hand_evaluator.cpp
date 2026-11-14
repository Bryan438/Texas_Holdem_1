#include <random>
#include <string>
#include <vector>
#include "test_framework.h"
#include "hand_evaluator.h"
#include "deck_of_card.h"

// Parse "As Kd 10h 2c" into cards. Suits: s d h c. Ranks: 2-10, J Q K A.
static std::vector<Card> parse(const std::string& text){
  std::vector<Card> cards;
  size_t i = 0;
  while(i < text.size()){
    if(text[i] == ' '){
      i++;
      continue;
    }
    int rank;
    if(text.compare(i, 2, "10") == 0){
      rank = 10;
      i += 2;
    }
    else{
      char r = text[i++];
      switch(r){
        case 'J': rank = 11; break;
        case 'Q': rank = 12; break;
        case 'K': rank = 13; break;
        case 'A': rank = 14; break;
        default: rank = r - '0'; break;
      }
    }
    card_suit suit;
    switch(text[i++]){
      case 's': suit = Spade; break;
      case 'd': suit = Diamond; break;
      case 'h': suit = Heart; break;
      default: suit = Club; break;
    }
    cards.push_back(Card(suit, rank));
  }
  return cards;
}

static int score(const std::string& text){
  std::vector<Card> cards = parse(text);
  std::vector<const Card*> ptrs;
  for(const Card& c : cards){
    ptrs.push_back(&c);
  }
  return evaluate_best(ptrs.data(), ptrs.size());
}

static int category(const std::string& text){
  return hand_category_of(score(text));
}

TEST(evaluator_detects_every_category){
  CHECK_EQ(category("As Ks Qs Js 10s 2d 3c"), STRAIGHT_FLUSH);
  CHECK_EQ(category("9h 9d 9s 9c Kd 2c 3h"), FOUR_OF_A_KIND);
  CHECK_EQ(category("Kh Kd Ks 4c 4d 2c 7h"), FULL_HOUSE);
  CHECK_EQ(category("2h 7h 9h Jh Kh 3c 4d"), FLUSH);
  CHECK_EQ(category("5c 6d 7h 8s 9c Kd 2h"), STRAIGHT);
  CHECK_EQ(category("Qc Qd Qh 2s 7c 9d Kh"), THREE_OF_A_KIND);
  CHECK_EQ(category("Jc Jd 4h 4s Ac 9d 2h"), TWO_PAIR);
  CHECK_EQ(category("10c 10d 4h 6s Ac 9d 2h"), ONE_PAIR);
  CHECK_EQ(category("Ac Jd 9h 7s 5c 3d 2h"), HIGH_CARD);
}

// The old check_flush() swapped the diamond and heart counters,
// so 5 diamonds or 5 hearts were not seen as a flush.
TEST(evaluator_flush_in_diamonds_and_hearts){
  CHECK_EQ(category("2d 5d 8d Jd Kd 3c 4s"), FLUSH);
  CHECK_EQ(category("2h 5h 8h Jh Kh 3c 4s"), FLUSH);
  CHECK_EQ(category("2d 5d 8d Jd 4h 3c Ks"), HIGH_CARD); // only four diamonds
}

TEST(evaluator_wheel_straight_and_straight_flush){
  CHECK_EQ(category("Ac 2d 3h 4s 5c 9d Kh"), STRAIGHT);
  CHECK_EQ(category("Ah 2h 3h 4h 5h 9d Kc"), STRAIGHT_FLUSH);
  // The wheel is the lowest straight: 6-high beats it.
  CHECK(score("2d 3h 4s 5c 6c Jd Kh") > score("Ac 2d 3h 4s 5c Jd Kh"));
  // Q-K-A-2-3 does not wrap around.
  CHECK_EQ(category("Qc Kd Ah 2s 3c 8d 9h"), HIGH_CARD);
}

TEST(evaluator_straight_with_paired_board){
  CHECK_EQ(category("Kc Kd Jh 10s 9h 8c 7c"), STRAIGHT);
  CHECK(score("Kc Kd Qh Js 10h 9c 2c") > score("Kc Kd Jh 10s 9h 8c 2c"));
}

TEST(evaluator_picks_best_five_of_seven){
  // Flush beats the straight also present in these cards.
  CHECK_EQ(category("4h 5h 6h 7h 8c Jh 2s"), FLUSH);
  // Two trips make a full house with the higher one on top.
  CHECK(score("Jc Jd Jh 9s 9c 9d 2h") > score("9c 9d 9h Js Jc 2d 3h"));
  // Three pairs: best two pairs plus best kicker.
  CHECK(score("Ac Ad Kh Ks 2c 2d Qh") > score("Ac Ad Kh Ks 2c 2d Jh"));
}

TEST(evaluator_kickers_break_ties){
  CHECK(score("Ac Ad Kh 9s 7c 3d 2h") > score("Ac Ad Qh 9s 7c 3d 2h"));
  CHECK(score("Kc Kd 5h 5s Ac 3d 2h") > score("Kc Kd 5h 5s Qc 3d 2h"));
  CHECK(score("9h 9d 9s 9c Ad 2c 3h") > score("9h 9d 9s 9c Kd 2c 3h"));
  CHECK(score("Ah Jh 9h 7h 3h 2c 4d") > score("Ah Jh 9h 7h 2h 3c 4d"));
}

TEST(evaluator_board_plays_is_a_tie){
  // Both players play the board's straight.
  CHECK_EQ(score("5c 6d 7h 8s 9c 2d 2h"), score("5c 6d 7h 8s 9c 3d 3s"));
  // Same two pair, same kicker, different suits.
  CHECK_EQ(score("Ac Ad Kh Ks Qc"), score("Ah As Kc Kd Qd"));
}

TEST(evaluator_needs_five_cards){
  CHECK_EQ(score("Ac Ad Kh Ks"), 0);
}

TEST(deck_has_52_unique_cards_after_shuffle){
  deck_of_card deck;
  for(int round = 0; round < 3; round++){
    deck.shuffle();
    bool seen[5][15] = {};
    int unique = 0;
    for(int i = 0; i < 52; i++){
      Card* c = deck.get_card(i);
      if(!seen[c->get_suit()][c->get_number()]){
        seen[c->get_suit()][c->get_number()] = true;
        unique++;
      }
    }
    CHECK_EQ(unique, 52);
  }
}

// The fast evaluator the bots use must score every hand exactly like the
// brute-force one: compare them on many random 5, 6 and 7 card hands.
TEST(evaluate_fast_matches_evaluate_best){
  std::vector<Card> deck;
  for(card_suit suit : {Diamond, Spade, Heart, Club}){
    for(int rank = 2; rank <= 14; rank++) deck.push_back(Card(suit, rank));
  }
  std::mt19937 rng(2024);
  int mismatches = 0;
  int seen[10] = {0};
  for(int t = 0; t < 100000; t++){
    int n = 5 + t % 3;
    for(int i = 0; i < n; i++){
      std::uniform_int_distribution<int> pick(i, 51);
      std::swap(deck[i], deck[pick(rng)]);
    }
    const Card* cards[7];
    for(int i = 0; i < n; i++) cards[i] = &deck[i];
    int best = evaluate_best(cards, n);
    mismatches += evaluate_fast(cards, n) != best;
    seen[hand_category_of(best)]++;
  }
  CHECK_EQ(mismatches, 0);
  for(int category = HIGH_CARD; category <= STRAIGHT_FLUSH; category++){
    CHECK(seen[category] > 0);      // every kind of hand was compared
  }
  // Hands random deals rarely produce.
  auto fast = [](const std::string& text){
    std::vector<Card> cards = parse(text);
    std::vector<const Card*> ptrs;
    for(const Card& c : cards) ptrs.push_back(&c);
    return evaluate_fast(ptrs.data(), ptrs.size());
  };
  for(const char* hand : {"As Ks Qs Js 10s 2d 3c", "5h 4h 3h 2h Ah Kh 9c", "Kd Kc Ks 9h 9d 9c 2s",
                          "7c 7d 7h 7s Ah Kh Qh", "9h 9d 8c 8s 2h 2d Ac", "Ah 2d 3c 4s 5h 9d 9c"}){
    CHECK_EQ(fast(hand), score(hand));
  }
}
