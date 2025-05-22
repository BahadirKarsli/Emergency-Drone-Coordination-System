#ifndef AI_H
#define AI_H
#include "drone.h"
#include "survivor.h"
void *ai_controller(void *args);
void assign_mission(Drone *drone, Coord target, const char *mission_id);
Drone *find_closest_idle_drone(Coord target);
#endif