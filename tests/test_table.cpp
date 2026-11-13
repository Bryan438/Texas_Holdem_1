// Drives whole hands through table::handle_message, the same entry point the
// network uses. Each player is one end of a socketpair so the table can send
// to it; the test reads those frames back where it needs to.
#include <fcntl.h>
#include <string.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "test_framework.h"
#include "table.h"
#include "transport.h"

namespace {

struct frame{
  int command;
  std::string payload;
};

class fake_table{
  public:
    table t;
    std::vector<int> server_fd;
    std::vector<int> client_fd;
    std::vector<std::string> last_result; // latest command-28 text per seat

    explicit fake_table(const std::vector<int>& stacks){
      for(size_t i = 0; i < stacks.size(); i++){
        join();
        std::string name = "P" + std::to_string(i);
        send_text(i, 4, name);
        send_int(i, 5, stacks[i]);
      }
    }

    ~fake_table(){
      for(int fd : server_fd) close(fd);
      for(int fd : client_fd) close(fd);
    }

    int join(){
      int sv[2];
      socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
      fcntl(sv[1], F_SETFL, O_NONBLOCK);
      server_fd.push_back(sv[0]);
      client_fd.push_back(sv[1]);
      last_result.push_back("");
      message_content m;
      m.set_command(3);
      t.handle_message(&m, sv[0]);
      return server_fd.size() - 1;
    }

    void send_text(int seat, int command, const std::string& text){
      message_content m;
      m.set_command(command);
      m.set_charmessage((char*)text.data(), text.size());
      t.handle_message(&m, server_fd[seat]);
    }

    void send_int(int seat, int command, int value){
      message_content m;
      m.set_command(command);
      m.set_intmessage(value);
      t.handle_message(&m, server_fd[seat]);
    }

    void start(){ send_int(0, 7, 0); }

    // Action payload as the client sends it: [bitmask][letter][raise amount].
    void act(int seat, char letter, int raise_to = 0){
      char payload[6] = {0, letter, 0, 0, 0, 0};
      uint32_t n = htonl(raise_to);
      memcpy(payload + 2, &n, 4);
      send_text(seat, 10, std::string(payload, 6));
    }

    player* p(int seat){ return t.get_player(seat); }

    int total_chips(){
      int sum = 0;
      for(int i = 0; i < t.get_player_count(); i++){
        sum += p(i)->get_money();
      }
      return sum + t.get_total_pot();
    }

    // Keep the hand going with check/call for everyone, answering every
    // card and bet notification, until the hand is over.
    void play_out(){
      for(int guard = 0; guard < 500 && t.get_state() != HAND_OVER; guard++){
        if(t.get_state() == WAITING_NOTIFY){
          send_int(t.get_inform_pos(), 13, 0);
        }
        else if(t.is_dealing()){
          send_int(t.get_current_pos(), 1, 0);
        }
        else{
          int seat = t.get_current_pos();
          int to_call = t.get_current_bet() - p(seat)->get_current_round_bet();
          act(seat, to_call == 0 ? 'h' : 'c');
        }
      }
      // Empty the client sockets so a long test never fills their buffers.
      for(size_t i = 0; i < client_fd.size(); i++){
        drain(i);
      }
    }

    std::vector<frame> drain(int seat){
      std::vector<frame> frames;
      std::string bytes;
      char buf[4096];
      int n;
      while((n = recv(client_fd[seat], buf, sizeof(buf), 0)) > 0){
        bytes.append(buf, n);
      }
      size_t pos = 0;
      while(pos + 4 <= bytes.size()){
        int command = 0, length = 0;
        transport::decode_header(bytes.data() + pos, &command, &length);
        frames.push_back({command, bytes.substr(pos + 4, length)});
        if(command == 28){
          last_result[seat] = frames.back().payload;
        }
        pos += 4 + length;
      }
      return frames;
    }

    std::string result_text(int seat){
      drain(seat);
      return last_result[seat];
    }
};

}

TEST(table_everyone_folds_to_big_blind){
  fake_table ft({50, 50, 50});
  ft.start();
  // Seat 0 deals, seat 1 small blind, seat 2 big blind, seat 0 acts first.
  CHECK_EQ(ft.t.get_dealer_pos(), 0);
  CHECK_EQ(ft.t.get_current_pos(), 0);
  CHECK_EQ(ft.t.get_total_pot(), 3);

  ft.act(0, 'f');
  CHECK_EQ(ft.t.get_current_pos(), 1);
  ft.act(1, 'f');

  // Before the fix nobody was paid when the second-to-last player folded.
  CHECK_EQ(ft.t.get_state(), HAND_OVER);
  CHECK_EQ(ft.p(0)->get_money(), 50);
  CHECK_EQ(ft.p(1)->get_money(), 49);
  CHECK_EQ(ft.p(2)->get_money(), 51);
  CHECK_EQ(ft.total_chips(), 150);

  std::string result = ft.result_text(0);
  CHECK(result.find("P2 wins 3") != std::string::npos);
}

TEST(table_rejects_out_of_turn_and_invalid_actions){
  fake_table ft({50, 50, 50});
  ft.start();

  ft.act(1, 'f');                       // not seat 1's turn
  CHECK(ft.p(1)->get_player_status());
  CHECK_EQ(ft.t.get_current_pos(), 0);

  ft.act(0, 'h');                       // cannot check facing the big blind
  CHECK(ft.p(0)->get_player_status());
  CHECK_EQ(ft.t.get_current_pos(), 0);
  CHECK_EQ(ft.p(0)->get_money(), 50);

  ft.act(0, 'r', 2);                    // a raise must go above the current bet
  CHECK_EQ(ft.p(0)->get_money(), 50);
  CHECK_EQ(ft.t.get_state(), IN_ROUND);

  ft.act(0, 'r', 51);                   // more than the stack
  CHECK_EQ(ft.p(0)->get_money(), 50);

  ft.act(0, 'c');                       // a valid call goes through
  CHECK_EQ(ft.p(0)->get_money(), 48);
  CHECK_EQ(ft.t.get_current_pos(), 1);
}

TEST(table_heads_up_check_down_reaches_showdown){
  fake_table ft({50, 50});
  ft.start();
  // Heads-up: the dealer is the small blind and acts first before the flop.
  CHECK_EQ(ft.t.get_dealer_pos(), 0);
  CHECK_EQ(ft.p(0)->get_money(), 49);
  CHECK_EQ(ft.p(1)->get_money(), 48);
  CHECK_EQ(ft.t.get_current_pos(), 0);

  ft.play_out();

  CHECK_EQ(ft.t.get_state(), HAND_OVER);
  CHECK_EQ(ft.total_chips(), 100);
  int a = ft.p(0)->get_money();
  CHECK(a == 48 || a == 50 || a == 52);
  CHECK(ft.result_text(1).find("Hand over.") == 0);
}

TEST(table_button_moves_and_hands_repeat){
  fake_table ft({50, 50, 50});
  for(int hand = 0; hand < 6; hand++){
    ft.start();
    CHECK_EQ(ft.t.get_state(), IN_ROUND);
    CHECK_EQ(ft.t.get_dealer_pos(), hand % 3);
    ft.play_out();
    CHECK_EQ(ft.t.get_state(), HAND_OVER);
    CHECK_EQ(ft.total_chips(), 150);
  }
}

TEST(table_all_in_side_pot_is_paid_correctly){
  // Seat 1 is short. It can win at most the main pot: 20 from each of 3 players.
  fake_table ft({50, 20, 100});
  ft.start();
  CHECK_EQ(ft.t.get_current_pos(), 0);

  ft.act(0, 'r', 50);                   // seat 0 shoves
  CHECK_EQ(ft.t.get_state(), WAITING_NOTIFY);
  CHECK(ft.p(0)->get_allin_status());
  ft.play_out();

  CHECK_EQ(ft.t.get_state(), HAND_OVER);
  CHECK_EQ(ft.total_chips(), 170);
  CHECK(ft.p(1)->get_money() <= 60);
  CHECK(ft.p(2)->get_money() >= 50);    // seat 2 only ever risked 50 of its 100
}

TEST(table_player_joining_mid_hand_sits_out){
  fake_table ft({50, 50});
  ft.start();
  int late = ft.join();
  ft.send_text(late, 4, "Late");
  CHECK(!ft.p(late)->get_player_status());
  ft.play_out();
  CHECK_EQ(ft.p(late)->get_money(), 50);
  CHECK_EQ(ft.total_chips(), 150);

  ft.start();                           // next hand deals them in
  CHECK(ft.p(late)->get_player_status());
  ft.play_out();
  CHECK_EQ(ft.total_chips(), 150);
}

TEST(table_busted_player_sits_out){
  fake_table ft({50, 50, 50});
  ft.start();
  // Seat 0 shoves, everyone calls; somebody may bust.
  ft.act(0, 'r', 50);
  ft.play_out();
  CHECK_EQ(ft.total_chips(), 150);

  int with_chips = 0;
  for(int i = 0; i < 3; i++){
    if(ft.p(i)->get_money() > 0) with_chips++;
  }
  ft.start();
  if(with_chips >= 2){
    CHECK_EQ(ft.t.get_state(), IN_ROUND);
    for(int i = 0; i < 3; i++){
      if(ft.p(i)->get_money() == 0 && ft.p(i)->get_total_bet() == 0){
        CHECK(!ft.p(i)->get_player_status());
      }
    }
    ft.play_out();
  }
  else{
    CHECK_EQ(ft.t.get_state(), HAND_OVER); // one player has everything
  }
  CHECK_EQ(ft.total_chips(), 150);
}
