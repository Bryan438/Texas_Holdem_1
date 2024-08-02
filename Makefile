CC = g++
CFLAGS = -Wall -g

thserver: controller.o card.o deck_of_card.o player.o table_server.o transport.o message_content.o table.o
	$(CC) $(CFLAGS) -o thserver controller.o card.o deck_of_card.o player.o table_server.o transport.o message_content.o table.o

controller.o: card.h controller.cpp deck_of_card.h player.h table_server.h message_content.h observer.h table.h
	$(CC) $(CFLAGS) -c controller.cpp

card.o: card.h card.cpp
	$(CC) $(CFLAGS) -c card.cpp

deck_of_card.o: deck_of_card.h deck_of_card.cpp
	$(CC) $(CFLAGS) -c deck_of_card.cpp
  
player.o: player.h player.cpp
	$(CC) $(CFLAGS) -c player.cpp

table_server.o: table_server.h table_server.cpp
	$(CC) $(CFLAGS) -c table_server.cpp

transport.o: transport.h transport.cpp
	$(CC) $(CFLAGS) -c transport.cpp
 
message_content.o: message_content.h message_content.cpp
	$(CC) $(CFLAGS) -c message_content.cpp

table.o: table.h table.cpp observer.h
	$(CC) $(CFLAGS) -c table.cpp
