CXX=clang++
CXXFLAGS=-c -std=c++14 -g -O2 -Wall -Wno-unused-function -Wshadow -fno-rtti `pkg-config --cflags tesseract`
LDFLAGS=-stdlib=libc++ -lpthread -g `pkg-config --libs tesseract` -llept
SOURCES=$(wildcard src/*.cpp)
OBJECTS=$(addprefix obj/,$(notdir $(SOURCES:.cpp=.o)))
EXECUTABLE=bin/trivia_oracle
DEPS=$(wildcard obj/*.d)

all: $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $(EXECUTABLE)
	dsymutil $(EXECUTABLE)
	cp $(EXECUTABLE) ./
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $(EXECUTABLE:.o=.dylib)

obj/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $< -o $@
	$(CXX) -MM -MP -MT $@ -MT obj/$*.d $(CXXFLAGS) $< > obj/$*.d

-include $(DEPS)

clean:
	rm obj/*.o
	rm obj/*.d
	rm bin/*
