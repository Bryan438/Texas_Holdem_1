#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include "test_framework.h"
#include "line_buffer.h"
#include "transport.h"

namespace {

// Records everything the transport hands to its listener.
class collector: public observer{
  public:
    std::vector<std::string> lines;
    int disconnects = 0;
    void on_message(int, const std::string& line) override { lines.push_back(line); }
    void on_disconnect(int) override { disconnects++; }
};

}

TEST(line_buffer_splits_merged_lines){
  line_buffer b;
  std::vector<std::string> out;
  std::string data = "{\"a\":1}\n{\"b\":2}\n";
  CHECK(b.feed(data.data(), data.size(), out));
  CHECK_EQ(out.size(), 2);
  CHECK(out[0] == "{\"a\":1}");
  CHECK(out[1] == "{\"b\":2}");
}

TEST(line_buffer_joins_split_line){
  line_buffer b;
  std::vector<std::string> out;
  CHECK(b.feed("{\"ty", 4, out));
  CHECK_EQ(out.size(), 0);
  CHECK(b.feed("pe\":1}", 6, out));
  CHECK_EQ(out.size(), 0);
  CHECK(b.feed("\r\n\n", 3, out));  // CRLF is accepted, empty lines skipped
  CHECK_EQ(out.size(), 1);
  CHECK(out[0] == "{\"type\":1}");
}

TEST(line_buffer_rejects_endless_line){
  line_buffer b(16);
  std::vector<std::string> out;
  std::string junk(40, 'x');
  CHECK(!b.feed(junk.data(), junk.size(), out));
}

TEST(transport_reads_lines_and_reports_disconnect){
  int sv[2];
  CHECK_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  transport* tp = transport::get_instance();
  collector c;
  tp->set_listener(&c);

  std::thread reader(&transport::read, tp, sv[0]);
  std::string two = "{\"type\":\"join\",\"name\":\"Bryan\"}\n{\"type\":";
  CHECK_EQ(write(sv[1], two.data(), two.size()), (ssize_t)two.size());
  usleep(5000);
  std::string rest = "\"start\"}\n";
  CHECK_EQ(write(sv[1], rest.data(), rest.size()), (ssize_t)rest.size());
  close(sv[1]);
  reader.join();
  tp->set_listener(NULL);

  CHECK_EQ(c.lines.size(), 2);
  if(c.lines.size() == 2){
    CHECK(c.lines[0] == "{\"type\":\"join\",\"name\":\"Bryan\"}");
    CHECK(c.lines[1] == "{\"type\":\"start\"}");
  }
  CHECK_EQ(c.disconnects, 1);
}

TEST(transport_send_line_appends_newline){
  int sv[2];
  CHECK_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  transport::get_instance()->send_line(sv[0], "{\"type\":\"event\"}");
  char buf[64];
  int n = recv(sv[1], buf, sizeof(buf), 0);
  CHECK(std::string(buf, n) == "{\"type\":\"event\"}\n");
  close(sv[0]);
  close(sv[1]);
}
