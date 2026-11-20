#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include "transport.h"
#include "line_buffer.h"
#include "websocket.h"

transport* transport::instance = NULL;

transport::transport(){
  sockfd = -1;
}

transport* transport::get_instance(){
  if(instance == NULL){
    instance = new transport();
  }
  return instance;
}

// send() may write only part of the buffer; keep going until it is all out.
static bool send_all(int client_socket, const std::string& data){
  size_t sent = 0;
  while(sent < data.size()){
    ssize_t n = send(client_socket, data.data() + sent, data.size() - sent, 0);
    if(n <= 0){
      return false;
    }
    sent += n;
  }
  return true;
}

static int listen_on(int port){
  // A client that disconnects must not kill the server on the next send().
  signal(SIGPIPE, SIG_IGN);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd == -1){
    printf("Create socket failed\n");
    return -1;
  }
  int reuse = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
    if(errno == EADDRINUSE){
      printf("Port %d is already in use: is another thserver still running?\n", port);
    }
    else{
      printf("Cannot use port %d: %s\n", port, strerror(errno));
    }
    close(fd);
    return -1;
  }
  if(listen(fd, 16) < 0){
    printf("Listen failed\n");
    close(fd);
    return -1;
  }
  return fd;
}

void transport::accept_loop(int listen_fd, bool web){
  while(true){
    sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    int client_socket = accept(listen_fd, (struct sockaddr*)&client_addr, &addrlen);
    if(client_socket < 0){
      continue;
    }
    // A client that stops reading must not freeze the whole table.
    timeval timeout = {2, 0};
    setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if(web){
      // Most of these are file requests: the game hears about a browser only
      // once it has opened a WebSocket.
      std::thread(&transport::serve_http, this, client_socket).detach();
      continue;
    }
    printf("Client %d connected from %s\n", client_socket, inet_ntoa(client_addr.sin_addr));
    run_locked([&]{
      if(p_listener) p_listener->on_connect(client_socket);
    });
    read_with_thread(client_socket);
  }
}

int transport::start_server(int port){
  sockfd = listen_on(port);
  if(sockfd == -1){
    return -1;
  }
  printf("Listening on port %d\n", port);
  accept_loop(sockfd, false);
  return 0;
}

// Open the port here, so a port that is taken is reported to the caller
// instead of silently ending a background thread.
bool transport::start_with_thread(int port){
  sockfd = listen_on(port);
  if(sockfd == -1){
    return false;
  }
  printf("Listening on port %d\n", port);
  std::thread(&transport::accept_loop, this, sockfd, false).detach();
  return true;
}

int transport::start_web_server(int port, const std::string& root){
  web_root = root;
  web_sockfd = listen_on(port);
  if(web_sockfd == -1){
    return -1;
  }
  accept_loop(web_sockfd, true);
  return 0;
}

bool transport::start_web_with_thread(int port, const std::string& root){
  web_sockfd = listen_on(port);
  if(web_sockfd == -1){
    return false;
  }
  web_root = root;
  std::thread(&transport::accept_loop, this, web_sockfd, true).detach();
  return true;
}

void transport::read_with_thread(int client_socket){
  std::thread t(&transport::read, this, client_socket);
  t.detach();
}

void transport::read(int client_socket){
  line_buffer buffer;
  char chunk[4096];
  std::vector<std::string> lines;

  while(true){
    int n = recv(client_socket, chunk, sizeof(chunk), 0);
    if(n <= 0){
      break;
    }
    lines.clear();
    bool ok = buffer.feed(chunk, n, lines);
    for(const std::string& line : lines){
      if(verbose){
        printf("<- %d: %s\n", client_socket, line.c_str());
      }
      run_locked([&]{
        if(p_listener) p_listener->on_message(client_socket, line);
      });
    }
    if(!ok){
      printf("Client %d sent a line that is too long, disconnecting\n", client_socket);
      break;
    }
  }
  finish(client_socket);
}

// Tell the game first, then close: once closed the fd number can be reused
// by a new connection.
void transport::finish(int client_socket){
  run_locked([&]{
    if(p_listener) p_listener->on_disconnect(client_socket);
  });
  {
    std::lock_guard<std::mutex> guard(ws_lock);
    ws_clients.erase(client_socket);
  }
  close(client_socket);
  printf("Client %d disconnected\n", client_socket);
}

// ---------------------------------------------------------------- browsers

void transport::serve_http(int client_socket){
  // A browser sends its request right away; do not let a silent connection
  // hold a thread forever.
  timeval wait = {10, 0};
  setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));

  std::string head;
  char chunk[4096];
  size_t end;
  while((end = head.find("\r\n\r\n")) == std::string::npos){
    int n = head.size() > 16 * 1024 ? 0 : recv(client_socket, chunk, sizeof(chunk), 0);
    if(n <= 0){
      close(client_socket);
      return;
    }
    head.append(chunk, n);
  }
  std::string after = head.substr(end + 4);   // a WebSocket frame may follow at once
  head.resize(end + 4);

  http_request request;
  std::string answer;
  if(!parse_http_request(head, request)){
    answer = http_response(400, "text/plain; charset=utf-8", "Bad request\n");
  }
  else if(request.method != "GET"){
    answer = http_response(405, "text/plain; charset=utf-8", "Only GET is supported\n");
  }
  else if(is_websocket_upgrade(request)){
    if(!same_origin(request)){
      answer = http_response(403, "text/plain; charset=utf-8",
                             "Open the game from this server's own page\n");
    }
    else if(send_all(client_socket, websocket_accept_response(request))){
      timeval none = {0, 0};
      setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &none, sizeof(none));
      {
        std::lock_guard<std::mutex> guard(ws_lock);
        ws_clients.insert(client_socket);
      }
      printf("Client %d connected from a browser\n", client_socket);
      run_locked([&]{
        if(p_listener) p_listener->on_connect(client_socket);
      });
      read_websocket(client_socket, after);
      return;
    }
  }
  else{
    std::string relative;
    std::ifstream file;
    if(!web_root.empty() && web_file_path(request.target, relative)){
      file.open(web_root + "/" + relative, std::ios::binary);
    }
    if(file.is_open()){
      std::stringstream body;
      body << file.rdbuf();
      answer = http_response(200, content_type_for(relative), body.str());
    }
    else{
      answer = http_response(404, "text/plain; charset=utf-8", "Not found\n");
    }
  }
  send_all(client_socket, answer);
  close(client_socket);
}

void transport::read_websocket(int client_socket, const std::string& already_read){
  ws_decoder decoder;
  std::vector<ws_message> messages;
  char chunk[4096];
  bool open = decoder.feed(already_read.data(), already_read.size(), messages);

  while(open){
    for(const ws_message& m : messages){
      if(m.opcode == WS_TEXT){
        std::string line = m.payload;
        while(!line.empty() && (line.back() == '\n' || line.back() == '\r')){
          line.pop_back();
        }
        if(line.empty()){
          continue;
        }
        if(verbose){
          printf("<- %d: %s\n", client_socket, line.c_str());
        }
        run_locked([&]{
          if(p_listener) p_listener->on_message(client_socket, line);
        });
      }
      else if(m.opcode == WS_PING){
        // Under the lock, so the answer cannot land inside a message the
        // game is sending to this client at the same moment.
        run_locked([&]{ send_all(client_socket, websocket_frame(m.payload, WS_PONG)); });
      }
      else if(m.opcode == WS_CLOSE || m.opcode == WS_BINARY){
        run_locked([&]{ send_all(client_socket, websocket_frame("", WS_CLOSE)); });
        open = false;
        break;
      }
    }
    if(!open){
      break;
    }
    int n = recv(client_socket, chunk, sizeof(chunk), 0);
    if(n <= 0){
      break;
    }
    messages.clear();
    open = decoder.feed(chunk, n, messages);
    if(!open){
      printf("Client %d broke the WebSocket protocol, disconnecting\n", client_socket);
    }
  }
  finish(client_socket);
}

bool transport::is_websocket(int client_socket){
  std::lock_guard<std::mutex> guard(ws_lock);
  return ws_clients.count(client_socket) > 0;
}

void transport::disconnect(){
  if(sockfd != -1){
    close(sockfd);
    sockfd = -1;
  }
  if(web_sockfd != -1){
    close(web_sockfd);
    web_sockfd = -1;
  }
}

void transport::set_listener(observer* ob){
  p_listener = ob;
}

void transport::run_locked(const std::function<void()>& fn){
  std::lock_guard<std::mutex> guard(dispatch_lock);
  fn();
}

void transport::send_line(int client_socket, const std::string& line){
  if(client_socket < 0){
    return;
  }
  if(verbose){
    printf("-> %d: %s\n", client_socket, line.c_str());
  }
  if(!send_all(client_socket, is_websocket(client_socket) ? websocket_frame(line) : line + "\n")){
    printf("Send to client %d failed\n", client_socket);
  }
}
