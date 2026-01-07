CXX = g++
CXXFLAGS = -std=c++17 -Wall -g

SRCS = main.cpp \
       src/MemoryAllocator.cpp \
       src/BuddyAllocator.cpp \
       src/Cache.cpp \
       src/VirtualMemory.cpp

OBJS = $(SRCS:.cpp=.o)
TARGET = memsim

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) *.o src/*.o $(TARGET)

