#include "headers/ai.h"
#include <limits.h>
#include <stdio.h>
#include <string.h> 
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <json-c/json.h>

void assign_mission(Drone *drone, Coord target, const char *mission_id) {
    pthread_mutex_lock(&drone->lock);
    drone->target = target;
    drone->status = ON_MISSION;
    struct json_object *mission = json_object_new_object();
    json_object_object_add(mission, "type", json_object_new_string("ASSIGN_MISSION"));
    json_object_object_add(mission, "mission_id", json_object_new_string(mission_id));
    json_object_object_add(mission, "priority", json_object_new_string("high"));
    struct json_object *target_obj = json_object_new_object();
    json_object_object_add(target_obj, "x", json_object_new_int(target.x));
    json_object_object_add(target_obj, "y", json_object_new_int(target.y));
    json_object_object_add(mission, "target", target_obj);
    json_object_object_add(mission, "expiry", json_object_new_int64(time(NULL) + 3600));
    json_object_object_add(mission, "checksum", json_object_new_string("a1b2c3"));
    const char *json_str = json_object_to_json_string_ext(mission, JSON_C_TO_STRING_PLAIN);
    send(drone->sock, json_str, strlen(json_str), 0);
    json_object_put(mission);
    pthread_mutex_unlock(&drone->lock);
}

Drone *find_closest_idle_drone(Coord target) {
    Drone *closest = NULL;
    int min_distance = INT_MAX;
    pthread_mutex_lock(&drones->lock);
    Node *node = drones->head;
    while (node != NULL) {
        Drone *d = (Drone *)node->data;
        pthread_mutex_lock(&d->lock);
        if (d->status == IDLE) {
            int dist = abs(d->coord.x - target.x) + abs(d->coord.y - target.y);
            if (dist < min_distance) {
                min_distance = dist;
                closest = d;
            }
        }
        pthread_mutex_unlock(&d->lock);
        node = node->next;
    }
    pthread_mutex_unlock(&drones->lock);
    return closest;
}

void *ai_controller(void *arg) {
    while (1) {
        pthread_mutex_lock(&survivors->lock);
        Node *node = survivors->head;
        Survivor *s = node ? (Survivor *)node->data : NULL;
        if (s) {
            Drone *closest = find_closest_idle_drone(s->coord);
            if (closest) {
                assign_mission(closest, s->coord, s->info);
                printf("Drone %d assigned to survivor %s at (%d, %d)\n",
                       closest->id, s->info, s->coord.x, s->coord.y);
                survivors->removenode(survivors, node);
            }
        }
        pthread_mutex_unlock(&survivors->lock);
        sleep(1);
    }
    return NULL;
}