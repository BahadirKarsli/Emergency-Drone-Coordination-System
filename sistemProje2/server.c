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
#include "headers/globals.h"
#include "headers/ai.h"
#include "headers/map.h"
#include "headers/drone.h"
#include "headers/survivor.h"
#include "headers/list.h"
#include "headers/server.h"

// Forward declaration
Drone* find_drone_by_id(int id);

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

void *run_server_loop(void *args) { // Renamed from main
    int server_fd; // Declaration for server_fd
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    pthread_t thread_id[MAX_CLIENTS];
    int client_count = 0;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    while (1) {
        int new_socket;
        struct sockaddr_in client_addr; // Declaration for client_addr
        socklen_t client_len = sizeof(client_addr);

        if ((new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0) {
            if (errno == EINTR) continue; // Interrupted by signal, try again
            perror("accept");
            continue; // Or handle error more gracefully
        }

        if (client_count < MAX_CLIENTS) {
            char client_ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
            printf("Connection accepted from %s:%d on socket %d\n", client_ip_str, ntohs(client_addr.sin_port), new_socket);

            handle_drone_args_t *thread_args = malloc(sizeof(handle_drone_args_t));
            if (!thread_args) {
                perror("Failed to allocate memory for thread args");
                close(new_socket);
                continue;
            }
            thread_args->sock = new_socket;
            strncpy(thread_args->client_ip, client_ip_str, INET_ADDRSTRLEN);
            
            if (pthread_create(&thread_id[client_count], NULL, handle_drone, thread_args) != 0) {
                perror("pthread_create failed for handle_drone");
                free(thread_args);
                close(new_socket);
            } else {
                client_count++; // Only increment if thread creation was successful
            }
        } else {
            printf("Max clients reached. Connection rejected.\n");
            close(new_socket);
        }
    }

    // Cleanup (though this part of the loop might not be reached in typical server)
    close(server_fd);
    // freemap, destroy lists etc. should be handled by the main controller on shutdown
    return NULL;
}

void *handle_drone(void *arg) {
    handle_drone_args_t *thread_args = (handle_drone_args_t*)arg;
    int sock = thread_args->sock;
    char client_ip[INET_ADDRSTRLEN];
    strncpy(client_ip, thread_args->client_ip, INET_ADDRSTRLEN);
    free(thread_args);

    while (1) {
        struct json_object *jobj = receive_json(sock);
        if (!jobj) {
            printf("No data received or client disconnected on sock %d\n", sock);
            pthread_mutex_lock(&drones->lock);
            Node *node = drones->head;
            while (node != NULL) {
                Drone *d = (Drone *)node->data;
                if (d->sock == sock) {
                    pthread_mutex_lock(&d->lock);
                    d->status = DISCONNECTED;
                    pthread_mutex_unlock(&d->lock);
                    break;
                }
                node = node->next;
            }
            pthread_mutex_unlock(&drones->lock);
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
        new_drone->coord.x = rand() % map.width;
        new_drone->coord.y = rand() % map.height;
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
    struct json_object *drone_id_obj, *loc_obj, *status_obj, *timestamp_obj;
    struct json_object *battery_obj, *speed_obj;

    if (!json_object_object_get_ex(jobj, "drone_id", &drone_id_obj) ||
        !json_object_object_get_ex(jobj, "location", &loc_obj) ||
        !json_object_object_get_ex(jobj, "status", &status_obj) ||
        !json_object_object_get_ex(jobj, "timestamp", &timestamp_obj) ||
        !json_object_object_get_ex(jobj, "battery", &battery_obj) ||
        !json_object_object_get_ex(jobj, "speed", &speed_obj) ) {
        fprintf(stderr, "STATUS_UPDATE missing fields.\n");
        return;
    }
    const char *drone_id_str = json_object_get_string(drone_id_obj);
    int id_val;
     if (sscanf(drone_id_str, "D%d", &id_val) != 1) {
        fprintf(stderr, "Invalid drone_id format in STATUS_UPDATE: %s\n", drone_id_str);
        return;
    }

    Drone *drone = find_drone_by_id(id_val);
    if (!drone) {
        fprintf(stderr, "Drone %s not found for STATUS_UPDATE.\n", drone_id_str);
        return;
    }

    pthread_mutex_lock(&drone->lock);
    struct json_object *x_obj, *y_obj;
    if (json_object_object_get_ex(loc_obj, "x", &x_obj) && json_object_object_get_ex(loc_obj, "y", &y_obj)) {
        drone->coord.x = json_object_get_int(x_obj);
        drone->coord.y = json_object_get_int(y_obj);
    }

    const char *status_str = json_object_get_string(status_obj);
    if (strcmp(status_str, "idle") == 0) {
        drone->status = IDLE;
    } else if (strcmp(status_str, "busy") == 0) {
        drone->status = ON_MISSION;
    } else if (strcmp(status_str, "charging") == 0) {
        // drone->status = CHARGING; // If you add this enum state
    }
    
    time_t now;
    time(&now);
    drone->last_update = *localtime(&now);

    printf("Drone %s (ID: %d) updated: loc=(%d,%d), status=%s\n", drone_id_str, drone->id, drone->coord.x, drone->coord.y, status_str);
    pthread_mutex_unlock(&drone->lock);
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
        drone->status = IDLE;
        printf("Drone %s (ID: %d) completed mission %s successfully.\n", drone_id_str, drone->id, mission_id);

        pthread_mutex_lock(&survivors->lock);
        pthread_mutex_lock(&helpedsurvivors->lock);
        Node* current_survivor_node = survivors->head;
        Survivor* found_survivor_data = NULL;
        Node* survivor_node_to_remove = NULL;

        while(current_survivor_node != NULL) {
            Survivor* s = (Survivor*)current_survivor_node->data;
            if (strcmp(s->info, mission_id) == 0) {
                found_survivor_data = (Survivor*)malloc(sizeof(Survivor));
                if (found_survivor_data) {
                    memcpy(found_survivor_data, s, sizeof(Survivor));
                    found_survivor_data->status = 1;
                    time_t now;
                    time(&now);
                    found_survivor_data->helped_time = *localtime(&now);
                    
                    if (helpedsurvivors->add(helpedsurvivors, found_survivor_data) != NULL) {
                        printf("Survivor %s moved to helped list.\n", mission_id);
                    } else {
                        fprintf(stderr, "Failed to add survivor %s to helped list.\n", mission_id);
                        free(found_survivor_data);
                    }
                } else {
                    perror("Malloc failed for survivor copy");
                }
                survivor_node_to_remove = current_survivor_node;
                break;
            }
            current_survivor_node = current_survivor_node->next;
        }

        if (survivor_node_to_remove) {
            Survivor* s_to_free = (Survivor*)survivor_node_to_remove->data;
            if (survivors->removenode(survivors, survivor_node_to_remove) == 0) {
                printf("Survivor %s removed from active list.\n", mission_id);
            } else {
                fprintf(stderr, "Failed to remove survivor %s from active list.\n", mission_id);
            }
        }
        pthread_mutex_unlock(&helpedsurvivors->lock);
        pthread_mutex_unlock(&survivors->lock);

    } else {
        drone->status = IDLE;
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