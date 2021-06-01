CPPFLAGS = -std=c++17 -Wextra  -pthread

all: T C


debug: CPPFLAGS += -g
debug: T C

T : tracker.cpp
	g++ $(CPPFLAGS) tracker.cpp -o tracker

C :	client.cpp
	g++ $(CPPFLAGS) client.cpp -o client

clean:
	rm -f client
	rm -f tracker