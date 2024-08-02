#ifndef _TABLE_H_
#define _TABLE_H_

#include "observer.h"
#include "transport.h"
#include "player.h"
#include "card.h"
#include "deck_of_card.h"

class table: public observer{
  private:
    deck_of_card* dcards = new deck_of_card();
    transport* tp = NULL;
    player* player_list[12];
    int number_of_player = 0;
    int current_game = 0;
  public:
    table(){};
    virtual void handle_message(message_content* msg, int socket_id);
    void set_transport(transport* tp);
    void create_player(int client_socket);
    player* search_player_by_name(char* name);
    player* search_player_by_socket(int socket_id);
    int my_strcmp(const char* str1, const char* str2);
};
#endif
