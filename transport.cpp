#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include "observer.h"
#include "transport.h"

transport* transport::instance = NULL;

transport::transport(){
  sockfd = -1;
  client_socket_temp = -1;
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

  sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_addr.s_addr = INADDR_ANY;
  sockaddr.sin_port = htons(port);

  if (bind(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0) {
    printf("Bind failed\n");
    return -1;
  }

  if (listen(sockfd, 10) < 0) {
    printf("Listen failed\n");
    return -1;
  }

  socklen_t addrlen = sizeof(sockaddr);
  while(true){
    printf("Awaiting... \n");
    int client_socket = accept(sockfd, (struct sockaddr*)&sockaddr, &addrlen);
    //Temp to be removed
    client_socket_temp = client_socket;

    if (client_socket < 0) {
      printf("Accept failed\n");
    } else {
      message_content* ini_msg = new message_content();
      ini_msg->set_command(3);
      ini_msg->set_intmessage(client_socket);

      dispatch(ini_msg, client_socket);
      read_with_thread(client_socket);
    }
  }
}

void transport::set_listener(observer* ob){
  p_listener = ob;
}

void transport::dispatch(message_content* msg, int client_socket){
  {
    std::lock_guard<std::mutex> guard(dispatch_lock);
    if(p_listener != NULL){
      p_listener->handle_message(msg, client_socket);
    }
  }
  delete msg;
}

void transport::start_with_thread(){
  std::thread thread_start(&transport::start_server, this, 4000);
  thread_start.detach();
}

void transport::read_with_thread(int client_socket){
  std::thread thread_read(&transport::read, this, client_socket);
  thread_read.detach();
}

void transport::disconnect(){
  if(sockfd != -1){
    close(sockfd);
    sockfd = -1;
  }
}

void transport::close_client_socket(int client_socket){
  if(client_socket != -1){
    close(client_socket);
  }
}

void transport::decode_header(const char* buf, int* command, int* length){
  uint32_t header;
  memcpy(&header, buf, 4);
  header = ntohl(header);
  *command = (header >> 24) & 0xFF;
  *length = (header >> 16) & 0xFF;
}

// Reads frames from one client until it disconnects. TCP is a byte stream:
// one recv() can return half a frame or several frames, so bytes are kept in
// `buf` until a whole frame is there.
void transport::read(int client_socket){
  char buf[1024];
  int hasrecvd = 0;

  while(true){
    int n = recv(client_socket, buf + hasrecvd, sizeof(buf) - hasrecvd, 0);
    if(n <= 0){
      printf("Client %d disconnected\n", client_socket);
      break;
    }
    hasrecvd += n;

    while(hasrecvd >= FRAME_HEADER_SIZE){
      int command = 0;
      int length = 0;
      decode_header(buf, &command, &length);
      int frame_size = FRAME_HEADER_SIZE + length;
      if(hasrecvd < frame_size){
        break; // wait for the rest of this frame
      }

      if(verbose){
        char* hexbuf = convert_buffer_to_base64(buf, frame_size);
        printf("length = %d, recvd buf = %s\n", length, hexbuf);
        delete[] hexbuf;
      }

      dispatch(deserialize(buf), client_socket);
      hasrecvd -= frame_size;
      memmove(buf, buf + frame_size, hasrecvd);
    }
  }
  close_client_socket(client_socket);
}

void transport::serialize(int client_socket, int command, int length, const char* buf){
  if(length < 0 || length > MAX_PAYLOAD){
    printf("Message too long (%d bytes), not sent\n", length);
    return;
  }
  char packet[FRAME_HEADER_SIZE + MAX_PAYLOAD];
  uint32_t header = ((uint32_t)(command & 0xFF) << 24) | ((uint32_t)length << 16);
  uint32_t n_header = htonl(header);
  memcpy(packet, &n_header, 4);
  if(length > 0){
    memcpy(packet + 4, buf, length);
  }
  if(send_msg(client_socket, packet, length + 4) == -1){
    printf("Error sending\n");
  }
}

message_content* transport::deserialize(const char* buffer){
  int command = 0;
  int length = 0;
  decode_header(buffer, &command, &length);

  message_content* packet = new message_content();
  packet->set_command(command);

  // Even commands carry text/bytes, odd commands carry an optional int.
  if(command % 2 == 0){
    packet->set_charmessage((char*)buffer + 4, length);
  }
  else if(length >= 4){
    uint32_t value;
    memcpy(&value, buffer + 4, 4);
    packet->set_intmessage((int)ntohl(value));
  }
  return packet;
}

int transport::send_msg(int client_socket, char* message, int length){
  if(verbose){
    char* hexbuf = convert_buffer_to_base64(message, length);
    printf("Sending... Socket = %d, length = %d, message = %s\n", client_socket, length, hexbuf);
    delete[] hexbuf;
  }

  if(send(client_socket, message, length, 0) == -1){
    return -1;
  }
  return 1;
}

// Despite the name this is a hex dump, used for debug logging.
char* transport::convert_buffer_to_base64(const char* message, int length){
  static const char digits[] = "0123456789ABCDEF";
  char* msg = new char[2*length + 1];
  for(int i = 0; i < length; i++){
    unsigned char byte = message[i];
    msg[2*i] = digits[byte >> 4];
    msg[2*i+1] = digits[byte & 0x0F];
  }
  msg[2*length] = 0;
  return msg;
}
