#ifndef _POT_CALCULATOR_H_
#define _POT_CALCULATOR_H_

#include <vector>

// Split the chips of one hand into main pot and side pots and pay them out.
//
//   contributions[i]  chips seat i put in over the whole hand
//   in_hand[i]        seat i has not folded
//   scores[i]         hand score of seat i (higher wins); ignored if folded
//   first_seat        seat that receives odd chips first when a pot does not
//                     split evenly (normally the first seat left of the dealer)
//
// Returns how many chips each seat wins. The sum always equals the sum of
// contributions as long as at least one seat is still in the hand.
std::vector<int> distribute_pot(const std::vector<int>& contributions,
                                const std::vector<bool>& in_hand,
                                const std::vector<int>& scores,
                                int first_seat);

#endif
