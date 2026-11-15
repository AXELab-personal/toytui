# toytui — Makefile
#
# Használat:
#   make               -> release build (build/release/toytui)
#   make BUILD=debug   -> debug build (-g -O0)
#   make BUILD=sanitize-> ASan+UBSan build
#   make test          -> unit tesztek fordítása + futtatása
#   make e2e           -> end-to-end szkriptek (tests/e2e/*.sh) a release binárison
#   make clean

CXX      := g++
CC       := gcc
CXXSTD   := -std=c++17
WARN     := -Wall -Wextra -Wpedantic -Werror -Wno-psabi
BUILD    ?= release

ifeq ($(BUILD),debug)
  OPT := -O0 -g
else ifeq ($(BUILD),sanitize)
  OPT := -O0 -g -fsanitize=address,undefined
  LDFLAGS += -fsanitize=address,undefined
else
  OPT := -O2
endif

CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) -Isrc -Ithird_party/vterm/include -MMD -MP

# Vendored C code (libvterm, third_party/vterm): compiled as C99 WITHOUT our
# warning gate (-w): it is not our code, and it is kept unmodified.
CFLAGS_3P := -std=c99 -w $(OPT) -Ithird_party/vterm/include -Ithird_party/vterm/src -MMD -MP

SRC_DIR   := src
BUILD_DIR := build/$(BUILD)
OBJ_DIR   := $(BUILD_DIR)/obj

VTERM_SRCS := $(wildcard third_party/vterm/src/*.c)
VTERM_OBJS := $(patsubst third_party/vterm/src/%.c,$(OBJ_DIR)/vterm/%.o,$(VTERM_SRCS))

CORE_SRCS := $(filter-out $(SRC_DIR)/main.cpp,$(wildcard $(SRC_DIR)/*.cpp))
CORE_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(CORE_SRCS)) $(VTERM_OBJS)
MAIN_OBJ  := $(OBJ_DIR)/main.o

TARGET := $(BUILD_DIR)/toytui

UNIT_SRCS := $(wildcard tests/unit/test_*.cpp)
UNIT_BINS := $(patsubst tests/unit/%.cpp,$(BUILD_DIR)/tests/%,$(UNIT_SRCS))

.PHONY: all test e2e clean sanitize

all: $(TARGET)

$(TARGET): $(MAIN_OBJ) $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/vterm/%.o: third_party/vterm/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_3P) -c $< -o $@

$(BUILD_DIR)/tests/%: tests/unit/%.cpp $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Itests/unit $< $(CORE_OBJS) -o $@

test: $(UNIT_BINS)
	@ok=1; \
	for t in $(UNIT_BINS); do \
		echo "== $$t =="; \
		./$$t || ok=0; \
	done; \
	test $$ok -eq 1

# A szkriptek a binárist első argumentumként kapják (abszolút úttal, mert
# saját tmp-könyvtárba lépnek).
e2e: $(TARGET)
	@ok=1; \
	for t in tests/e2e/*.sh; do \
		echo "== $$t =="; \
		bash $$t $(abspath $(TARGET)) || ok=0; \
	done; \
	test $$ok -eq 1

sanitize:
	$(MAKE) BUILD=sanitize all test

clean:
	rm -rf build

-include $(CORE_OBJS:.o=.d) $(MAIN_OBJ:.o=.d)
