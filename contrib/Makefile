# Makefile for fake/mock servers and integration tests

CXX = clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
PARENT_DIR = ..

.PHONY: all clean test

all: fake_iqfeed fake_tradezero

fake_iqfeed: fake_iqfeed.cpp
	$(CXX) $(CXXFLAGS) -o fake_iqfeed fake_iqfeed.cpp

fake_tradezero: fake_tradezero.cpp
	$(CXX) $(CXXFLAGS) -o fake_tradezero fake_tradezero.cpp

# Integration test (requires parent directory files)
test_with_mocks: test_with_mocks.cpp
	$(CXX) $(CXXFLAGS) -I$(PARENT_DIR) -o test_with_mocks test_with_mocks.cpp \
		$(PARENT_DIR)/iqfeed_tcp.cpp \
		$(PARENT_DIR)/tradezero_client.cpp \
		$(PARENT_DIR)/http_client.cpp \
		$(PARENT_DIR)/json_parser.cpp \
		-lcurl

# Run integration tests (starts servers in background, runs tests, kills servers)
test: all test_with_mocks
	@echo "Starting mock servers..."
	@./fake_iqfeed > /dev/null 2>&1 & echo $$! > .fake_iqfeed.pid
	@./fake_tradezero > /dev/null 2>&1 & echo $$! > .fake_tradezero.pid
	@sleep 1
	@echo "Running integration tests..."
	@./test_with_mocks || true
	@echo "Stopping mock servers..."
	@kill `cat .fake_iqfeed.pid` 2>/dev/null || true
	@kill `cat .fake_tradezero.pid` 2>/dev/null || true
	@rm -f .fake_iqfeed.pid .fake_tradezero.pid

clean:
	rm -f fake_iqfeed fake_tradezero test_with_mocks
	rm -f .fake_iqfeed.pid .fake_tradezero.pid
