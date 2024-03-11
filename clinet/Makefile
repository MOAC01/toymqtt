toymqtt-client:client.o
	g++ client.o -o toymqtt-client
client.o:client.cpp
	g++ client.cpp -std=c++11 -lpthread -c client.o

clean:
	rm *.o toymqtt-client -rf