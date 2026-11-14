CXX ?= c++
CXXFLAGS = -std=c++17 -Wall -g -pthread

SRCS = card.cpp deck_of_card.cpp player.cpp transport.cpp table.cpp \
       hand_evaluator.cpp pot_calculator.cpp
OBJS = $(SRCS:.cpp=.o)
HEADERS = $(wildcard *.h)

all: thserver thstats

thserver: controller.o $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Reads the hand history the server writes and prints per-player stats.
thstats: thstats.o stats.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# json.hpp is large: only these depend on it.
table.o stats.o thstats.o: %.o: %.cpp $(HEADERS) third_party/json.hpp
	$(CXX) $(CXXFLAGS) -c $<

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $<

# Tests build with AddressSanitizer + UBSan so memory bugs fail loudly.
TEST_FLAGS = $(CXXFLAGS) -I. -fsanitize=address,undefined -fno-omit-frame-pointer
TEST_SRCS = $(wildcard tests/*.cpp)

tests/run_tests: $(TEST_SRCS) $(SRCS) stats.cpp $(HEADERS) tests/test_framework.h third_party/json.hpp
	$(CXX) $(TEST_FLAGS) -o $@ $(TEST_SRCS) $(SRCS) stats.cpp

test: tests/run_tests
	./tests/run_tests

clean:
	rm -f *.o thserver thstats tests/run_tests

.PHONY: all test clean
