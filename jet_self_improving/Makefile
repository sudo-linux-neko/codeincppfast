CXX = g++
CXXFLAGS = -std=c++17 -O3 -march=native -Wall -Wextra
LDFLAGS = -lpthread

INCLUDES = -Ijet/src

SOURCES = jet_self_improving/main.cpp jet_self_improving/self_play.cpp
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = jet_self_improving/engine

all: $(TARGET)

$(TARGET): $(OBJECTS)
jet$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)
jet@echo "Build complete: $(TARGET)"

%.o: %.cpp
jet$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
jetrm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
