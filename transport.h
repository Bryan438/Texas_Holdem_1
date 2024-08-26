#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include "observer.h"
#include "message_content.h"

class transport{
  private:
    transport();

    static transport* instance;
    int sockfd;
    observer* p_listener = NULL;
    int client_socket_temp;
  public:
    ~transport(){};
    static transport* get_instance();

    void disconnect();
    void close_client_socket(int cs);
    int get_client_socket(){return client_socket_temp;};

    int start_server();
    void start_with_thread();
    void read_with_thread(int client_socket);
    void read(int client_socket);
    
    void serialize(int client_socket, int command, int length, char* buffer);
    message_content* deserialize(const char* buf);
    int send_msg(int client_socket, char* message, int length);
    void set_listener(observer* ob);
    char* convert_buffer_to_base64(char* message, int length);
};

#endif
