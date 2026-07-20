# Makefile for Gecko Chess Engine (OpenBench-friendly)

EXE ?= gecko

ifeq ($(OS),Windows_NT)
    DETECTED_OS := Windows
    EXEEXT := .exe
    RM := rm -f
else
    DETECTED_OS := $(shell uname -s)
    EXEEXT :=
    RM := rm -f
endif

TARGET := $(EXE)$(EXEEXT)

CXX ?= g++
EVALFILE ?= nnue.bin

CXXFLAGS_BASE := -std=c++17 -O3 -march=native -flto -DNDEBUG -Wall -Wextra -pedantic -mpopcnt
LDFLAGS_BASE := -flto

ifeq ($(DETECTED_OS),Windows)
    LDFLAGS_BASE += -static
else
    LDFLAGS_BASE += -pthread
endif

ifdef DEBUG
    CXXFLAGS_BASE := -std=c++17 -g -O0 -Wall -Wextra -pedantic -fsanitize=address,undefined
    LDFLAGS_BASE += -fsanitize=address,undefined
endif

CXXFLAGS ?= $(CXXFLAGS_BASE)
LDFLAGS ?= $(LDFLAGS_BASE)

SRCS := main.cpp bitboard.cpp position.cpp movegen.cpp eval.cpp nnue.cpp tt.cpp search.cpp search_params.cpp uci.cpp
OBJS := $(SRCS:.cpp=.o)

ifneq ($(strip $(EVALFILE)),)
    EVAL_ESCAPED := $(shell printf '%s' "$(EVALFILE)" | sed -e 's/[\/&]/\\&/g')
    EMBED_SRC := embedded_nnue.S
    EMBED_OBJ := embedded_nnue.o
    OBJS += $(EMBED_OBJ)
    override CXXFLAGS += -DOPENBENCH_EMBED_NNUE
endif

.PHONY: all clean profile-build strip

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

ifneq ($(strip $(EVALFILE)),)
$(EMBED_SRC): embedded_nnue.S.in $(EVALFILE)
	sed 's|@EVALFILE@|$(EVAL_ESCAPED)|g' $< > $@

$(EMBED_OBJ): $(EMBED_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $@
endif

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

profile-build: all

strip: $(TARGET)
	strip $(TARGET) || true

clean:
	-$(RM) $(OBJS) $(TARGET) embedded_nnue.S
