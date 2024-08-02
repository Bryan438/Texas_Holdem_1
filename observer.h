#ifndef _OBSERVER_H_
#define _OBSERVER_H_

#include "message_content.h"

class observer{
  private:
  public:
    observer(){};
    virtual void handle_message(message_content* msg, int socket_id) = 0;
};

#endif
