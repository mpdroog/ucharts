# Professional Trading Platform Makefile
# Requires: GLFW3, SQLite3 (brew install glfw sqlite on macOS)

CXX = clang++
LDFLAGS =

# ImGui directory
IMGUI_DIR = imgui

# Detect OS
UNAME_S := $(shell uname -s)

# Strict compiler flags for our code
CXXFLAGS = -std=c++11 -O2 \
    -Wall -Wextra -Wpedantic -Werror \
    -Wshadow -Wdouble-promotion -Wformat=2 -Wformat-truncation \
    -Wundef -Wconversion -Wsign-conversion \
    -Wcast-qual -Wcast-align \
    -Wstrict-overflow=5 -Wwrite-strings \
    -Wswitch-default -Wswitch-enum \
    -Wunreachable-code -Wduplicated-cond -Wduplicated-branches \
    -Wlogical-op -Wnull-dereference \
    -Wold-style-cast -Woverloaded-virtual \
    -Wnon-virtual-dtor -Wctor-dtor-privacy \
    -Wmissing-declarations -Wredundant-decls \
    -Wdisabled-optimization \
    -Winit-self -Wpointer-arith \
    -fno-common -fstrict-aliasing \
    -fstack-protector-strong

# Relaxed flags for ImGui (third-party code)
IMGUI_CXXFLAGS = -std=c++11 -O2 -Wall -Wextra

# Test flags (less strict to allow test macros)
TEST_CXXFLAGS = -std=c++11 -O2 -Wall -Wextra -Wpedantic -Werror

ifeq ($(UNAME_S), Darwin)
    # macOS - clang doesn't support all GCC warnings
    CXXFLAGS = -std=c++11 -O2 \
        -Wall -Wextra -Wpedantic -Werror \
        -Wshadow -Wdouble-promotion -Wformat=2 \
        -Wundef -Wconversion -Wsign-conversion \
        -Wcast-qual -Wcast-align \
        -Wstrict-overflow=5 -Wwrite-strings \
        -Wswitch-default -Wswitch-enum \
        -Wunreachable-code -Wnull-dereference \
        -Wold-style-cast -Woverloaded-virtual \
        -Wnon-virtual-dtor \
        -Wmissing-declarations -Wredundant-decls \
        -Wdisabled-optimization \
        -Winit-self -Wpointer-arith \
        -fno-common -fstrict-aliasing \
        -fstack-protector-strong
    CXXFLAGS += -I/opt/homebrew/include -I/usr/local/include
    IMGUI_CXXFLAGS += -I/opt/homebrew/include -I/usr/local/include
    TEST_CXXFLAGS += -I/opt/homebrew/include -I/usr/local/include
    LDFLAGS += -L/opt/homebrew/lib -L/usr/local/lib
    LDFLAGS += -lglfw -lsqlite3 -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
    TEST_LDFLAGS = -L/opt/homebrew/lib -L/usr/local/lib -lsqlite3
else
    # Linux
    LDFLAGS += -lglfw -lGL -ldl -lsqlite3
    TEST_LDFLAGS = -lsqlite3
endif

# ImGui sources
IMGUI_SRCS = $(IMGUI_DIR)/imgui.cpp \
             $(IMGUI_DIR)/imgui_draw.cpp \
             $(IMGUI_DIR)/imgui_tables.cpp \
             $(IMGUI_DIR)/imgui_widgets.cpp \
             $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
             $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

IMGUI_OBJS = $(IMGUI_SRCS:.cpp=.o)

# Include paths
CXXFLAGS += -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends
IMGUI_CXXFLAGS += -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends

# Main sources
MAIN_SRCS = main.cpp database.cpp market_data.cpp order_manager.cpp chart_widget.cpp ticker_widget.cpp positions_widget.cpp
MAIN_OBJS = $(MAIN_SRCS:.cpp=.o)

TARGET = ucharts

all: check-deps $(TARGET)

$(TARGET): $(MAIN_OBJS) $(IMGUI_OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Use static pattern rules to ensure correct flags are used
$(MAIN_OBJS): %.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(IMGUI_OBJS): %.o: %.cpp
	$(CXX) $(IMGUI_CXXFLAGS) -c -o $@ $<

# Check dependencies
check-deps:
	@if [ ! -d "$(IMGUI_DIR)" ]; then \
		echo "Downloading Dear ImGui..."; \
		git clone --depth 1 https://github.com/ocornut/imgui.git $(IMGUI_DIR); \
	fi
	@if ! pkg-config --exists glfw3 2>/dev/null && ! [ -f /opt/homebrew/lib/libglfw.dylib ] && ! [ -f /usr/local/lib/libglfw.dylib ]; then \
		echo ""; \
		echo "ERROR: GLFW3 not found!"; \
		echo "Install with: brew install glfw"; \
		echo ""; \
		exit 1; \
	fi

# Test targets
test: test_logic test_database test_market_data test_order_manager test_integration
	@echo "Running logic tests..."
	./test_logic
	@echo ""
	@echo "Running database tests..."
	./test_database
	@echo ""
	@echo "Running market data tests..."
	./test_market_data
	@echo ""
	@echo "Running order manager tests..."
	./test_order_manager
	@echo ""
	@echo "Running integration tests..."
	./test_integration
	@echo ""
	@echo "All tests passed!"

test_logic: test_logic.cpp
	$(CXX) $(TEST_CXXFLAGS) -o $@ $<

test_database: test_database.cpp database.cpp
	$(CXX) $(TEST_CXXFLAGS) -o $@ test_database.cpp database.cpp $(TEST_LDFLAGS)

test_market_data: test_market_data.cpp market_data.cpp
	$(CXX) $(TEST_CXXFLAGS) -o $@ test_market_data.cpp market_data.cpp

test_order_manager: test_order_manager.cpp order_manager.cpp database.cpp market_data.cpp
	$(CXX) $(TEST_CXXFLAGS) -o $@ test_order_manager.cpp order_manager.cpp database.cpp market_data.cpp $(TEST_LDFLAGS)

test_integration: test_integration.cpp order_manager.cpp database.cpp market_data.cpp
	$(CXX) $(TEST_CXXFLAGS) -o $@ test_integration.cpp order_manager.cpp database.cpp market_data.cpp $(TEST_LDFLAGS)

clean:
	rm -f $(MAIN_OBJS) $(IMGUI_OBJS) $(TARGET) test_logic test_database test_market_data test_order_manager test_integration test_ucharts.db test_order_manager.db test_integration.db

clean-all: clean
	rm -rf $(IMGUI_DIR)

.PHONY: all clean clean-all check-deps test
