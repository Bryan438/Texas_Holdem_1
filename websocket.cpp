#include <algorithm>
#include <cctype>
#include <cstdint>
#include "websocket.h"

static std::string lower(std::string s){
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
  return s;
}

static std::string trim(const std::string& s){
  size_t from = s.find_first_not_of(" \t");
  if(from == std::string::npos){
    return "";
  }
  return s.substr(from, s.find_last_not_of(" \t") - from + 1);
}

// ---------------------------------------------------------------- HTTP

std::string http_request::header(const std::string& name) const{
  auto it = headers.find(name);
  return it == headers.end() ? "" : it->second;
}

bool parse_http_request(const std::string& head, http_request& out){
  size_t line_end = head.find("\r\n");
  if(line_end == std::string::npos){
    return false;
  }
  // "GET /path HTTP/1.1"
  std::string first = head.substr(0, line_end);
  size_t a = first.find(' ');
  size_t b = first.rfind(' ');
  if(a == std::string::npos || b == a || first.compare(b + 1, 5, "HTTP/") != 0){
    return false;
  }
  out.method = first.substr(0, a);
  out.target = first.substr(a + 1, b - a - 1);
  out.headers.clear();
  if(out.method.empty() || out.target.empty() || out.target[0] != '/'){
    return false;
  }

  size_t at = line_end + 2;
  while(at < head.size()){
    size_t end = head.find("\r\n", at);
    if(end == std::string::npos || end == at){
      break;   // the blank line: headers are over
    }
    std::string line = head.substr(at, end - at);
    size_t colon = line.find(':');
    if(colon == std::string::npos || colon == 0){
      return false;
    }
    out.headers[lower(line.substr(0, colon))] = trim(line.substr(colon + 1));
    at = end + 2;
  }
  return true;
}

static const char* status_text(int status){
  switch(status){
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}

std::string http_response(int status, const std::string& content_type, const std::string& body){
  return "HTTP/1.1 " + std::to_string(status) + " " + status_text(status) + "\r\n"
         "Content-Type: " + content_type + "\r\n"
         "Content-Length: " + std::to_string(body.size()) + "\r\n"
         "Cache-Control: no-cache\r\n"
         "X-Content-Type-Options: nosniff\r\n"
         "Connection: close\r\n\r\n" + body;
}

bool web_file_path(const std::string& target, std::string& relative){
  std::string path = target.substr(0, target.find_first_of("?#"));
  if(path.empty() || path[0] != '/'){
    return false;
  }
  if(path == "/"){
    relative = "index.html";
    return true;
  }
  path.erase(0, 1);
  // Every part of the path is a plain name: no "..", no hidden files, no "//".
  bool start_of_part = true;
  for(char c : path){
    if(c == '/'){
      if(start_of_part){
        return false;
      }
      start_of_part = true;
      continue;
    }
    bool plain = std::isalnum((unsigned char)c) || c == '_' || c == '-' || (c == '.' && !start_of_part);
    if(!plain){
      return false;
    }
    start_of_part = false;
  }
  if(start_of_part){
    return false;   // ends with "/": folders are not listed
  }
  relative = path;
  return true;
}

std::string content_type_for(const std::string& path){
  size_t dot = path.rfind('.');
  std::string ext = dot == std::string::npos ? "" : lower(path.substr(dot + 1));
  if(ext == "html") return "text/html; charset=utf-8";
  if(ext == "js") return "text/javascript; charset=utf-8";
  if(ext == "css") return "text/css; charset=utf-8";
  if(ext == "json") return "application/json";
  if(ext == "svg") return "image/svg+xml";
  if(ext == "png") return "image/png";
  if(ext == "ico") return "image/x-icon";
  if(ext == "txt") return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

// ---------------------------------------------------------------- handshake

bool is_websocket_upgrade(const http_request& request){
  return request.method == "GET" &&
         lower(request.header("upgrade")) == "websocket" &&
         lower(request.header("connection")).find("upgrade") != std::string::npos &&
         !request.header("sec-websocket-key").empty() &&
         request.header("sec-websocket-version") == "13";
}

bool same_origin(const http_request& request){
  std::string origin = lower(request.header("origin"));
  if(origin.empty()){
    return true;
  }
  size_t scheme = origin.find("://");
  if(scheme == std::string::npos){
    return false;   // "null" (a local file or a sandboxed page) and anything odd
  }
  return origin.substr(scheme + 3) == lower(request.header("host"));
}

std::string websocket_accept_key(const std::string& client_key){
  return base64_encode(sha1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
}

std::string websocket_accept_response(const http_request& request){
  return "HTTP/1.1 101 Switching Protocols\r\n"
         "Upgrade: websocket\r\n"
         "Connection: Upgrade\r\n"
         "Sec-WebSocket-Accept: " + websocket_accept_key(request.header("sec-websocket-key")) + "\r\n\r\n";
}

// ---------------------------------------------------------------- frames

std::string websocket_frame(const std::string& payload, int opcode){
  std::string frame;
  frame.push_back((char)(0x80 | (opcode & 0x0f)));   // one frame, complete
  size_t n = payload.size();
  if(n < 126){
    frame.push_back((char)n);
  }
  else if(n <= 0xffff){
    frame.push_back(126);
    frame.push_back((char)(n >> 8));
    frame.push_back((char)(n & 0xff));
  }
  else{
    frame.push_back(127);
    for(int shift = 56; shift >= 0; shift -= 8){
      frame.push_back((char)(((uint64_t)n >> shift) & 0xff));
    }
  }
  return frame + payload;
}

bool ws_decoder::feed(const char* data, size_t len, std::vector<ws_message>& out){
  pending.append(data, len);
  size_t at = 0;
  while(true){
    size_t left = pending.size() - at;
    if(left < 2){
      break;
    }
    const unsigned char* p = (const unsigned char*)pending.data() + at;
    bool final_frame = p[0] & 0x80;
    int opcode = p[0] & 0x0f;
    bool masked = p[1] & 0x80;
    uint64_t size = p[1] & 0x7f;
    size_t header = 2;
    // No extensions were agreed, and a browser must mask everything it sends.
    if((p[0] & 0x70) != 0 || !masked){
      return false;
    }
    if(size == 126){
      if(left < 4) break;
      size = ((uint64_t)p[2] << 8) | p[3];
      header = 4;
    }
    else if(size == 127){
      if(left < 10) break;
      size = 0;
      for(int i = 0; i < 8; i++){
        size = (size << 8) | p[2 + i];
      }
      header = 10;
    }
    if(size > max_message){
      return false;
    }
    if(left < header + 4 + size){
      break;
    }
    const unsigned char* mask = p + header;
    std::string payload((size_t)size, '\0');
    for(size_t i = 0; i < (size_t)size; i++){
      payload[i] = (char)(mask[4 + i] ^ mask[i % 4]);
    }
    at += header + 4 + (size_t)size;

    if(opcode >= WS_CLOSE){
      // Close, ping, pong: short, never split, may come between data frames.
      if(!final_frame || size > 125){
        return false;
      }
      out.push_back({opcode, payload});
      continue;
    }
    if(opcode == WS_CONTINUATION){
      if(partial_opcode < 0){
        return false;
      }
    }
    else if(opcode == WS_TEXT || opcode == WS_BINARY){
      if(partial_opcode >= 0){
        return false;   // a new message before the last one ended
      }
      partial_opcode = opcode;
    }
    else{
      return false;
    }
    if(partial.size() + payload.size() > max_message){
      return false;
    }
    partial += payload;
    if(final_frame){
      out.push_back({partial_opcode, partial});
      partial.clear();
      partial_opcode = -1;
    }
  }
  pending.erase(0, at);
  return true;
}

// ---------------------------------------------------------------- SHA-1 and base64

static uint32_t rotate_left(uint32_t x, int n){
  return (x << n) | (x >> (32 - n));
}

std::string sha1(const std::string& data){
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

  // Pad: a 1 bit, zeros, then the length in bits as 8 bytes.
  std::string m = data;
  uint64_t bits = (uint64_t)data.size() * 8;
  m.push_back((char)0x80);
  while(m.size() % 64 != 56){
    m.push_back('\0');
  }
  for(int shift = 56; shift >= 0; shift -= 8){
    m.push_back((char)((bits >> shift) & 0xff));
  }

  for(size_t block = 0; block < m.size(); block += 64){
    uint32_t w[80];
    for(int i = 0; i < 16; i++){
      const unsigned char* b = (const unsigned char*)m.data() + block + i * 4;
      w[i] = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    }
    for(int i = 16; i < 80; i++){
      w[i] = rotate_left(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for(int i = 0; i < 80; i++){
      uint32_t f, k;
      if(i < 20){ f = (b & c) | (~b & d); k = 0x5A827999; }
      else if(i < 40){ f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if(i < 60){ f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else{ f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t next = rotate_left(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rotate_left(b, 30);
      b = a;
      a = next;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }

  std::string digest;
  for(uint32_t word : h){
    for(int shift = 24; shift >= 0; shift -= 8){
      digest.push_back((char)((word >> shift) & 0xff));
    }
  }
  return digest;
}

std::string base64_encode(const std::string& data){
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for(size_t i = 0; i < data.size(); i += 3){
    uint32_t chunk = (unsigned char)data[i] << 16;
    if(i + 1 < data.size()) chunk |= (unsigned char)data[i + 1] << 8;
    if(i + 2 < data.size()) chunk |= (unsigned char)data[i + 2];
    out.push_back(table[(chunk >> 18) & 63]);
    out.push_back(table[(chunk >> 12) & 63]);
    out.push_back(i + 1 < data.size() ? table[(chunk >> 6) & 63] : '=');
    out.push_back(i + 2 < data.size() ? table[chunk & 63] : '=');
  }
  return out;
}
