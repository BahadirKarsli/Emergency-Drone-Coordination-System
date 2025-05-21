#include "headers/ai.h"
#include <limits.h>
#include <stdio.h>
#include <string.h> 
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <json-c/json.h>
#include <signal.h>

extern volatile sig_atomic_t global_shutdown_flag;

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
    
    // Add newline to ensure proper message framing
    const char *json_str = json_object_to_json_string_ext(mission, JSON_C_TO_STRING_PLAIN);
    char *msg = malloc(strlen(json_str) + 2);  // +2 for newline and null terminator
    if (msg) {
        sprintf(msg, "%s\n", json_str);
        send(drone->sock, msg, strlen(msg), 0);
        free(msg);
    }
    
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
    printf("AI controller thread started.\n");
    while (!global_shutdown_flag) {
        pthread_mutex_lock(&survivors->lock);
        Node *node = survivors->head;
        
        if (node && node->data) {
            Survivor *s = (Survivor *)node->data;
            Coord target = s->coord;
            char mission_id[25];
            strncpy(mission_id, s->info, sizeof(mission_id) - 1);
            mission_id[sizeof(mission_id) - 1] = '\0';
            
            Drone *closest = find_closest_idle_drone(target);
            if (closest) {
                printf("Drone %d assigned to survivor %s at (%d, %d)\n",
                       closest->id, mission_id, target.x, target.y);
                       
                assign_mission(closest, target, mission_id);
            }
        }
        pthread_mutex_unlock(&survivors->lock);
        sleep(1);  // Prevent busy-waiting
    }
    printf("AI controller thread exiting.\n");
    return NULL;
}