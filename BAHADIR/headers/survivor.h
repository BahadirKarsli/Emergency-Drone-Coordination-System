#ifndef SURVIVOR_H
#define SURVIVOR_H
#include "coord.h"
#include <time.h>
#include "list.h"

typedef struct survivor {
    int status;
    Coord coord;
    struct tm discovery_time;
    struct tm helped_time;
    char info[25];
} Survivor;

extern List *survivors;
extern List *helpedsurvivors;
Survivor *create_survivor(Coord *coord, char *info, struct tm *discovery_time);
void *survivor_generator(void *args);
void survivor_cleanup(Survivor *s);
#endif