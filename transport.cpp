#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include "observer.h"
#include "transport.h"
//#include "player.h"

transport::transport(){
  sockfd = -1;
}

int transport::start_server(){
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sockfd == -1){
    printf("Create socket failed\n");
    return -1;
  }
  sockaddr_in sockaddr;
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_addr.s_addr = INADDR_ANY;
  sockaddr.sin_port = htons(4002);

  if (bind(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0) {
    printf("Bind failed\n");
    return -1;
  }

  if (listen(sockfd, 10) < 0) {
    printf("Listen failed\n");
    return -1;
  }

  int addrlen = sizeof(sockaddr);
  while(true){
    printf("Awaiting... \n");
    int client_socket = accept(sockfd, (struct sockaddr*)&sockaddr, (socklen_t*)&addrlen);
    //Temp to be removed
    client_socket_temp = client_socket;

    if (client_socket < 0) {
      printf("Error");
    } else {
      message_content* ini_msg = new message_content();
      ini_msg->set_command(3);
      ini_msg->set_intmessage(client_socket);

      p_listener->handle_message(ini_msg, client_socket);
      read_with_thread(client_socket);
    }
  }
}

void transport::set_listener(observer* ob){
  p_listener = ob;
}

void transport::start_with_thread(){
  std::thread thread_start(&transport::start_server, this);
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
    client_socket = -1;
  }
}

void transport::read(int client_socket){
  int n = -1;
  char buf[1000];
  int flag;

  while(true){
    do{
      printf("size : %ld\n" , sizeof(buf));
      n = recv(client_socket, buf, sizeof(buf), 0);
      flag = ioctl(client_socket, FIONREAD, &flag);
    } while(flag > 0);

    if(n <= 0){
      printf("error\n");
      close_client_socket(client_socket);
      break;
    }
    else{
      message_content* msg_packet = deserialize(buf);
      p_listener->handle_message(msg_packet, client_socket);
    }
  }
}

void transport::serialize(int client_socket, int command, int length, char* buf){
  char packet[length + 4];
  int operation = 0;
  memset(packet, 0, length+4);

  command <<= 28;
  operation = command | operation;
  int length_cpy = length << 20;
  operation = length_cpy | operation;
  int n_operation = htonl(operation);
  memcpy(packet, &n_operation, 4);
  memcpy(packet+4, buf, length);
  if(send_msg(client_socket, packet, length + 4) == -1){
    printf("Error sending");
  }
}

message_content* transport::deserialize(const char* buffer){

  char message[50];
  int intmessage = 0;
  memset(message, 0, 30);

  int header = ntohl(*((int*) buffer));
  header >>= 20;
  int length = header & 0x0FF;
  header >>= 8;
  int command = header;
  memcpy(message, buffer+4, length);
  message_content* packet = new message_content();
  packet->set_command(command);
  if(command%2 == 0){
    packet->set_charmessage(message, length);
  }
  else{
    packet->set_intmessage(intmessage);
  }
  return packet;
}

int transport::send_msg(int client_socket, char* message, int length){
  if(send(client_socket, message, length, 0) == -1){
    printf("Error");
    return -1;
  }
  return 1;
} 
