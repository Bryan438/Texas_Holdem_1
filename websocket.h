#ifndef _WEBSOCKET_H_
#define _WEBSOCKET_H_

#include <map>
#include <string>
#include <vector>

// What the server needs to talk to a browser: read an HTTP request, answer it,
// and carry messages over a WebSocket (RFC 6455). Pure functions and a small
// decoder, no sockets, so all of it can be unit tested.
//
// A browser cannot open a plain TCP connection, so the web client reaches the
// table through a WebSocket instead. Each WebSocket text message is one JSON
// message, the same ones the terminal client sends as lines.

// ---------------------------------------------------------------- HTTP

struct http_request{
  std::string method;                          // "GET"
  std::string target;                          // "/app.js?v=2"
  std::map<std::string, std::string> headers;  // names in lower case
  std::string header(const std::string& name) const;   // "" when absent
};

// `head` is everything up to and including the blank line that ends the headers.
bool parse_http_request(const std::string& head, http_request& out);

// A complete response; the connection is closed after it.
std::string http_response(int status, const std::string& content_type, const std::string& body);

// The file a request asks for, relative to the web folder: "/" -> "index.html",
// "/app.js?v=2" -> "app.js". False for anything that could leave the folder
// ("..", hidden files, odd characters).
bool web_file_path(const std::string& target, std::string& relative);
std::string content_type_for(const std::string& path);

// ---------------------------------------------------------------- WebSocket

// Does the request ask to switch to a WebSocket (version 13)?
bool is_websocket_upgrade(const http_request& request);

// A page may only connect to the server it was loaded from: without this check
// any website open in the player's browser could sit down at their table.
// Programs that send no Origin header (not browsers) are let through.
bool same_origin(const http_request& request);

// The "101 Switching Protocols" answer that completes the handshake.
std::string websocket_accept_response(const http_request& request);
std::string websocket_accept_key(const std::string& client_key);

enum ws_opcode{
  WS_CONTINUATION = 0,
  WS_TEXT = 1,
  WS_BINARY = 2,
  WS_CLOSE = 8,
  WS_PING = 9,
  WS_PONG = 10,
};

struct ws_message{
  int opcode;
  std::string payload;
};

// One frame from server to browser (never masked).
std::string websocket_frame(const std::string& payload, int opcode = WS_TEXT);

// Turns the bytes a browser sends into messages. Like TCP lines, a frame may
// arrive in pieces or several at once; a message may also be split over frames.
class ws_decoder{
  public:
    explicit ws_decoder(size_t max_message_bytes = 64 * 1024) : max_message(max_message_bytes) {}

    // Add received bytes and move every complete message into `out`.
    // False when the peer breaks the protocol or sends too much: disconnect it.
    bool feed(const char* data, size_t len, std::vector<ws_message>& out);

  private:
    std::string pending;      // bytes of a frame that is not complete yet
    std::string partial;      // a message whose last frame has not arrived
    int partial_opcode = -1;
    size_t max_message;
};

// Used by the handshake; exposed for tests.
std::string sha1(const std::string& data);            // 20 raw bytes
std::string base64_encode(const std::string& data);

#endif
