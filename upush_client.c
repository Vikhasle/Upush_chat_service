#include "host.h"
#include "send_packet.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PACKET_SIZE 1450
int timeout;
time_t last_ping;
int sock;
struct host* known_hosts = NULL;

void check_error(int res, char* msg)
{
    if (res == -1) {
        perror(msg);
        free_hosts(known_hosts);
        exit(EXIT_FAILURE);
    }
}

// Send a packet on repeat until ack
int deliver_packet(struct host* recipient, char* packet, int max_attempts)
{
    struct pollfd pfd[1];
    pfd[0].fd = sock, pfd[0].events = POLLIN;
    int num_attempts = 1;
    do {
        send_packet(sock, packet, strlen(packet), 0,
            (struct sockaddr*)&recipient->addr, sizeof(recipient->addr));
    } while (poll(pfd, 1, timeout) == 0 && ++num_attempts <= max_attempts);
    if (num_attempts > max_attempts)
        return -1;
    return 0;
}

void ack(struct sockaddr_in addr, int num)
{
    char packet[PACKET_SIZE];
    sprintf(packet, "ACK %d OK", num);
    send_packet(sock, packet, strlen(packet), 0,
        (struct sockaddr*)&addr, sizeof(addr));
}

int connect_to_server(char* nick, struct host* server)
{
    char packet[PACKET_SIZE];
    sprintf(packet, "PKT 0 REG %s", nick);
    int connected = deliver_packet(server, packet, 5);
    check_error(connected, "Could not reach server. Check that the ip and port is correct");
    recvfrom(sock, packet, PACKET_SIZE, 0, NULL, NULL);
    if (strcmp(packet, "ACK 0 NICK INVALID") == 0)
        return -1; // Success
    return 1;
}

void ping(struct host* server)
{
    char* packet = "PING";
    send_packet(sock, packet, strlen(packet), 0,
        (struct sockaddr*)&server->addr, sizeof(server->addr));
}

int lookup_nick(struct host* server, char* nick, struct host* replace)
{
    char packet[PACKET_SIZE];
    sprintf(packet, "PKT %d LOOKUP %s", ++server->num, nick);
    int conn = deliver_packet(server, packet, 3);
    check_error(conn, "Could not reach server");
    int n = recvfrom(sock, packet, PACKET_SIZE, 0, NULL, NULL);
    packet[n] = '\0';
    strtok(packet, " "); // ACK
    strtok(NULL, " ");   // NUM
    char* token = strtok(NULL, " ");
    if (strcmp(token, "NOT") == 0)
        return -1;
    strtok(NULL, " "); // nickname
    token = strtok(NULL, " ");
    struct sockaddr_in addr;
    inet_pton(AF_INET, token, &addr.sin_addr);
    addr.sin_family = AF_INET;
    strtok(NULL, " "); // PORT
    token = strtok(NULL, " ");
    addr.sin_port = htons(atoi(token));
    if (replace == NULL)
        add_host(strdup(nick), addr, &known_hosts);
    else
        replace->addr = addr;
    return 0;
}

void send_message(struct msg* unsent_msg, struct host* recipient)
{
    char* packet = unsent_msg->msg_body;

    send_packet(sock, packet, strlen(packet), 0,
        (struct sockaddr*)&recipient->addr, sizeof(recipient->addr));
    unsent_msg->attempts++;
    unsent_msg->last_sent = time(NULL);
}

void resend_msg(struct host* server, time_t now)
{
    for (struct host* h = known_hosts; h != NULL; h = h->next) {
        struct msg* to_send = h->q->head;
        if (to_send != NULL && now - to_send->last_sent >= timeout / 1000) {
            if (to_send->attempts == 2) {
                char* nick = h->nick;
                int resp = lookup_nick(server, nick, h);
                if (resp == -1) {
                    h->num--;
                    fprintf(stderr, "NICK %s NOT REGISTERED\n", nick);
                    pop(h->q);
                } else
                    send_message(to_send, h);
            } else if (to_send->attempts == 5) {
                h->num--;
                fprintf(stderr, "NICK %s UNREACHABLE\n", h->nick);
                pop(h->q);
            } else {
                send_message(to_send, h);
            }
        }
    }
}

void send_list(struct host* server)
{
    char packet[PACKET_SIZE];
    sprintf(packet, "PKT %d LIST NICK", server->num++);
    int c = deliver_packet(server, packet, 5);
    check_error(c, "You have been disconnected from the server\n");
    int n = recvfrom(sock, packet, PACKET_SIZE, 0, NULL, NULL);
    packet[n] = '\0';
    strtok(packet, " ");             // ACK
    strtok(NULL, " ");               // NUM
    char* token = strtok(NULL, " "); // LIST
    if (strcmp(token, "NOT") == 0) {
        printf("You have been disconnected from the server\n");
        return;
    }
    printf("ALL USERS CURRENTLY ONLINE:\n--------------------------\n");
    token = strtok(NULL, " ");
    while (token != NULL) {
        printf("\t%s\n", token);
        token = strtok(NULL, " ");
    }
}

void print_help()
{
    printf("Usage:\n\t@someone to send them a msg\n\tlist to list all the users on the server\n\thelp to view this help\n\tquit or q to exit the client\n");
}

int main(int argc, char* argv[])
{
    if (argc < 6) {
        fprintf(stderr, "Usage: ./upush_client <nick> <address> <port> <timeout> <loss_probability>\n");
        return EXIT_FAILURE;
    }
    float prob = atof(argv[5]) / 100;
    timeout = atoi(argv[4]) * 1000;
    set_loss_probability(prob);
    struct host client, server;
    server.addr.sin_port = htons(atoi(argv[3]));
    server.addr.sin_family = AF_INET;
    if (!inet_pton(AF_INET, argv[2], &server.addr.sin_addr)) {
        fprintf(stderr, "Invalid ipaddress!\n");
        return EXIT_FAILURE;
    }
    client.addr.sin_port = 0;
    client.addr.sin_family = AF_INET;
    client.addr.sin_addr.s_addr = INADDR_ANY;
    client.nick = argv[1];
    server.num = 0;
    int rc;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    check_error(sock, "socket");
    rc = bind(sock, (struct sockaddr*)&client.addr, sizeof(struct sockaddr_in));
    check_error(rc, "bind");
    struct pollfd pfds[2];
    pfds[0].fd = 0, pfds[0].events = POLLIN;
    pfds[1].fd = sock, pfds[1].events = POLLIN;
    printf("Connecting to server...\n");
    rc = connect_to_server(argv[1], &server);
    check_error(rc, "Could not connect");
    last_ping = time(NULL);
    printf("Connection successful\n");
    printf("Welcome to Upush!\n");
    print_help();
    char buf[PACKET_SIZE];
    struct sockaddr_in inc_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    // Main loop
    while (1) {
        time_t now = time(NULL);
        resend_msg(&server, now);
        if (now - last_ping >= 10) {
            ping(&server);
            last_ping = now;
        }
        rc = poll(pfds, 2, 10);
        // Read from stdin
        if (pfds[0].revents & POLLIN) {
            fgets(buf, 1400, stdin);
            if (buf[strlen(buf) - 1] != '\n')
                for (char c = getchar(); c != '\n' && c != EOF; c = getchar())
                    ;
            buf[strlen(buf) - 1] = '\0';
            if (strcmp(buf, "QUIT") == 0 || strcmp(buf, "quit") == 0 || strcmp(buf, "q") == 0)
                break;
            else if (strcmp(buf, "help") == 0)
                print_help();
            else if (strcmp(buf, "list") == 0) {
                send_list(&server);
            } else if (buf[0] == '@') {
                char* recipient = strtok(buf + 1, " ");
                char* msg = recipient + strlen(recipient) + 1;
                struct host* rec_host = hosts_by_name(known_hosts, recipient);
                if (rec_host == NULL || rec_host->look) {
                    if (lookup_nick(&server, recipient, rec_host) == -1) {
                        fprintf(stderr, "NICK %s NOT REGISTERED\n", recipient);
                        continue;
                    }
                    if (rec_host == NULL)
                        rec_host = known_hosts;
                    rec_host->look = 0;
                }
                if (rec_host->block) {
                    fprintf(stderr, "NICK %s IS BLOCKED. UNBLOCK THEM TO SEND A MESSAGE\n", recipient);
                } else {
                    char* msg_body = malloc(PACKET_SIZE);
                    sprintf(msg_body, "PKT %d FROM %s TO %s MSG %s",
                        rec_host->num, client.nick, rec_host->nick, msg);
                    new_msg(rec_host, client.nick, rec_host->num++, msg_body);
                }
            } else {
                char* cmd = strtok(buf, " ");
                if (cmd == NULL)
                    continue;
                if (strcmp(cmd, "BLOCK") == 0) {
                    char* nick = strtok(NULL, " ");
                    struct host* to_block = hosts_by_name(known_hosts, nick);
                    if (to_block == NULL)
                        fprintf(stderr, "Unknown username\n");
                    else
                        to_block->block = 1;
                } else if (strcmp(cmd, "UNBLOCK") == 0) {
                    char* nick = strtok(NULL, " ");
                    struct host* to_unblock = hosts_by_name(known_hosts, nick);
                    if (to_unblock == NULL)
                        fprintf(stderr, "Unknown username\n");
                    else
                        to_unblock->block = 0;
                } else {
                    printf("Unknown command\n");
                }
            }
        }
        // Read from socket
        if (pfds[1].revents & POLLIN) {
            rc = recvfrom(sock, buf, PACKET_SIZE, 0, (struct sockaddr*)&inc_addr, &addr_len);
            char* tokens[8];
            buf[rc] = '\0';
            tokens[0] = strtok(buf, " ");
            for (int i = 1; i < 7; i++)
                tokens[i] = strtok(NULL, " ");
            struct host* sender = hosts_by_addr(known_hosts, inc_addr);
            if (sender == NULL) {
                add_host(strdup(tokens[3]), inc_addr, &known_hosts);
                sender = known_hosts;
                sender->look = 1; // Mark that we need to look up this host
            }
            if (sender->block == 0) {
                if (strcmp(tokens[0], "ACK") == 0) {
                    pop(sender->q);
                } else {
                    ack(sender->addr, atoi(tokens[1]));
                    if (atoi(tokens[1]) == sender->num) { // Check for dropped acks
                        printf("%s:\t%s\n", sender->nick, tokens[6] + strlen(tokens[6]) + 1);
                        sender->num++;
                    }
                }
            }
        }
    }
    free_hosts(known_hosts);
    close(sock);
    return 0;
}
