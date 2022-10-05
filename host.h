#include <arpa/inet.h>

struct msg_q {
    struct msg* tail;
    struct msg* head;
};

struct msg {
    time_t last_sent;
    int num;
    int attempts;
    char* from;
    char* msg_body;
    struct msg* next;
};

struct host {
    struct sockaddr_in addr;
    char* nick;
    int num;
    int block;
    int look;
    struct host* next;
    struct msg_q* q;
};

struct host* hosts_by_addr(struct host* known, struct sockaddr_in addr);
struct host* hosts_by_name(struct host* known, char* name);
void add_host(char* nick, struct sockaddr_in addr, struct host** known);
void free_hosts(struct host* h);

void pop(struct msg_q* q);
void new_msg(struct host* to, char* from, int num, char* msg_body);
