#include "host.h"
#include "send_packet.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define PACKET_SIZE 1400
#define TIMEOUT_THRESHOLD 30

struct host* known_hosts = NULL;

void check_error(int res, char* msg)
{
    if (res == -1) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

time_t last_check;

void check_alive()
{
    struct host* prev = NULL;
    struct host* i = known_hosts;
    while (i != NULL) {
        if (i->block == 1) { // Client has not responded
            if (prev != NULL)
                prev->next = i->next;
            else
                known_hosts = i->next;
            printf("User %s has disconnected from the server\n", i->nick);
            struct host* next = i->next;
            free(i->nick);
            free(i);
            i = next;
        } else {
            i->block = 1;
            prev = i;
            i = i->next;
        }
    }
    last_check = time(NULL);
}

void ack(int sock, struct host* client, int num)
{
    char packet[PACKET_SIZE];
    sprintf(packet, "ACK %d OK", num);
    send_packet(sock, packet, strlen(packet), 0,
        (struct sockaddr*)&client->addr, sizeof(client->addr));
}

void lookup_nick(int sock, struct host* client, char* nick)
{
    struct host* client2 = hosts_by_name(known_hosts, nick);
    char packet[PACKET_SIZE];
    if (client2 == NULL) {
        sprintf(packet, "ACK %d NOT FOUND", client->num);
    } else {
        char* ip = malloc(16);
        inet_ntop(AF_INET, &client2->addr.sin_addr, ip, sizeof(client->addr));
        sprintf(packet, "ACK %d NICK %s %s PORT %u", client->num++, nick, ip, ntohs(client2->addr.sin_port));
        free(ip);
    }
    send_packet(sock, packet, strlen(packet), 0,
        (struct sockaddr*)&client->addr, sizeof(client->addr));
}

void list_clients(int sock, struct host* client)
{
    char packet[PACKET_SIZE];
    int n = sprintf(packet, "ACK %d NICKS", client->num);
    for (struct host* i = known_hosts; i != NULL; i = i->next) {
        packet[n++] = ' ';
        strcpy(packet + n, i->nick);
        n += strlen(i->nick);
    }
    send_packet(sock, packet, strlen(packet), 0,
        (struct sockaddr*)&client->addr, sizeof(client->addr));
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: ./upush_server <port> <loss_probability>\n");
        return EXIT_FAILURE;
    }
    set_loss_probability(atof(argv[2]) / 100);
    struct sockaddr_in server;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    server.sin_port = htons(atoi(argv[1]));
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_family = AF_INET;
    int rc, sock = socket(AF_INET, SOCK_DGRAM, 0);
    rc = bind(sock, (struct sockaddr*)&server, addr_size);
    check_error(rc, "bind");
    char buf[PACKET_SIZE];
    char* tokens[4];
    struct sockaddr_in inc_addr;
    struct pollfd pfd[1];
    pfd[0].fd = sock;
    pfd[0].events = POLLIN;
    // Main loop
    while (1) {
        if (time(NULL) - last_check >= TIMEOUT_THRESHOLD)
            check_alive();
        poll(pfd, 1, 10); // 1 second timeout
        if (pfd[0].revents & POLLIN) {
            int n = recvfrom(sock, buf, PACKET_SIZE, 0, (struct sockaddr*)&inc_addr, &addr_size);
            buf[n] = '\0';
            tokens[0] = strtok(buf, " ");
            for (int i = 1; i < 4; i++)
                tokens[i] = strtok(NULL, " ");
            struct host* client = hosts_by_addr(known_hosts, inc_addr);
            if (tokens[2] != NULL && strcmp(tokens[2], "REG") == 0) {
                struct host* name = hosts_by_name(known_hosts, tokens[3]);
                if (strlen(tokens[3]) > 20) {
                    char packet[PACKET_SIZE];
                    sprintf(packet, "ACK %s NICK INVALID", tokens[1]);
                    send_packet(sock, packet, strlen(packet), 0,
                        (struct sockaddr*)&inc_addr, addr_size);
                } else if (client == NULL && name == NULL) {
                    add_host(strdup(tokens[3]), inc_addr, &known_hosts);
                    client = known_hosts;
                    client->block = 0;
                    ack(sock, client, atoi(tokens[1]));
                } else if (name != NULL) {
                    name->addr = inc_addr;
                    ack(sock, client, atoi(tokens[1]));
                } else {
                    free(client->nick);
                    client->nick = strdup(tokens[3]);
                    ack(sock, client, atoi(tokens[1]));
                }
            } else if (client != NULL) {
                if (strcmp(tokens[0], "PING") == 0) {
                    client->block = 0;
                } else if (strcmp(tokens[2], "LIST") == 0) {
                    list_clients(sock, client);
                } else if (strcmp(tokens[2], "LOOKUP") == 0) {
                    lookup_nick(sock, client, tokens[3]);
                } else {
                    char packet[PACKET_SIZE];
                    sprintf(packet, "ACK %s NOT FOUND", tokens[1]);
                    send_packet(sock, packet, strlen(packet), 0,
                        (struct sockaddr*)&inc_addr, addr_size);
                }
            }
        }
    }
    free_hosts(known_hosts);
    close(sock);
    return 0;
}
