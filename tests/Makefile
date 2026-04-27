CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

.PHONY: test clean
test: test_protocol
	./test_protocol

test_protocol: test_protocol.cpp ../components/gea/protocol.h
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f test_protocol
