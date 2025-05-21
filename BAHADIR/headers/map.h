#ifndef MAP_H
#define MAP_H
#include "survivor.h"
#include "list.h"
#include "coord.h"

typedef struct mapcell {
    Coord coord;
    List *survivors;
} MapCell;

typedef struct map {
    int height, width;
    MapCell **cells;
} Map;

extern Map map;
void init_map(int height, int width);
void freemap();
#endif