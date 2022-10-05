CFLAGS = -g -Wall -Wextra
all: upush_server upush_client

upush_client: upush_client.c send_packet.c host.c
	gcc $(CFLAGS) $^ -o $@

upush_server: upush_server.c send_packet.c host.c
	gcc $(CFLAGS) $^ -o $@

clean:
	rm -f upush_server upush_client
