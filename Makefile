CXX ?= c++
CPPFLAGS ?= -Isrc
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -Werror -O2
LDFLAGS ?=

TARGET := build/umami
TEST_TARGET := build/umami_tests
COMMON_SOURCES := src/Catalog.cpp src/CatalogRefresh.cpp src/InstalledState.cpp src/JsonSupport.cpp src/Logger.cpp src/PackageManager.cpp src/Paths.cpp src/ProcessRunner.cpp src/RecipeManager.cpp
SOURCES := src/main.cpp $(COMMON_SOURCES)
OBJECTS := $(SOURCES:src/%.cpp=build/%.o)
TEST_OBJECTS := build/tests.o $(COMMON_SOURCES:src/%.cpp=build/%.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS)

$(TEST_TARGET): $(TEST_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_OBJECTS)

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/tests.o: tests/tests.cpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf build
