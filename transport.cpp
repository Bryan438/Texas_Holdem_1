#include <cstdio>
#include <cstring>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include "transport.h"
#include "line_buffer.h"

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

int transport::start_server(int port){
  // A client that disconnects must not kill the server on the next send().
  signal(SIGPIPE, SIG_IGN);

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sockfd == -1){
    printf("Create socket failed\n");
    return -1;
  }
  int reuse = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if(bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
    printf("Bind failed (is port %d already in use?)\n", port);
    return -1;
  }
  if(listen(sockfd, 16) < 0){
    printf("Listen failed\n");
    return -1;
  }
  printf("Listening on port %d\n", port);

  while(true){
    sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    int client_socket = accept(sockfd, (struct sockaddr*)&client_addr, &addrlen);
    if(client_socket < 0){
      continue;
    }
    // A client that stops reading must not freeze the whole table.
    timeval timeout = {2, 0};
    setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    printf("Client %d connected from %s\n", client_socket, inet_ntoa(client_addr.sin_addr));

    run_locked([&]{
      if(p_listener) p_listener->on_connect(client_socket);
    });
    read_with_thread(client_socket);
  }
}

void transport::start_with_thread(int port){
  std::thread t(&transport::start_server, this, port);
  t.detach();
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

  // Tell the game first, then close: once closed the fd number can be reused
  // by a new connection.
  run_locked([&]{
    if(p_listener) p_listener->on_disconnect(client_socket);
  });
  close(client_socket);
  printf("Client %d disconnected\n", client_socket);
}

void transport::disconnect(){
  if(sockfd != -1){
    close(sockfd);
    sockfd = -1;
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
  std::string data = line + "\n";
  size_t sent = 0;
  // send() may write only part of the buffer; keep going until it is all out.
  while(sent < data.size()){
    ssize_t n = send(client_socket, data.data() + sent, data.size() - sent, 0);
    if(n <= 0){
      printf("Send to client %d failed\n", client_socket);
      return;
    }
    sent += n;
  }
}
