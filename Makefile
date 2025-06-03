# Compiler and flags
CXX = g++
CC = gcc
CXXFLAGS = -Wall -std=c++17 -I$(INC_DIR) -MMD -MP
CFLAGS = -Wall -I$(INC_DIR) -MMD -MP

# Add -g if debug is needed
LDFLAGS = -lgpiodcxx -lgpiod -lyuv -lturbojpeg -lavformat -lavcodec -lavutil -lswscale # -g

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

# Source and object files
CPP_SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
C_SOURCES   = $(wildcard $(SRC_DIR)/*.c)
CPP_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(CPP_SOURCES))
C_OBJECTS   = $(patsubst $(SRC_DIR)/%.c,   $(BUILD_DIR)/%.o, $(C_SOURCES))
DEPS = $(CPP_OBJECTS:.o=.d) $(C_OBJECTS:.o=.d)

# Output executable
TARGET = MyApp

# Default rule
all: $(BIN_DIR)/$(TARGET)

# Link object files into binary
$(BIN_DIR)/$(TARGET): $(CPP_OBJECTS) | $(BIN_DIR)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Compile each .cpp / .c to .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure build and bin directories exist
$(BUILD_DIR):
	@mkdir -p $@

$(BIN_DIR):
	@mkdir -p $@

# Clean build output
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

-include $(DEPS)

.PHONY: all clean