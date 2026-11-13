#include <algorithm>
#include "pot_calculator.h"

// Give `pot` to the best-scoring seats among `eligible`, splitting ties.
// Odd chips go one at a time clockwise starting from first_seat.
static void award(int pot, const std::vector<int>& eligible,
                  const std::vector<int>& scores, int first_seat,
                  std::vector<int>& payout){
  int n = payout.size();
  int best = scores[eligible[0]];
  for(int seat : eligible){
    best = std::max(best, scores[seat]);
  }

  std::vector<int> winners;
  for(int k = 0; k < n; k++){
    int seat = (first_seat + k) % n;
    if(std::find(eligible.begin(), eligible.end(), seat) != eligible.end() && scores[seat] == best){
      winners.push_back(seat);
    }
  }

  int share = pot / winners.size();
  int odd = pot % winners.size();
  for(size_t i = 0; i < winners.size(); i++){
    payout[winners[i]] += share + (static_cast<int>(i) < odd ? 1 : 0);
  }
}

std::vector<int> distribute_pot(const std::vector<int>& contributions,
                                const std::vector<bool>& in_hand,
                                const std::vector<int>& scores,
                                int first_seat){
  int n = contributions.size();
  std::vector<int> payout(n, 0);
  if(n == 0){
    return payout;
  }
  first_seat = ((first_seat % n) + n) % n;

  // Each distinct contribution amount closes one pot layer.
  std::vector<int> levels;
  for(int c : contributions){
    if(c > 0){
      levels.push_back(c);
    }
  }
  std::sort(levels.begin(), levels.end());
  levels.erase(std::unique(levels.begin(), levels.end()), levels.end());

  int previous = 0;
  int carry = 0;
  for(int level : levels){
    int pot = carry;
    std::vector<int> eligible;
    for(int i = 0; i < n; i++){
      pot += std::min(contributions[i], level) - std::min(contributions[i], previous);
      if(in_hand[i] && contributions[i] >= level){
        eligible.push_back(i);
      }
    }
    previous = level;

    if(eligible.empty()){
      // Only folded seats reached this layer; roll it into the next one.
      carry = pot;
      continue;
    }
    carry = 0;
    award(pot, eligible, scores, first_seat, payout);
  }

  if(carry > 0){
    // Top layer belonged to folded seats only: give it to the best live hand.
    std::vector<int> eligible;
    for(int i = 0; i < n; i++){
      if(in_hand[i]){
        eligible.push_back(i);
      }
    }
    if(!eligible.empty()){
      award(carry, eligible, scores, first_seat, payout);
    }
  }
  return payout;
}
