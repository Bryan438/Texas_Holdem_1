#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include "observer.h"

// The network. Two doors lead to the same table:
//   - a TCP port for the terminal client and bots: one JSON message per line;
//   - a web port for browsers: serves the web client's files over HTTP and
//     carries the same JSON messages over a WebSocket, one per message.
// Every client gets its own read thread; all events are handed to the
// listener one at a time under a single lock, so the game never sees two
// messages at once.
class transport: public sender{
  private:
    transport();

    static transport* instance;
    int sockfd;
    observer* p_listener = NULL;
    int web_sockfd = -1;
    std::string web_root;
    bool verbose = true;
    std::mutex dispatch_lock;
    std::mutex ws_lock;
    std::set<int> ws_clients;   // clients that get WebSocket frames instead of lines

    void accept_loop(int listen_fd, bool web);
    void read_websocket(int client_socket, const std::string& already_read);
    bool is_websocket(int client_socket);
    void finish(int client_socket);
  public:
    static transport* get_instance();

    int start_server(int port);
    // Open the port, then accept in the background. False: the port could not
    // be opened (the reason is printed).
    bool start_with_thread(int port);
    void read_with_thread(int client_socket);
    void read(int client_socket);
    void disconnect();

    // Browsers: serve the files in `root` and accept WebSocket connections.
    int start_web_server(int port, const std::string& root);
    bool start_web_with_thread(int port, const std::string& root);
    void set_web_root(const std::string& root){ web_root = root; }
    // One browser connection: answers a file request, or becomes a WebSocket.
    void serve_http(int client_socket);

    void set_listener(observer* ob);
    void set_verbose(bool v){verbose = v;};

    // Run `fn` while holding the same lock as message handling (for timers).
    void run_locked(const std::function<void()>& fn);

    // One message to a client, as a line or as a WebSocket frame.
    virtual void send_line(int client_socket, const std::string& line);
};

#endif
