
CXX = g++
CFLAGS = -std=c++14 -O2 -Wall -g 

TARGET = main
OBJS = test.cpp

all: $(OBJS)
	$(CXX) $(CFLAGS) $(OBJS) -o $(TARGET)  -pthread 
clean:
	rm -rf *.o main
