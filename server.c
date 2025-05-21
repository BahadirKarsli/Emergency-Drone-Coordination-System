#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <json-c/json.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include "headers/globals.h"
#include "headers/ai.h"
#include "headers/map.h"
#include "headers/drone.h"
#include "headers/survivor.h"
#include "headers/list.h"
#include "headers/server.h"

// Forward declaration
Drone* find_drone_by_id(int id);

extern volatile sig_atomic_t global_shutdown_flag;

#define PORT 8080
#define MAX_DRONES 10
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 10

void *handle_drone(void *arg);
void send_json(int sock, struct json_object *jobj);
struct json_object *receive_json(int sock);
void process_handshake(int sock, struct json_object *jobj, const char* client_ip);
void process_status_update(int sock, struct json_object *jobj);
void process_mission_complete(int sock, struct json_object *jobj);
void process_heartbeat_response(int sock, struct json_object *jobj);

// Structure to pass arguments to handle_drone thread
typedef struct {
    int sock;
    char client_ip[INET_ADDRSTRLEN];
} handle_drone_args_t;

void *run_server_loop(void *args) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return NULL;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        return NULL;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return NULL;
    }
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        return NULL;
    }

    printf("Server listening on port %d\n", PORT);

    while (!global_shutdown_flag) {
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        tv.tv_sec = 1;  // 1 second timeout
        tv.tv_usec = 0;

        int activity = select(server_fd + 1, &readfds, NULL, NULL, &tv);
        if (activity < 0) {
            if (errno == EINTR) continue;
            perror("select error");
            break;
        }
        
        if (activity == 0) continue;  // Timeout, check shutdown flag
        
        if (FD_ISSET(server_fd, &readfds)) {
            int new_socket;
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            if ((new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0) {
                if (errno == EINTR) continue;
                perror("accept");
                continue;
            }

            char client_ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
            printf("Connection accepted from %s:%d on socket %d\n", 
                   client_ip_str, ntohs(client_addr.sin_port), new_socket);

            handle_drone_args_t *thread_args = malloc(sizeof(handle_drone_args_t));
            if (!thread_args) {
                perror("Failed to allocate memory for thread args");
                close(new_socket);
                continue;
            }
            thread_args->sock = new_socket;
            strncpy(thread_args->client_ip, client_ip_str, INET_ADDRSTRLEN);
            
            pthread_t thread_id;
            if (pthread_create(&thread_id, NULL, handle_drone, thread_args) != 0) {
                perror("pthread_create failed for handle_drone");
                free(thread_args);
                close(new_socket);
            }
            pthread_detach(thread_id);  // Automatically clean up thread when it exits
        }
    }

    printf("Server shutting down...\n");
    close(server_fd);
    return NULL;
}

void *handle_drone(void *arg) {
    handle_drone_args_t *thread_args = (handle_drone_args_t*)arg;
    int sock = thread_args->sock;
    char client_ip[INET_ADDRSTRLEN];
    strncpy(client_ip, thread_args->client_ip, INET_ADDRSTRLEN);
    free(thread_args);

    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = 5;  // 5 second timeout
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    Drone *current_drone = NULL;  // Keep track of the drone for cleanup

    while (!global_shutdown_flag) {
        struct json_object *jobj = receive_json(sock);
        if (!jobj) {
            printf("Client disconnected or error on socket %d\n", sock);
            if (current_drone) {
                pthread_mutex_lock(&current_drone->lock);
                current_drone->status = DISCONNECTED;
                pthread_mutex_unlock(&current_drone->lock);
            }
            close(sock);
            break;
        }

        const char *type = json_object_get_string(json_object_object_get(jobj, "type"));
        printf("Received message on sock %d: type=%s\n", sock, type ? type : "NULL");
        
        if (!type) {
            struct json_object *error = json_object_new_object();
            json_object_object_add(error, "type", json_object_new_string("ERROR"));
            json_object_object_add(error, "code", json_object_new_int(400));
            json_object_object_add(error, "message", json_object_new_string("Missing message type"));
            send_json(sock, error);
            json_object_put(error);
        } else if (strcmp(type, "HANDSHAKE") == 0) {
            process_handshake(sock, jobj, client_ip);
            // After handshake, get the drone object for this connection
            struct json_object *drone_id_obj;
            if (json_object_object_get_ex(jobj, "drone_id", &drone_id_obj)) {
                const char *drone_id_str = json_object_get_string(drone_id_obj);
                int id_val;
                if (sscanf(drone_id_str, "D%d", &id_val) == 1) {
                    current_drone = find_drone_by_id(id_val);
                }
            }
        } else if (strcmp(type, "STATUS_UPDATE") == 0) {
            process_status_update(sock, jobj);
        } else if (strcmp(type, "MISSION_COMPLETE") == 0) {
            process_mission_complete(sock, jobj);
        } else if (strcmp(type, "HEARTBEAT_RESPONSE") == 0) {
            process_heartbeat_response(sock, jobj);
        } else {
            struct json_object *error = json_object_new_object();
            json_object_object_add(error, "type", json_object_new_string("ERROR"));
            json_object_object_add(error, "code", json_object_new_int(400));
            json_object_object_add(error, "message", json_object_new_string("Invalid message type"));
            send_json(sock, error);
            json_object_put(error);
        }

        json_object_put(jobj);
    }

    // Cleanup on thread exit
    if (current_drone) {
        pthread_mutex_lock(&current_drone->lock);
        current_drone->status = DISCONNECTED;
        pthread_mutex_unlock(&current_drone->lock);
    }
    close(sock);
    return NULL;
}

void send_json(int sock, struct json_object *jobj) {
    const char *json_str = json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PLAIN);
    size_t len = strlen(json_str);
    char *msg = malloc(len + 2);
    snprintf(msg, len + 2, "%s\n", json_str);
    send(sock, msg, strlen(msg), 0);
    free(msg);
}

struct json_object *receive_json(int sock) {
    static char buffer[BUFFER_SIZE * 2];
    static size_t buf_pos = 0;

    while (1) {
        char *newline = strchr(buffer, '\n');
        if (newline) {
            *newline = '\0';
            struct json_object *jobj = json_tokener_parse(buffer);
            if (!jobj) {
                printf("Failed to parse JSON: %s\n", buffer);
            }
            size_t len = newline - buffer + 1;
            memmove(buffer, newline + 1, buf_pos - len);
            buf_pos -= len;
            return jobj;
        }

        int bytes = recv(sock, buffer + buf_pos, BUFFER_SIZE - buf_pos - 1, 0);
        if (bytes <= 0) {
            if (buf_pos > 0) {
                buffer[buf_pos] = '\0';
                struct json_object *jobj = json_tokener_parse(buffer);
                buf_pos = 0;
                return jobj;
            }
            return NULL;
        }
        buf_pos += bytes;
        buffer[buf_pos] = '\0';
        printf("Received %d bytes on sock %d: %s\n", bytes, sock, buffer + buf_pos - bytes);
    }
}

void process_handshake(int sock, struct json_object *jobj, const char* client_ip) {
    printf("[DEBUG Handshake] Processing HANDSHAKE from %s\n", client_ip);
    struct json_object *drone_id_obj, *capabilities_obj;
    if (!json_object_object_get_ex(jobj, "drone_id", &drone_id_obj) ||
        !json_object_object_get_ex(jobj, "capabilities", &capabilities_obj)) {
        fprintf(stderr, "HANDSHAKE from %s missing fields.\n", client_ip);
        return;
    }
    const char *drone_id_str = json_object_get_string(drone_id_obj);
    int new_drone_id_val;
    if (sscanf(drone_id_str, "D%d", &new_drone_id_val) != 1) {
        fprintf(stderr, "Invalid drone_id format in HANDSHAKE from %s: %s\n", client_ip, drone_id_str);
        return;
    }
    printf("[DEBUG Handshake] Parsed drone_id_str: %s to ID: %d\n", drone_id_str, new_drone_id_val);

    Drone *existing_drone = find_drone_by_id(new_drone_id_val);
    if (existing_drone) {
        printf("[DEBUG Handshake] Drone ID: %d is an existing drone. Socket: %d\n", new_drone_id_val, existing_drone->sock);
    } else {
        printf("[DEBUG Handshake] Drone ID: %d is a new drone. Creating.\n", new_drone_id_val);
        Drone *new_drone = (Drone *)malloc(sizeof(Drone));
        if (!new_drone) {
            perror("Failed to allocate memory for new drone");
            return;
        }
        printf("[DEBUG Handshake] Memory allocated for new drone ID: %d.\n", new_drone_id_val);
        new_drone->id = new_drone_id_val;
        new_drone->sock = sock;
        new_drone->status = IDLE;
        
        // Ensure drone spawns within valid map bounds
        new_drone->coord.x = rand() % map.width;
        new_drone->coord.y = rand() % map.height;
        
        // Validate coordinates
        if (new_drone->coord.x < 0 || new_drone->coord.x >= map.width || 
            new_drone->coord.y < 0 || new_drone->coord.y >= map.height) {
            printf("Generated invalid drone coordinates, fixing to valid range\n");
            new_drone->coord.x = new_drone->coord.x % map.width;
            new_drone->coord.y = new_drone->coord.y % map.height;
            if (new_drone->coord.x < 0) new_drone->coord.x = 0;
            if (new_drone->coord.y < 0) new_drone->coord.y = 0;
        }
        
        new_drone->target.x = 0;
        new_drone->target.y = 0;
        
        time_t now;
        time(&now);
        new_drone->last_update = *localtime(&now);

        if (pthread_mutex_init(&new_drone->lock, NULL) != 0) {
            perror("Failed to initialize drone mutex");
            free(new_drone);
            return;
        }
        printf("[DEBUG Handshake] Mutex initialized for new drone ID: %d.\n", new_drone_id_val);
        
        if (drones->add(drones, new_drone) == NULL) {
            fprintf(stderr, "Failed to add drone %s to list from %s.\n", drone_id_str, client_ip);
            pthread_mutex_destroy(&new_drone->lock);
            free(new_drone);
            return;
        }
        printf("[DEBUG Handshake] New drone ID: %d added to drones list.\n", new_drone_id_val);
        printf("Drone %s (ID: %d) from %s registered successfully. Initial pos: (%d, %d)\n", drone_id_str, new_drone->id, client_ip, new_drone->coord.x, new_drone->coord.y);
    }

    struct json_object *ack = json_object_new_object();
    json_object_object_add(ack, "type", json_object_new_string("HANDSHAKE_ACK"));
    json_object_object_add(ack, "session_id", json_object_new_string("S123"));
    struct json_object *config = json_object_new_object();
    json_object_object_add(config, "status_update_interval", json_object_new_int(5));
    json_object_object_add(config, "heartbeat_interval", json_object_new_int(10));
    json_object_object_add(ack, "config", config);
    send_json(sock, ack);
    json_object_put(ack);
}

void process_status_update(int sock, struct json_object *jobj) {
    const char *drone_id = json_object_get_string(json_object_object_get(jobj, "drone_id"));
    struct json_object *loc = json_object_object_get(jobj, "location");
    int new_x = json_object_get_int(json_object_object_get(loc, "x"));
    int new_y = json_object_get_int(json_object_object_get(loc, "y"));
    const char *status_str = json_object_get_string(json_object_object_get(jobj, "status"));
    DroneStatus new_status = strcmp(status_str, "idle") == 0 ? IDLE : ON_MISSION;
    
    // Extract drone ID number
    int drone_num;
    sscanf(drone_id + 1, "%d", &drone_num);
    
    pthread_mutex_lock(&drones->lock);
    Node *current = drones->head;
    Drone *drone = NULL;
    
    while (current != NULL) {
        Drone *d = (Drone *)current->data;
        if (d->id == drone_num) {
            drone = d;
            break;
        }
        current = current->next;
    }
    
    if (drone) {
        pthread_mutex_lock(&drone->lock);
        printf("[DEBUG] Drone %d position update: (%d,%d) -> (%d,%d)\n",
               drone->id, drone->coord.x, drone->coord.y, new_x, new_y);
        drone->coord.x = new_x;
        drone->coord.y = new_y;
        drone->status = new_status;
        printf("[DEBUG] Drone %s (ID: %d) final state: loc=(%d,%d), status=%s\n",
               drone_id, drone->id, drone->coord.x, drone->coord.y,
               drone->status == IDLE ? "idle" : "busy");
        pthread_mutex_unlock(&drone->lock);
    }
    pthread_mutex_unlock(&drones->lock);
    
    // Check if drone is at a survivor's position
    pthread_mutex_lock(&survivors->lock);
    Node* survivor_current = survivors->head;
    Node* prev = NULL;
    bool found_survivor = false;
    
    while (survivor_current != NULL && !found_survivor) {
        Survivor* s = (Survivor*)survivor_current->data;
        if (new_x == s->coord.x && new_y == s->coord.y) {
            printf("[DEBUG] Found survivor %s at (%d,%d) for removal.\n", 
                   s->info, s->coord.x, s->coord.y);
            
            // Create mission complete message
            struct json_object *complete_msg = json_object_new_object();
            json_object_object_add(complete_msg, "type", json_object_new_string("MISSION_COMPLETE"));
            json_object_object_add(complete_msg, "drone_id", json_object_new_string(drone_id));
            json_object_object_add(complete_msg, "mission_id", json_object_new_string(s->info));
            json_object_object_add(complete_msg, "success", json_object_new_boolean(true));
            json_object_object_add(complete_msg, "details", json_object_new_string("Delivered aid to survivor"));
            
            // Send mission complete message
            send_json(sock, complete_msg);
            json_object_put(complete_msg);
            
            // Remove the survivor from the list
            if (prev == NULL) {
                survivors->head = survivor_current->next;
            } else {
                prev->next = survivor_current->next;
            }
            
            // Add to helped survivors list
            pthread_mutex_lock(&helpedsurvivors->lock);
            add(helpedsurvivors, s);
            pthread_mutex_unlock(&helpedsurvivors->lock);
            
            // Don't free the survivor data since we moved it to helped survivors
            free(survivor_current);
            
            // Update drone status to idle
            if (drone) {
                pthread_mutex_lock(&drone->lock);
                drone->status = IDLE;
                pthread_mutex_unlock(&drone->lock);
            }
            
            printf("[DEBUG] Updated drone %d position to (%d,%d) after mission completion\n",
                   drone_num, new_x, new_y);
            found_survivor = true;
        } else {
            prev = survivor_current;
            survivor_current = survivor_current->next;
        }
    }
    pthread_mutex_unlock(&survivors->lock);
}

void process_mission_complete(int sock, struct json_object *jobj) {
    struct json_object *drone_id_obj, *mission_id_obj, *success_obj;
    if (!json_object_object_get_ex(jobj, "drone_id", &drone_id_obj) ||
        !json_object_object_get_ex(jobj, "mission_id", &mission_id_obj) ||
        !json_object_object_get_ex(jobj, "success", &success_obj)) {
        fprintf(stderr, "MISSION_COMPLETE missing fields.\n");
        return;
    }
    const char *drone_id_str = json_object_get_string(drone_id_obj);
    const char *mission_id = json_object_get_string(mission_id_obj);
    int success = json_object_get_boolean(success_obj);

    int id_val;
    if (sscanf(drone_id_str, "D%d", &id_val) != 1) {
        fprintf(stderr, "Invalid drone_id format in MISSION_COMPLETE: %s\n", drone_id_str);
        return;
    }
    
    Drone *drone = find_drone_by_id(id_val);
    if (!drone) {
        fprintf(stderr, "Drone %s not found for MISSION_COMPLETE.\n", drone_id_str);
        return;
    }

    pthread_mutex_lock(&drone->lock);
    if (success) {
        printf("Drone %s (ID: %d) completed mission %s successfully.\n", drone_id_str, drone->id, mission_id);

        pthread_mutex_lock(&survivors->lock);
        pthread_mutex_lock(&helpedsurvivors->lock);
        
        Node* current_survivor_node = survivors->head;
        Node* survivor_node_to_remove = NULL;
        Survivor* found_survivor = NULL;

        // First find the survivor without modifying anything
        while(current_survivor_node != NULL) {
            Survivor* s = (Survivor*)current_survivor_node->data;
            if (strcmp(s->info, mission_id) == 0) {
                survivor_node_to_remove = current_survivor_node;
                found_survivor = s;
                break;
            }
            current_survivor_node = current_survivor_node->next;
        }

        if (found_survivor) {
            // Update drone position to survivor's position
            drone->coord.x = found_survivor->coord.x;
            drone->coord.y = found_survivor->coord.y;
            printf("[DEBUG] Updated drone %d position to (%d,%d) after mission completion\n", 
                   drone->id, drone->coord.x, drone->coord.y);
        }

        if (found_survivor) {
            printf("[DEBUG] Found survivor %s at (%d,%d) for removal.\n", mission_id, found_survivor->coord.x, found_survivor->coord.y);
            // Create a copy for the helped survivors list
            Survivor* helped_survivor = (Survivor*)malloc(sizeof(Survivor));
            if (helped_survivor) {
                memcpy(helped_survivor, found_survivor, sizeof(Survivor));
                helped_survivor->status = 1;
                time_t now;
                time(&now);
                helped_survivor->helped_time = *localtime(&now);
                
                // Add to helped survivors list
                if (helpedsurvivors->add(helpedsurvivors, helped_survivor) != NULL) {
                    printf("Survivor %s moved to helped list.\n", mission_id);
                    
                    // Only remove from active list if successfully added to helped list
                    if (survivors->removenode(survivors, survivor_node_to_remove) == 0) {
                        printf("Survivor %s removed from active list.\n", mission_id);
                        
                        // Also remove from the map cell
                        pthread_mutex_lock(&map.cells[found_survivor->coord.y][found_survivor->coord.x].survivors->lock);
                        map.cells[found_survivor->coord.y][found_survivor->coord.x].survivors->removedata(
                            map.cells[found_survivor->coord.y][found_survivor->coord.x].survivors, 
                            found_survivor
                        );
                        pthread_mutex_unlock(&map.cells[found_survivor->coord.y][found_survivor->coord.x].survivors->lock);
                        printf("[DEBUG] Survivor %s removed from map cell (%d,%d).\n", mission_id, found_survivor->coord.x, found_survivor->coord.y);
                    } else {
                        fprintf(stderr, "Failed to remove survivor %s from active list.\n", mission_id);
                    }
                } else {
                    fprintf(stderr, "Failed to add survivor %s to helped list.\n", mission_id);
                    free(helped_survivor);
                }
            } else {
                perror("Failed to allocate memory for helped survivor");
            }
        } else {
            printf("[DEBUG] Survivor %s not found in survivors list.\n", mission_id);
        }

        pthread_mutex_unlock(&helpedsurvivors->lock);
        pthread_mutex_unlock(&survivors->lock);

        // Immediately spawn a new survivor
        printf("[DEBUG] Spawning a new survivor after mission complete.\n");
        pthread_t temp_thread;
        pthread_create(&temp_thread, NULL, survivor_generator, NULL);
        pthread_detach(temp_thread);
    }
    
    // Always set drone back to IDLE, whether mission succeeded or failed
    drone->status = IDLE;
    printf("[DEBUG] Drone %d set to IDLE at (%d, %d).\n", drone->id, drone->coord.x, drone->coord.y);
    if (!success) {
        printf("Drone %s (ID: %d) failed mission %s.\n", drone_id_str, drone->id, mission_id);
    }
    
    pthread_mutex_unlock(&drone->lock);
}

void process_heartbeat_response(int sock, struct json_object *jobj) {
    struct json_object *drone_id_obj;
    if (!json_object_object_get_ex(jobj, "drone_id", &drone_id_obj)) {
        fprintf(stderr, "HEARTBEAT_RESPONSE missing drone_id.\n");
        return;
    }
    const char *drone_id_str = json_object_get_string(drone_id_obj);
    printf("Received HEARTBEAT_RESPONSE from %s\n", drone_id_str);

    int id_val;
    if (sscanf(drone_id_str, "D%d", &id_val) != 1) return;
    
    Drone *drone = find_drone_by_id(id_val);
    if (drone) {
        pthread_mutex_lock(&drone->lock);
        time_t now;
        time(&now);
        drone->last_update = *localtime(&now);
        pthread_mutex_unlock(&drone->lock);
    }
}

Drone* find_drone_by_id(int id) {
    pthread_mutex_lock(&drones->lock);
    Node *current = drones->head;
    Drone *found_drone = NULL;
    while (current != NULL) {
        Drone *d = (Drone *)current->data;
        if (d->id == id) {
            found_drone = d;
            break;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&drones->lock);
    return found_drone;
}