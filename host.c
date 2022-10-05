#include "host.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct host* hosts_by_addr(struct host* known, struct sockaddr_in addr)
{
    for (struct host* i = known; i != NULL; i = i->next)
        if (i->addr.sin_addr.s_addr == addr.sin_addr.s_addr && i->addr.sin_port == addr.sin_port)
            return i;
    return NULL;
}

struct host* hosts_by_name(struct host* known, char* name)
{
    for (struct host* i = known; i != NULL; i = i->next)
        if (strcmp(i->nick, name) == 0)
            return i;
    return NULL;
}

void add_host(char* nick, struct sockaddr_in addr, struct host** known)
{
    struct host* new = malloc(sizeof(struct host));
    new->nick = nick, new->addr = addr;
    new->num = 0, new->block = 0;
    new->next = *known;
    new->q = malloc(sizeof(struct msg_q));
    new->q->head = NULL;
    new->q->tail = NULL;
    *known = new;
}

void new_msg(struct host* to, char* from, int num, char* msg_body)
{
    struct msg* new = malloc(sizeof(struct msg));
    struct msg_q* q = to->q;
    new->from = from, new->num = num;
    new->msg_body = msg_body, new->attempts = 0;
    new->last_sent = 0;
    if (q->head == NULL) {
        q->head = new;
        new->next = NULL;
    } else if (q->tail == NULL) {
        q->tail = new;
        new->next = NULL;
        q->head->next = new;
    } else {
        q->tail->next = new;
        new->next = NULL;
        q->tail = new;
    }
}

void pop(struct msg_q* q)
{
    struct msg* to_free = q->head;
    q->head = to_free->next;
    free(to_free->msg_body);
    free(to_free);
}

void free_hosts(struct host* h)
{
    while (h != NULL) {
        struct host* n = h->next;
        while (h->q->head != NULL)
            pop(h->q);
        free(h->q);
        free(h->nick);
        free(h);
        h = n;
    }
}
