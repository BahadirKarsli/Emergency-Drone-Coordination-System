#ifndef LIST_H
#define LIST_H
#include <time.h>
#include <pthread.h>

typedef struct node {
    struct node *prev;
    struct node *next;
    char occupied;
    char data[];
} Node;

typedef struct list {
    Node *head;
    Node *tail;
    int number_of_elements;
    int capacity;
    int datasize;
    int nodesize;
    char *startaddress;
    char *endaddress;
    Node *lastprocessed;
    Node *free_list;
    pthread_mutex_t lock;
    Node *(*add)(struct list *list, void *data);
    int (*removedata)(struct list *list, void *data);
    int (*removenode)(struct list *list, Node *node);
    void *(*pop)(struct list *list, void *dest);
    void *(*peek)(struct list *list);
    void (*destroy)(struct list *list);
    void (*printlist)(struct list *list, void (*print)(void*));
    void (*printlistfromtail)(struct list *list, void (*print)(void*));
    struct list *self;
} List;

List *create_list(size_t datasize, int capacity);
int removenode(List *list, Node *node);
Node *add(List *list, void *data);
int removedata(List *list, void *data);
void *pop(List *list, void *dest);
void *peek(List *list);
void destroy(List *list);
void printlist(List *list, void (*print)(void*));
void printlistfromtail(List *list, void (*print)(void*));
#endif