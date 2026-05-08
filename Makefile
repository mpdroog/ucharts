# Candlestick Chart Makefile
# Requires: GLFW3 (brew install glfw on macOS)

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
    LDFLAGS += -L/opt/homebrew/lib -L/usr/local/lib
    LDFLAGS += -lglfw -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
else
    # Linux
    LDFLAGS += -lglfw -lGL -ldl
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
SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)

TARGET = ucharts

all: check-deps $(TARGET)

$(TARGET): $(OBJS) $(IMGUI_OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Use static pattern rules to ensure correct flags are used
$(OBJS): %.o: %.cpp
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

# Test target (no GUI dependencies)
TEST_CXXFLAGS = -std=c++11 -O2 -Wall -Wextra -Wpedantic -Werror

test: test_logic
	./test_logic

test_logic: test_logic.cpp
	$(CXX) $(TEST_CXXFLAGS) -o $@ $<

clean:
	rm -f $(OBJS) $(IMGUI_OBJS) $(TARGET) test_logic

clean-all: clean
	rm -rf $(IMGUI_DIR)

.PHONY: all clean clean-all check-deps test
