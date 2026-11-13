#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <functional>
#include <mutex>
#include "observer.h"

// TCP server. Wire format: one JSON message per line ("\n"-terminated).
// Every client gets its own read thread; all events are handed to the
// listener one at a time under a single lock, so the game never sees two
// messages at once.
class transport: public sender{
  private:
    transport();

    static transport* instance;
    int sockfd;
    observer* p_listener = NULL;
    bool verbose = true;
    std::mutex dispatch_lock;
  public:
    static transport* get_instance();

    int start_server(int port);
    void start_with_thread(int port);
    void read_with_thread(int client_socket);
    void read(int client_socket);
    void disconnect();

    void set_listener(observer* ob);
    void set_verbose(bool v){verbose = v;};

    // Run `fn` while holding the same lock as message handling (for timers).
    void run_locked(const std::function<void()>& fn);

    virtual void send_line(int client_socket, const std::string& line);
};

#endif
