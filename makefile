CC = gcc
CFLAGS = -Wall -pthread
SERVER_DEPS =
CLIENT_DEPS =
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS = $(shell pkg-config --libs gtk+-3.0)

all: enhanced_server enhanced_client gtk_client

# Server
enhanced_server: server.c $(SERVER_DEPS)
	$(CC) $(CFLAGS) -o enhanced_server server.c

# Client
enhanced_client: client.c $(CLIENT_DEPS)
	$(CC) $(CFLAGS) -o enhanced_client client.c

# GTK Client
gtk_client: gtk_client.c $(CLIENT_DEPS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o gtk_client gtk_client.c $(GTK_LIBS)

clean:
	rm -f enhanced_server enhanced_client gtk_client
