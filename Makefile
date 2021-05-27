T : tracker.cpp
	g++ -std=c++11 -pthread tracker.cpp -o tracker

C :	client.cpp
	g++ -std=c++11 -pthread client.cpp -o client

clean:
	rm -f client
	rm -f tracker