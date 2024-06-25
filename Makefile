CC = g++
CFLAGS = -Wall -g

main: controller.o card.o deck_of_card.o player.o table_server.o
	$(CC) $(CFLAGS) -o main controller.o card.o deck_of_card.o player.o table_server.o

controller.o: card.h controller.cpp deck_of_card.h player.h table_server.h
	$(CC) $(CFLAGS) -c controller.cpp

card.o: card.h card.cpp
	$(CC) $(CFLAGS) -c card.cpp

deck_of_card.o: deck_of_card.h deck_of_card.cpp
	$(CC) $(CFLAGS) -c deck_of_card.cpp
  
player.o: player.h player.cpp
	$(CC) $(CFLAGS) -c player.cpp

table_server.o: table_server.h table_server.cpp
	$(CC) $(CFLAGS) -c table_server.cpp

