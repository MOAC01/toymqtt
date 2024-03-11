
OBJS=main.o configuration.o protocol.o object.o threadpool.o
CC=g++
CFLAGS+=-std=c++11
toymqtt:$(OBJS)
	$(CC) $(OBJS) -o toymqtt
main.o:main.cpp
	$(CC) main.cpp $(CFLAGS) -c main.o
configuration.o:configuration.cpp
	$(CC) configuration.cpp $(CFLAGS) -c configuration.o
protocol.o:protocol.cpp
	$(CC) protocol.cpp $(CFLAGS) -c protocol.o
threadpool.o:threadpool.cpp
	$(CC) threadpool.cpp $(CFLAGS) -lpthread -c threadpool.o
	
clean:
	rm *.o toymqtt -rf
