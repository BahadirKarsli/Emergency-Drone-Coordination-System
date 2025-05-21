#include "headers/survivor.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "headers/globals.h"
#include "headers/map.h"

Survivor *create_survivor(Coord *coord, char *info, struct tm *discovery_time) {
    Survivor *s = malloc(sizeof(Survivor));
    if (!s) return NULL;
    memset(s, 0, sizeof(Survivor));
    s->coord = *coord;
    memcpy(&s->discovery_time, discovery_time, sizeof(struct tm));
    strncpy(s->info, info, sizeof(s->info) - 1);
    s->info[sizeof(s->info) - 1] = '\0';
    s->status = 0;
    return s;
}

void *survivor_generator(void *args) {
    printf("Survivor generator thread running!\n");
    (void)args;
    time_t t;
    struct tm discovery_time;
    srand(time(NULL));

    while (1) {
        Coord coord = {.x = rand() % map.width, .y = rand() % map.height};
        char info[25];
        snprintf(info, sizeof(info), "SURV-%04d", rand() % 10000);
        time(&t);
        localtime_r(&t, &discovery_time);

        printf("Attempting to create survivor at (%d, %d)\n", coord.x, coord.y);
        Survivor *s = create_survivor(&coord, info, &discovery_time);
        if (!s) {
            printf("create_survivor failed!\n");
            continue;
        }
        printf("create_survivor succeeded: %p\n", (void*)s);

        printf("survivors->add pointer: %p\n", (void*)survivors->add);
        survivors->add(survivors, s);
        printf("After adding survivor to global list\n");
        printf("Added survivor to global list at (%d, %d): %s\n", coord.x, coord.y, info);

        pthread_mutex_lock(&map.cells[coord.y][coord.x].survivors->lock);
        map.cells[coord.y][coord.x].survivors->add(map.cells[coord.y][coord.x].survivors, s);
        pthread_mutex_unlock(&map.cells[coord.y][coord.x].survivors->lock);

        printf("New survivor at (%d,%d): %s\n", coord.x, coord.y, info);
        sleep(rand() % 3 + 2);
    }
    return NULL;
}

void survivor_cleanup(Survivor *s) {
    pthread_mutex_lock(&map.cells[s->coord.y][s->coord.x].survivors->lock);
    map.cells[s->coord.y][s->coord.x].survivors->removedata(map.cells[s->coord.y][s->coord.x].survivors, s);
    pthread_mutex_unlock(&map.cells[s->coord.y][s->coord.x].survivors->lock);
    free(s);
}