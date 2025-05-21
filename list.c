/**
 * @file list.c
 * @author adaskin
 * @brief A simple doubly linked list stored in an array (contiguous memory).
 * @version 0.2
 * @date 2025-05-15
 * @copyright Copyright (c) 2024-2025
 */
#include "headers/list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

List *create_list(size_t datasize, int capacity) {
    List *list = malloc(sizeof(List));
    if (!list) return NULL;
    memset(list, 0, sizeof(List));

    pthread_mutex_init(&list->lock, NULL);
    list->datasize = datasize;
    list->nodesize = sizeof(Node) + datasize;
    list->startaddress = malloc(list->nodesize * capacity);
    if (!list->startaddress) {
        pthread_mutex_destroy(&list->lock);
        free(list);
        return NULL;
    }
    list->endaddress = list->startaddress + (list->nodesize * capacity);
    memset(list->startaddress, 0, list->nodesize * capacity);
    list->lastprocessed = (Node *)list->startaddress;
    list->number_of_elements = 0;
    list->capacity = capacity;
    list->free_list = NULL;

    list->self = list;
    list->add = add;
    list->removedata = removedata;
    list->removenode = removenode;
    list->pop = pop;
    list->peek = peek;
    list->destroy = destroy;
    list->printlist = printlist;
    list->printlistfromtail = printlistfromtail;
    return list;
}

static Node *find_memcell_fornode(List *list) {
    Node *node = NULL;
    if (list->free_list) {
        node = list->free_list;
        list->free_list = node->next;
        return node;
    }

    Node *temp = list->lastprocessed;
    while ((char *)temp < list->endaddress) {
        if (temp->occupied == 0) {
            node = temp;
            break;
        }
        temp = (Node *)((char *)temp + list->nodesize);
    }

    if (node == NULL) {
        temp = (Node *)list->startaddress;
        while (temp < list->lastprocessed) {
            if (temp->occupied == 0) {
                node = temp;
                break;
            }
            temp = (Node *)((char *)temp + list->nodesize);
        }
    }
    return node;
}

Node *add(List *list, void *data) {
    printf("In add(): list=%p, data=%p\n", (void*)list, data);
    fflush(stdout);
    pthread_mutex_lock(&list->lock);
    printf("In add(): after locking list->lock\n");
    fflush(stdout);
    if (list->number_of_elements >= list->capacity) {
        perror("list is full!");
        pthread_mutex_unlock(&list->lock);
        return NULL;
    }

    Node *node = find_memcell_fornode(list);
    printf("In add(): after find_memcell_fornode, node=%p\n", (void*)node);
    fflush(stdout);
    if (node != NULL) {
        node->occupied = 1;
        memcpy(node->data, data, list->datasize);
        printf("In add(): after memcpy\n");
        fflush(stdout);
        if (list->head != NULL) {
            Node *oldhead = list->head;
            oldhead->prev = node;
            node->prev = NULL;
            node->next = oldhead;
        }
        list->head = node;
        list->lastprocessed = node;
        list->number_of_elements += 1;
        if (list->tail == NULL) {
            list->tail = list->head;
        }
    } else {
        perror("list is full!");
    }
    pthread_mutex_unlock(&list->lock);
    printf("In add(): after unlocking list->lock\n");
    fflush(stdout);
    return node;
}

int removedata(List *list, void *data) {
    pthread_mutex_lock(&list->lock);
    Node *temp = list->head;
    while (temp != NULL && memcmp(temp->data, data, list->datasize) != 0) {
        temp = temp->next;
    }
    if (temp != NULL) {
        Node *prevnode = temp->prev;
        Node *nextnode = temp->next;
        if (prevnode != NULL) {
            prevnode->next = nextnode;
        }
        if (nextnode != NULL) {
            nextnode->prev = prevnode;
        }
        temp->next = list->free_list;
        temp->prev = NULL;
        temp->occupied = 0;
        list->free_list = temp;
        list->number_of_elements--;
        if (temp == list->tail) {
            list->tail = prevnode;
        }
        if (temp == list->head) {
            list->head = nextnode;
        }
        list->lastprocessed = temp;
        pthread_mutex_unlock(&list->lock);
        return 0;
    }
    pthread_mutex_unlock(&list->lock);
    return 1;
}

void *pop(List *list, void *dest) {
    pthread_mutex_lock(&list->lock);
    if (list->head != NULL) {
        Node *node = list->head;
        if (removenode(list, node) == 0) {
            memcpy(dest, node->data, list->datasize);
            pthread_mutex_unlock(&list->lock);
            return dest;
        }
    }
    pthread_mutex_unlock(&list->lock);
    return NULL;
}

void *peek(List *list) {
    pthread_mutex_lock(&list->lock);
    void *data = (list->head != NULL) ? list->head->data : NULL;
    pthread_mutex_unlock(&list->lock);
    return data;
}

int removenode(List *list, Node *node) {
    pthread_mutex_lock(&list->lock);
    if (node != NULL) {
        Node *prevnode = node->prev;
        Node *nextnode = node->next;
        if (prevnode != NULL) {
            prevnode->next = nextnode;
        }
        if (nextnode != NULL) {
            nextnode->prev = prevnode;
        }
        node->next = list->free_list;
        node->prev = NULL;
        node->occupied = 0;
        list->free_list = node;
        list->number_of_elements--;
        if (node == list->tail) {
            list->tail = prevnode;
        }
        if (node == list->head) {
            list->head = nextnode;
        }
        list->lastprocessed = node;
        pthread_mutex_unlock(&list->lock);
        return 0;
    }
    pthread_mutex_unlock(&list->lock);
    return 1;
}

void destroy(List *list) {
    pthread_mutex_lock(&list->lock);
    free(list->startaddress);
    list->startaddress = NULL;
    list->endaddress = NULL;
    list->head = NULL;
    list->tail = NULL;
    list->lastprocessed = NULL;
    list->free_list = NULL;
    list->number_of_elements = 0;
    pthread_mutex_unlock(&list->lock);
    pthread_mutex_destroy(&list->lock);
    free(list);
}

void printlist(List *list, void (*print)(void *)) {
    pthread_mutex_lock(&list->lock);
    Node *temp = list->head;
    while (temp != NULL) {
        print(temp->data);
        temp = temp->next;
    }
    pthread_mutex_unlock(&list->lock);
}

void printlistfromtail(List *list, void (*print)(void *)) {
    pthread_mutex_lock(&list->lock);
    Node *temp = list->tail;
    while (temp != NULL) {
        print(temp->data);
        temp = temp->prev;
    }
    pthread_mutex_unlock(&list->lock);
}