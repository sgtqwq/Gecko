# Makefile for Gecko Chess Engine

# Detect OS
ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
    EXE := gecko.exe
    RM := del /Q
else
    DETECTED_OS := $(shell uname -s)
    EXE := gecko
    RM := rm -f
endif

# Compiler
CXX := g++

# Compiler flags
CXXFLAGS := -std=c++17 -O3 -march=native -flto
CXXFLAGS += -Wall -Wextra -pedantic
CXXFLAGS += -DNDEBUG

# Debug flags (use make DEBUG=1)
ifdef DEBUG
    CXXFLAGS := -std=c++17 -g -O0
    CXXFLAGS += -Wall -Wextra -pedantic
    CXXFLAGS += -fsanitize=address,undefined
endif

# Linker flags
LDFLAGS := -flto
ifeq ($(DETECTED_OS),Windows)
    LDFLAGS += -static
else
    LDFLAGS += -pthread
endif

ifdef DEBUG
    LDFLAGS += -fsanitize=address,undefined
endif

# Source files
SRCS := main.cpp bitboard.cpp position.cpp movegen.cpp eval.cpp tt.cpp search.cpp uci.cpp
OBJS := $(SRCS:.cpp=.o)

# Targets
.PHONY: all clean

all: $(EXE)

$(EXE): $(OBJS)
	$(CXX) $(OBJS) -o $(EXE) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
ifeq ($(DETECTED_OS),Windows)
	$(RM) $(OBJS) $(EXE)
else
	$(RM) $(OBJS) $(EXE)
endif

# Dependencies
main.o: main.cpp types.h bitboard.h position.h movegen.h eval.h search.h tt.h uci.h
bitboard.o: bitboard.cpp bitboard.h types.h
position.o: position.cpp position.h types.h bitboard.h
movegen.o: movegen.cpp movegen.h types.h position.h bitboard.h
eval.o: eval.cpp eval.h types.h position.h bitboard.h
tt.o: tt.cpp tt.h types.h position.h bitboard.h
search.o: search.cpp search.h types.h position.h movegen.h eval.h tt.h bitboard.h
uci.o: uci.cpp uci.h position.h movegen.h search.h tt.h bitboard.h