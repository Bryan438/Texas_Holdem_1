#include "hand_evaluator.h"

static int pack(int category, const int ranks[], int count){
  int score = category;
  for(int i = 0; i < 5; i++){
    score <<= 4;
    if(i < count){
      score |= ranks[i];
    }
  }
  return score;
}

int evaluate_five(const Card* const cards[5]){
  int ranks[5];
  bool flush = true;
  for(int i = 0; i < 5; i++){
    ranks[i] = cards[i]->get_number();
    if(cards[i]->get_suit() != cards[0]->get_suit()){
      flush = false;
    }
  }

  // Sort ranks high to low (insertion sort, five items).
  for(int i = 1; i < 5; i++){
    int v = ranks[i];
    int j = i - 1;
    while(j >= 0 && ranks[j] < v){
      ranks[j + 1] = ranks[j];
      j--;
    }
    ranks[j + 1] = v;
  }

  int count[15] = {0};
  for(int i = 0; i < 5; i++){
    count[ranks[i]]++;
  }

  // Straight: five distinct ranks spanning four, or the wheel A-5-4-3-2.
  bool distinct = true;
  for(int r = 2; r <= 14; r++){
    if(count[r] > 1){
      distinct = false;
    }
  }
  int straight_high = 0;
  if(distinct){
    if(ranks[0] - ranks[4] == 4){
      straight_high = ranks[0];
    }
    else if(ranks[0] == 14 && ranks[1] == 5 && ranks[4] == 2){
      straight_high = 5;
    }
  }

  // Group ranks by how often they appear, larger groups first, then higher rank.
  int group_rank[5];
  int group_size[5];
  int groups = 0;
  for(int size = 4; size >= 1; size--){
    for(int r = 14; r >= 2; r--){
      if(count[r] == size){
        group_rank[groups] = r;
        group_size[groups] = size;
        groups++;
      }
    }
  }

  if(straight_high && flush){
    return pack(STRAIGHT_FLUSH, &straight_high, 1);
  }
  if(group_size[0] == 4){
    return pack(FOUR_OF_A_KIND, group_rank, 2);
  }
  if(group_size[0] == 3 && group_size[1] == 2){
    return pack(FULL_HOUSE, group_rank, 2);
  }
  if(flush){
    return pack(FLUSH, ranks, 5);
  }
  if(straight_high){
    return pack(STRAIGHT, &straight_high, 1);
  }
  if(group_size[0] == 3){
    return pack(THREE_OF_A_KIND, group_rank, 3);
  }
  if(group_size[0] == 2 && group_size[1] == 2){
    return pack(TWO_PAIR, group_rank, 3);
  }
  if(group_size[0] == 2){
    return pack(ONE_PAIR, group_rank, 4);
  }
  return pack(HIGH_CARD, ranks, 5);
}

int evaluate_best(const Card* const cards[], int count){
  if(count < 5){
    return 0;
  }
  int best = 0;
  const Card* hand[5];
  // Try every 5-card combination (21 of them for 7 cards).
  for(int a = 0; a < count; a++)
  for(int b = a + 1; b < count; b++)
  for(int c = b + 1; c < count; c++)
  for(int d = c + 1; d < count; d++)
  for(int e = d + 1; e < count; e++){
    hand[0] = cards[a];
    hand[1] = cards[b];
    hand[2] = cards[c];
    hand[3] = cards[d];
    hand[4] = cards[e];
    int score = evaluate_five(hand);
    if(score > best){
      best = score;
    }
  }
  return best;
}

int hand_category_of(int score){
  return score >> 20;
}

const char* hand_category_name(int category){
  switch(category){
    case HIGH_CARD: return "High card";
    case ONE_PAIR: return "One pair";
    case TWO_PAIR: return "Two pair";
    case THREE_OF_A_KIND: return "Three of a kind";
    case STRAIGHT: return "Straight";
    case FLUSH: return "Flush";
    case FULL_HOUSE: return "Full house";
    case FOUR_OF_A_KIND: return "Four of a kind";
    case STRAIGHT_FLUSH: return "Straight flush";
    default: return "Unknown";
  }
}
