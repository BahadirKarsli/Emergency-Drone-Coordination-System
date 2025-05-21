#include "headers/drone.h"
#include "headers/globals.h"
#include "headers/map.h"
#include "headers/survivor.h"
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
            printf("[DEBUG] Drone %d at (%d,%d), target (%d,%d)\n", d->id, d->coord.x, d->coord.y, d->target.x, d->target.y);
            if (d->coord.x < d->target.x) d->coord.x++;
            else if (d->coord.x > d->target.x) d->coord.x--;
            else if (d->coord.y < d->target.y) d->coord.y++;
            else if (d->coord.y > d->target.y) d->coord.y--;
            
            // Check if drone has reached its target
            if (d->coord.x == d->target.x && d->coord.y == d->target.y) {
                printf("[DEBUG] Drone %d reached target (%d,%d)\n", d->id, d->coord.x, d->coord.y);
                Survivor *found_survivor = NULL;
                pthread_mutex_lock(&map.cells[d->coord.y][d->coord.x].survivors->lock);
                Node *current = map.cells[d->coord.y][d->coord.x].survivors->head;
                while (current != NULL) {
                    Survivor *s = (Survivor *)current->data;
                    printf("[DEBUG] Checking survivor at (%d,%d)\n", s->coord.x, s->coord.y);
                    if (s && s->coord.x == d->coord.x && s->coord.y == d->coord.y) {
                        found_survivor = s;
                        break;
                    }
                    current = current->next;
                }
                pthread_mutex_unlock(&map.cells[d->coord.y][d->coord.x].survivors->lock);
                if (found_survivor) {
                    printf("[DEBUG] Drone %d found survivor to rescue at (%d,%d)\n", d->id, d->coord.x, d->coord.y);
                    pthread_mutex_lock(&map.cells[d->coord.y][d->coord.x].survivors->lock);
                    map.cells[d->coord.y][d->coord.x].survivors->removedata(
                        map.cells[d->coord.y][d->coord.x].survivors, found_survivor);
                    pthread_mutex_unlock(&map.cells[d->coord.y][d->coord.x].survivors->lock);

                    pthread_mutex_lock(&survivors->lock);
                    survivors->removedata(survivors, found_survivor);
                    pthread_mutex_unlock(&survivors->lock);

                    pthread_mutex_lock(&helpedsurvivors->lock);
                    helpedsurvivors->add(helpedsurvivors, found_survivor);
                    pthread_mutex_unlock(&helpedsurvivors->lock);

                    printf("Drone %d: Rescued survivor at (%d, %d)\n", d->id, d->coord.x, d->coord.y);
                } else {
                    printf("[DEBUG] Drone %d did NOT find a survivor to rescue at (%d,%d)\n", d->id, d->coord.x, d->coord.y);
                }
                d->status = IDLE;
                printf("Drone %d: Mission completed!\n", d->id);
            }
        }
        pthread_mutex_unlock(&d->lock);
        usleep(1000); // Sleep for 1ms (even faster movement)
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