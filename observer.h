#ifndef _OBSERVER_H_
#define _OBSERVER_H_

#include <string>

// Receives events from the network layer. Each message is one JSON document
// (one line on the wire, without the trailing newline).
class observer{
  public:
    virtual ~observer(){}
    virtual void on_connect(int client){ (void)client; }
    virtual void on_message(int client, const std::string& line) = 0;
    virtual void on_disconnect(int client){ (void)client; }
};

// Sends one message (one line) to a client.
class sender{
  public:
    virtual ~sender(){}
    virtual void send_line(int client, const std::string& line) = 0;
};

#endif
