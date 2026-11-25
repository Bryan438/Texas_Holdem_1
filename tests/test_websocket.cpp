// Browsers: the HTTP and WebSocket helpers, and a browser connection through
// the transport from the handshake to the goodbye.
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include "test_framework.h"
#include "table_harness.h"
#include "transport.h"
#include "websocket.h"

namespace {

std::string hex(const std::string& bytes){
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for(unsigned char c : bytes){
    out.push_back(digits[c >> 4]);
    out.push_back(digits[c & 15]);
  }
  return out;
}

// A frame as a browser sends it: masked.
std::string browser_frame(const std::string& payload, int opcode = WS_TEXT, bool final_frame = true){
  const unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};
  std::string frame;
  frame.push_back((char)((final_frame ? 0x80 : 0) | opcode));
  if(payload.size() < 126){
    frame.push_back((char)(0x80 | payload.size()));
  }
  else{
    frame.push_back((char)(0x80 | 126));
    frame.push_back((char)(payload.size() >> 8));
    frame.push_back((char)(payload.size() & 0xff));
  }
  frame.append((const char*)mask, 4);
  for(size_t i = 0; i < payload.size(); i++){
    frame.push_back((char)(payload[i] ^ mask[i % 4]));
  }
  return frame;
}

const char* HANDSHAKE =
  "GET /ws HTTP/1.1\r\n"
  "Host: localhost:8080\r\n"
  "Upgrade: websocket\r\n"
  "Connection: keep-alive, Upgrade\r\n"
  "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
  "Sec-WebSocket-Version: 13\r\n"
  "Origin: http://localhost:8080\r\n\r\n";

class ws_collector: public observer{
  public:
    std::vector<std::string> lines;
    int connects = 0, disconnects = 0;
    void on_connect(int) override { connects++; }
    void on_message(int, const std::string& line) override { lines.push_back(line); }
    void on_disconnect(int) override { disconnects++; }
};

// Read from `fd` until `text` has shown up or the other side closed.
std::string read_until(int fd, const std::string& text){
  std::string got;
  char buf[4096];
  while(got.find(text) == std::string::npos){
    int n = recv(fd, buf, sizeof(buf), 0);
    if(n <= 0) break;
    got.append(buf, n);
  }
  return got;
}

std::string read_all(int fd){ return read_until(fd, std::string(1, '\0') + "never"); }

}

TEST(sha1_and_base64_match_known_answers){
  CHECK(hex(sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  CHECK(hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
  // Longer than one 64-byte block once padded.
  CHECK(hex(sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  CHECK(base64_encode("") == "");
  CHECK(base64_encode("f") == "Zg==");
  CHECK(base64_encode("fo") == "Zm8=");
  CHECK(base64_encode("foo") == "Zm9v");
  CHECK(base64_encode("foobar") == "Zm9vYmFy");
  // The example in RFC 6455.
  CHECK(websocket_accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(http_request_is_parsed){
  http_request r;
  CHECK(parse_http_request(HANDSHAKE, r));
  CHECK(r.method == "GET" && r.target == "/ws");
  CHECK(r.header("host") == "localhost:8080");
  CHECK(r.header("sec-websocket-key") == "dGhlIHNhbXBsZSBub25jZQ==");
  CHECK(r.header("cookie") == "");
  CHECK(is_websocket_upgrade(r));
  CHECK(same_origin(r));

  CHECK(parse_http_request("GET /app.js?v=2 HTTP/1.1\r\nHost: x\r\n\r\n", r));
  CHECK(r.target == "/app.js?v=2");
  CHECK(!is_websocket_upgrade(r));
  CHECK(!parse_http_request("hello\r\n\r\n", r));
  CHECK(!parse_http_request("GET nothing HTTP/1.1\r\n\r\n", r));
  CHECK(!parse_http_request("GET / HTTP/1.1\r\nno colon here\r\n\r\n", r));
}

TEST(websocket_only_from_the_servers_own_page){
  http_request r;
  r.method = "GET";
  r.headers = {{"host", "192.168.1.20:8080"}};
  CHECK(same_origin(r));                               // not a browser: no Origin
  r.headers["origin"] = "http://192.168.1.20:8080";
  CHECK(same_origin(r));
  r.headers["origin"] = "HTTP://192.168.1.20:8080";
  CHECK(same_origin(r));
  r.headers["origin"] = "http://evil.example";
  CHECK(!same_origin(r));
  r.headers["origin"] = "http://192.168.1.20:9999";
  CHECK(!same_origin(r));
  r.headers["origin"] = "null";                        // a file opened from disk
  CHECK(!same_origin(r));
}

TEST(web_paths_stay_inside_the_web_folder){
  std::string path;
  CHECK(web_file_path("/", path) && path == "index.html");
  CHECK(web_file_path("/app.js", path) && path == "app.js");
  CHECK(web_file_path("/style.css?v=3", path) && path == "style.css");
  CHECK(web_file_path("/img/chip-1.svg", path) && path == "img/chip-1.svg");
  CHECK(!web_file_path("/../hands.jsonl", path));
  CHECK(!web_file_path("/img/../../hands.jsonl", path));
  CHECK(!web_file_path("/.git/config", path));
  CHECK(!web_file_path("//etc/passwd", path));
  CHECK(!web_file_path("/img/", path));
  CHECK(!web_file_path("/a%2e%2e/b", path));
  CHECK(!web_file_path("/a\\b", path));
  CHECK(!web_file_path("app.js", path));
  CHECK(content_type_for("index.html") == "text/html; charset=utf-8");
  CHECK(content_type_for("app.JS") == "text/javascript; charset=utf-8");
  CHECK(content_type_for("mystery") == "application/octet-stream");
}

TEST(websocket_frames_carry_any_length){
  CHECK(websocket_frame("hi") == std::string("\x81\x02hi", 4));
  std::string medium(300, 'm'), large(70000, 'L');
  std::string f = websocket_frame(medium);
  CHECK_EQ(f.size(), 4 + 300);
  CHECK((unsigned char)f[1] == 126 && (unsigned char)f[2] == 1 && (unsigned char)f[3] == 44);
  f = websocket_frame(large);
  CHECK_EQ(f.size(), 10 + 70000);
  CHECK((unsigned char)f[1] == 127 && (unsigned char)f[7] == 1 && (unsigned char)f[8] == 0x11 &&
        (unsigned char)f[9] == 0x70);
  CHECK(websocket_frame("", WS_CLOSE) == std::string("\x88\x00", 2));
}

TEST(ws_decoder_reads_whole_split_and_merged_frames){
  ws_decoder d;
  std::vector<ws_message> out;
  std::string join = "{\"type\":\"join\",\"name\":\"Bryan\"}";
  std::string two = browser_frame(join) + browser_frame(std::string(200, 'x'));
  // One byte at a time: nothing may come out early or twice.
  for(char c : two){
    CHECK(d.feed(&c, 1, out));
  }
  CHECK_EQ(out.size(), 2);
  if(out.size() == 2){
    CHECK(out[0].opcode == WS_TEXT && out[0].payload == join);
    CHECK(out[1].payload == std::string(200, 'x'));
  }
  // A message split over three frames, with a ping in the middle.
  out.clear();
  std::string parts = browser_frame("{\"type\":", WS_TEXT, false) + browser_frame("\"sta", WS_CONTINUATION, false) +
                      browser_frame("p", WS_PING) + browser_frame("rt\"}", WS_CONTINUATION, true);
  CHECK(d.feed(parts.data(), parts.size(), out));
  CHECK_EQ(out.size(), 2);
  if(out.size() == 2){
    CHECK(out[0].opcode == WS_PING && out[0].payload == "p");
    CHECK(out[1].opcode == WS_TEXT && out[1].payload == "{\"type\":\"start\"}");
  }
}

TEST(ws_decoder_refuses_what_a_browser_never_sends){
  std::vector<ws_message> out;
  std::string unmasked = websocket_frame("hi");
  CHECK(!ws_decoder().feed(unmasked.data(), unmasked.size(), out));
  std::string stray = browser_frame("x", WS_CONTINUATION);            // nothing to continue
  CHECK(!ws_decoder().feed(stray.data(), stray.size(), out));
  std::string twice = browser_frame("a", WS_TEXT, false) + browser_frame("b", WS_TEXT);
  CHECK(!ws_decoder().feed(twice.data(), twice.size(), out));
  std::string big = browser_frame(std::string(500, 'x'));
  CHECK(!ws_decoder(100).feed(big.data(), big.size(), out));
  // Many small frames that add up to too much.
  ws_decoder d(100);
  std::string piece = browser_frame(std::string(60, 'x'), WS_TEXT, false);
  CHECK(d.feed(piece.data(), piece.size(), out));
  piece = browser_frame(std::string(60, 'x'), WS_CONTINUATION, false);
  CHECK(!d.feed(piece.data(), piece.size(), out));
  CHECK_EQ(out.size(), 0);
}

TEST(transport_talks_to_a_browser_over_a_websocket){
  int sv[2];
  CHECK_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  transport* tp = transport::get_instance();
  ws_collector c;
  tp->set_listener(&c);
  std::thread server(&transport::serve_http, tp, sv[0]);

  // The handshake and the first message arrive together.
  std::string hello = std::string(HANDSHAKE) + browser_frame("{\"type\":\"join\",\"name\":\"Bryan\"}\n");
  CHECK_EQ(write(sv[1], hello.data(), hello.size()), (ssize_t)hello.size());
  std::string answer = read_until(sv[1], "\r\n\r\n");
  CHECK(answer.find("HTTP/1.1 101") == 0);
  CHECK(answer.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);

  // A ping is answered with the same bytes.
  std::string ping = browser_frame("abc", WS_PING);
  CHECK_EQ(write(sv[1], ping.data(), ping.size()), (ssize_t)ping.size());
  CHECK(read_until(sv[1], "abc") == websocket_frame("abc", WS_PONG));

  // What the game sends goes out as one frame, without a newline.
  CHECK_EQ(c.lines.size(), 1);
  tp->send_line(sv[0], "{\"type\":\"event\"}");
  CHECK(read_until(sv[1], "}") == websocket_frame("{\"type\":\"event\"}"));

  std::string bye = browser_frame("", WS_CLOSE);
  CHECK_EQ(write(sv[1], bye.data(), bye.size()), (ssize_t)bye.size());
  server.join();
  tp->set_listener(NULL);
  CHECK(read_all(sv[1]) == websocket_frame("", WS_CLOSE));
  close(sv[1]);

  CHECK_EQ(c.connects, 1);
  CHECK_EQ(c.disconnects, 1);
  if(c.lines.size() == 1){
    CHECK(c.lines[0] == "{\"type\":\"join\",\"name\":\"Bryan\"}");
  }
}

TEST(transport_serves_web_files_and_nothing_else){
  const char* dir = "tests/web_test.tmp";
  mkdir(dir, 0755);
  std::ofstream(std::string(dir) + "/index.html") << "<h1>table</h1>";
  transport* tp = transport::get_instance();
  ws_collector c;
  tp->set_listener(&c);
  tp->set_web_root(dir);

  auto request = [&](const std::string& text){
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    std::thread server(&transport::serve_http, tp, sv[0]);
    write(sv[1], text.data(), text.size());
    std::string answer = read_all(sv[1]);
    server.join();
    close(sv[1]);
    return answer;
  };
  std::string page = request("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
  CHECK(page.find("HTTP/1.1 200 OK") == 0);
  CHECK(page.find("Content-Type: text/html") != std::string::npos);
  CHECK(page.find("\r\n\r\n<h1>table</h1>") != std::string::npos);
  CHECK(request("GET /missing.js HTTP/1.1\r\nHost: x\r\n\r\n").find("HTTP/1.1 404") == 0);
  CHECK(request("GET /../Makefile HTTP/1.1\r\nHost: x\r\n\r\n").find("HTTP/1.1 404") == 0);
  CHECK(request("POST / HTTP/1.1\r\nHost: x\r\n\r\n").find("HTTP/1.1 405") == 0);
  CHECK(request("nonsense\r\n\r\n").find("HTTP/1.1 400") == 0);
  // A page from another site may not open a WebSocket to the table.
  std::string foreign = HANDSHAKE;
  foreign.replace(foreign.find("Origin: http://localhost:8080"), 29, "Origin: http://evil.example");
  CHECK(request(foreign).find("HTTP/1.1 403") == 0);

  // None of that reached the game.
  CHECK_EQ(c.connects, 0);
  CHECK_EQ(c.disconnects, 0);
  tp->set_listener(NULL);
  tp->set_web_root("");
  remove((std::string(dir) + "/index.html").c_str());
  rmdir(dir);
}

TEST(events_and_errors_say_what_happened_in_fields){
  harness h(2);
  h.start();
  // Heads-up: the dealer posts the small blind and acts first.
  int first = h.t.get_turn();
  json start = json();
  for(const json& m : h.out.got[harness::client(0)]){
    if(m["type"] == "event" && m.value("kind", "") == "hand_start") start = m;
  }
  CHECK(start.value("hand", 0) == 1);
  CHECK(start.value("name", "") == h.t.seat(h.t.get_dealer()).name);
  CHECK(start.value("text", "").find("Hand #1") == 0);        // the terminal client's line is unchanged

  h.act(1 - first, "check");
  json error = h.last(harness::client(1 - first), "error");
  CHECK(error.value("code", "") == "not_your_turn");
  CHECK(error.value("message", "") == "it is not your turn");

  h.act(first, "raise", 6);
  json raise = h.last(harness::client(0), "event");
  CHECK(raise.value("kind", "") == "raise");
  CHECK(raise.value("name", "") == h.t.seat(first).name);
  CHECK_EQ(raise.value("amount", 0), 6);
  CHECK(raise.value("allin", true) == false);

  h.act(1 - first, "call");
  json flop = h.last(harness::client(1), "event");
  CHECK(flop.value("kind", "") == "flop");
  CHECK_EQ(flop["cards"].size(), 3);
  h.play_out();
  json over = h.last(harness::client(1), "event");
  CHECK(over.value("kind", "") == "hand_over");
  bool win = false, show = false;
  for(const json& m : h.out.got[harness::client(1)]){
    win = win || (m["type"] == "event" && m.value("kind", "") == "win" && m.value("amount", 0) > 0);
    show = show || (m["type"] == "event" && m.value("kind", "") == "show" && m["cards"].size() == 2);
  }
  CHECK(win && show);
}

// A second server on a port that is taken must hear about it, so thserver can
// say so and stop instead of hanging with nothing to listen on.
TEST(transport_reports_a_port_that_is_taken){
  int taken = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = 0;   // any free port
  CHECK(bind(taken, (sockaddr*)&addr, sizeof(addr)) == 0);
  CHECK(listen(taken, 1) == 0);
  socklen_t size = sizeof(addr);
  getsockname(taken, (sockaddr*)&addr, &size);
  int port = ntohs(addr.sin_port);

  transport* tp = transport::get_instance();
  CHECK(!tp->start_with_thread(port));
  CHECK(!tp->start_web_with_thread(port, "web"));
  close(taken);
}

// On the internet: one address cannot hold every connection.
TEST(connection_limits_count_in_all_and_per_address){
  connection_limits l;
  l.max_total = 4;
  l.max_per_address = 2;
  CHECK(l.admit(10, "1.2.3.4"));
  CHECK(l.admit(11, "1.2.3.4"));
  CHECK(!l.admit(12, "1.2.3.4"));       // a third from the same address
  CHECK(l.admit(12, "5.6.7.8"));
  CHECK(l.admit(13, ""));               // through the proxy, address not known yet
  CHECK(!l.admit(14, "9.9.9.9"));       // four open: full
  CHECK_EQ(l.open(), 4);

  // The proxied one turns out to be 1.2.3.4, which already has two.
  CHECK(!l.rekey(13, "1.2.3.4"));
  CHECK(l.rekey(13, "5.6.7.8"));
  CHECK(!l.admit(15, "5.6.7.8"));

  l.release(10);
  l.release(10);                        // twice: counted once
  l.release(99);                        // never counted (a test's socketpair)
  CHECK_EQ(l.open(), 3);
  CHECK(l.admit(14, "1.2.3.4"));
  CHECK(!l.admit(15, "9.9.9.9"));
}

TEST(forwarded_client_takes_the_address_our_proxy_added){
  CHECK(forwarded_client("") == "");
  CHECK(forwarded_client("203.0.113.7") == "203.0.113.7");
  // Whatever the browser sent comes first; the proxy appends the real one.
  CHECK(forwarded_client("6.6.6.6, 203.0.113.7") == "203.0.113.7");
  CHECK(forwarded_client(" 2001:db8::1 ") == "2001:db8::1");
}
