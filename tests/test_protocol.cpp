#include <string.h>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "test_framework.h"
#include "transport.h"
#include "message_content.h"

namespace {

struct received{
  int command;
  std::string text;
  int number;
};

// Records every message the transport hands to its listener.
class collector: public observer{
  public:
    std::vector<received> messages;
    void handle_message(message_content* msg, int){
      received r;
      r.command = msg->get_command();
      r.text = msg->get_charmessage() ? std::string(msg->get_charmessage(), msg->get_length()) : "";
      r.number = msg->get_intmessage();
      messages.push_back(r);
    }
};

std::string frame(int command, const std::string& payload){
  uint32_t header = htonl(((uint32_t)command << 24) | ((uint32_t)payload.size() << 16));
  std::string out((const char*)&header, 4);
  return out + payload;
}

std::string int_payload(int value){
  uint32_t n = htonl(value);
  return std::string((const char*)&n, 4);
}

}

TEST(message_content_copies_and_terminates_text){
  message_content m;
  char text[5] = {'h', 'e', 'l', 'l', 'o'}; // not NUL terminated on purpose
  m.set_charmessage(text, 5);
  CHECK_EQ(m.get_length(), 5);
  CHECK(strcmp(m.get_charmessage(), "hello") == 0);
  m.set_charmessage(text, 2); // replacing must free the old buffer
  CHECK(strcmp(m.get_charmessage(), "he") == 0);
}

TEST(serialize_then_deserialize_round_trip){
  int sv[2];
  CHECK_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  transport* tp = transport::get_instance();

  tp->serialize(sv[0], 28, 11, "hello world");
  std::string p = int_payload(1234);
  tp->serialize(sv[0], 5, 4, p.data());

  char buf[64];
  int n = recv(sv[1], buf, 4 + 11 + 4 + 4, MSG_WAITALL);
  CHECK_EQ(n, 4 + 11 + 4 + 4);

  int command = 0, length = 0;
  transport::decode_header(buf, &command, &length);
  CHECK_EQ(command, 28);
  CHECK_EQ(length, 11);
  message_content* text = tp->deserialize(buf);
  CHECK(strcmp(text->get_charmessage(), "hello world") == 0);
  delete text;

  message_content* number = tp->deserialize(buf + 15);
  CHECK_EQ(number->get_command(), 5);
  CHECK_EQ(number->get_intmessage(), 1234);
  delete number;

  close(sv[0]);
  close(sv[1]);
}

TEST(serialize_refuses_oversized_payload){
  int sv[2];
  CHECK_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  std::string big(300, 'x');
  transport::get_instance()->serialize(sv[0], 2, big.size(), big.data());
  close(sv[0]);
  char buf[8];
  CHECK_EQ(recv(sv[1], buf, sizeof(buf), 0), 0); // nothing was sent
  close(sv[1]);
}

// TCP may deliver several frames in one recv() or one frame over several.
// The old loop spun forever on a partial frame and mis-counted on errors.
TEST(read_reassembles_split_and_merged_frames){
  int sv[2];
  CHECK_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  transport* tp = transport::get_instance();
  collector c;
  tp->set_listener(&c);

  std::thread reader(&transport::read, tp, sv[0]);

  // Two frames in one write.
  std::string two = frame(4, "Bryan") + frame(7, "");
  CHECK_EQ(write(sv[1], two.data(), two.size()), (ssize_t)two.size());

  // One frame dribbled out a few bytes at a time.
  std::string split = frame(5, int_payload(200));
  for(size_t i = 0; i < split.size(); i += 3){
    size_t len = std::min<size_t>(3, split.size() - i);
    CHECK_EQ(write(sv[1], split.data() + i, len), (ssize_t)len);
    usleep(5000);
  }

  close(sv[1]); // reader sees EOF and returns
  reader.join();
  tp->set_listener(NULL);

  CHECK_EQ(c.messages.size(), 3);
  if(c.messages.size() == 3){
    CHECK_EQ(c.messages[0].command, 4);
    CHECK(c.messages[0].text == "Bryan");
    CHECK_EQ(c.messages[1].command, 7);
    CHECK_EQ(c.messages[2].command, 5);
    CHECK_EQ(c.messages[2].number, 200);
  }
}

TEST(hex_dump_is_terminated){
  char* hex = transport::get_instance()->convert_buffer_to_base64("\x01\xAB", 2);
  CHECK(strcmp(hex, "01AB") == 0);
  delete[] hex;
}
