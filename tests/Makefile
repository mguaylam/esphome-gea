CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

BINS = test_protocol test_decoder

.PHONY: test clean
test: $(BINS)
	@for b in $(BINS); do \
		echo "=== $$b ==="; \
		./$$b || exit 1; \
	done

test_protocol: test_protocol.cpp ../components/gea/protocol.h
	$(CXX) $(CXXFLAGS) -o $@ $<

test_decoder: test_decoder.cpp ../components/gea/decoder.h
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(BINS)
