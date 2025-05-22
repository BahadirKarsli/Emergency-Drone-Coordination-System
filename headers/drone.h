#ifndef DRONE_H
#define DRONE_H
#include "coord.h"
#include <time.h>
#include <pthread.h>
#include "list.h"

typedef enum {
    IDLE,
    ON_MISSION,
    DISCONNECTED
} DroneStatus;

typedef struct drone {
    int id;
    pthread_t thread_id; // Used in Phase 1
    int status;
    Coord coord;
    Coord target;
    struct tm last_update;
    pthread_mutex_t lock;
    int sock; // Socket descriptor for client communication
    char mission_id[32]; // Store current mission ID
} Drone;

extern List *drones;
extern Drone *drone_fleet;
extern int num_drones;
void initialize_drones();
void *drone_behavior(void *arg);
void cleanup_drones();
#endif