#include "headers/drone.h"
#include "headers/globals.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

Drone *drone_fleet = NULL;
int num_drones = 10;

void initialize_drones() {
    drone_fleet = malloc(sizeof(Drone) * num_drones);
    srand(time(NULL));

    for (int i = 0; i < num_drones; i++) {
        drone_fleet[i].id = i;
        drone_fleet[i].status = IDLE;
        drone_fleet[i].coord = (Coord){rand() % map.width, rand() % map.height};
        drone_fleet[i].target = drone_fleet[i].coord;
        pthread_mutex_init(&drone_fleet[i].lock, NULL);

        pthread_mutex_lock(&drones->lock);
        drones->add(drones, &drone_fleet[i]);
        pthread_mutex_unlock(&drones->lock);

        pthread_create(&drone_fleet[i].thread_id, NULL, drone_behavior, &drone_fleet[i]);
    }
}

void *drone_behavior(void *arg) {
    Drone *d = (Drone*)arg;
    while (1) {
        pthread_mutex_lock(&d->lock);
        if (d->status == ON_MISSION) {
            if (d->coord.x < d->target.x) d->coord.x++;
            else if (d->coord.x > d->target.x) d->coord.x--;
            if (d->coord.y < d->target.y) d->coord.y++;
            else if (d->coord.y > d->target.y) d->coord.y--;
            if (d->coord.x == d->target.x && d->coord.y == d->target.y) {
                d->status = IDLE;
                printf("Drone %d: Mission completed!\n", d->id);
            }
        }
        pthread_mutex_unlock(&d->lock);
        sleep(1);
    }
    return NULL;
}

void cleanup_drones() {
    for (int i = 0; i < num_drones; i++) {
        pthread_cancel(drone_fleet[i].thread_id);
        pthread_mutex_destroy(&drone_fleet[i].lock);
    }
    free(drone_fleet);
}