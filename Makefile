
all: server client

server: source/server.o source/common.o
	$(CXX) -o $@ $^

client: source/client.o source/common.o
	$(CXX) -o $@ $^

clean:
	$(RM) source/server.o source/common.o source/client.o server client
