CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -g -O2 -pthread

# The game itself; the server adds the network (transport.cpp) on top.
CORE = card.cpp deck_of_card.cpp player.cpp table.cpp hand_evaluator.cpp pot_calculator.cpp
SRCS = $(CORE) transport.cpp
OBJS = $(SRCS:.cpp=.o)
HEADERS = $(wildcard *.h)

all: thserver thstats thbot thmatch

thserver: controller.o $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Reads the hand history the server writes and prints per-player stats.
thstats: thstats.o stats.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# A bot that joins a running server like a player.
thbot: thbot.o bot.o equity.o card.o hand_evaluator.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# Bots against bots on a table in one process: how strong is each style?
thmatch: thmatch.o match.o bot.o equity.o stats.o $(CORE:.cpp=.o)
	$(CXX) $(CXXFLAGS) -o $@ $^

# json.hpp is large: only these depend on it.
JSON_OBJS = table.o stats.o thstats.o bot.o match.o thbot.o thmatch.o
$(JSON_OBJS): %.o: %.cpp $(HEADERS) third_party/json.hpp
	$(CXX) $(CXXFLAGS) -c $<

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $<

# Tests build with AddressSanitizer + UBSan so memory bugs fail loudly.
TEST_FLAGS = $(CXXFLAGS) -I. -fsanitize=address,undefined -fno-omit-frame-pointer
TEST_SRCS = $(wildcard tests/*.cpp)

TESTED = $(SRCS) stats.cpp bot.cpp equity.cpp match.cpp

tests/run_tests: $(TEST_SRCS) $(TESTED) $(HEADERS) tests/test_framework.h third_party/json.hpp
	$(CXX) $(TEST_FLAGS) -o $@ $(TEST_SRCS) $(TESTED)

test: tests/run_tests
	./tests/run_tests

clean:
	rm -f *.o thserver thstats thbot thmatch tests/run_tests

.PHONY: all test clean
