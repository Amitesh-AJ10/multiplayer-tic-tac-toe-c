# Makefile for Multiplayer Tic-Tac-Toe using Raw Sockets in C

CC = gcc
CFLAGS = -Wall -Wextra -g
SERVER_SRC = server.c
CLIENT_SRC = client.c
SERVER_EXEC = ttt_server
CLIENT_EXEC = ttt_client

all: $(SERVER_EXEC) $(CLIENT_EXEC)

$(SERVER_EXEC): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $^

$(CLIENT_EXEC): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(SERVER_EXEC) $(CLIENT_EXEC)

# Run the server (needs root privileges for raw sockets)
run_server: $(SERVER_EXEC)
	sudo ./$(SERVER_EXEC)

# Run the client (connect to server, localhost by default)
run_client: $(CLIENT_EXEC)
	./$(CLIENT_EXEC) 127.0.0.1

.PHONY: all clean run_server run_client