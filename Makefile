CXX = g++
CXXFLAGS = -std=c++17 -Wall -g -pthread
LDFLAGS = -pthread

SRCS = main.cpp \
       src/MemoryAllocator.cpp \
       src/BuddyAllocator.cpp \
       src/SlabAllocator.cpp \
       src/Cache.cpp \
       src/VirtualMemory.cpp \
       src/MultiLevelPageTable.cpp \
       src/MMap.cpp \
       src/TraceBenchmark.cpp \
       src/ThreadedBenchmark.cpp \
       src/Visualization.cpp

OBJS = $(SRCS:.cpp=.o)
TARGET = memsim

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

bench: CXXFLAGS += -O2
bench: clean $(TARGET)
	@echo "Built with -O2 optimizations for benchmarking."

clean:
	rm -f $(OBJS) *.o src/*.o $(TARGET)

