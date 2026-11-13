#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <mutex>
#include "observer.h"
#include "message_content.h"

// Wire format: 4-byte header in network byte order, then the payload.
//   bits 31..24 command, bits 23..16 payload length (so at most 255 bytes).
#define FRAME_HEADER_SIZE 4
#define MAX_PAYLOAD 255

class transport{
  private:
    transport();

    static transport* instance;
    int sockfd;
    observer* p_listener = NULL;
    int client_socket_temp;
    bool verbose = true;
    // Every client has its own read thread; this lock makes sure the table
    // only ever handles one message at a time.
    std::mutex dispatch_lock;
  public:
    ~transport(){};
    static transport* get_instance();

    void disconnect();
    void close_client_socket(int cs);
    int get_client_socket(){return client_socket_temp;};

    int start_server(int port = 4000);
    void start_with_thread();
    void read_with_thread(int client_socket);
    void read(int client_socket);
    void dispatch(message_content* msg, int client_socket);

    static void decode_header(const char* buf, int* command, int* length);
    void serialize(int client_socket, int command, int length, const char* buffer);
    message_content* deserialize(const char* buf);
    int send_msg(int client_socket, char* message, int length);
    void set_listener(observer* ob);
    void set_verbose(bool v){verbose = v;};
    char* convert_buffer_to_base64(const char* message, int length);
};

#endif
