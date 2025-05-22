#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include "headers/drone.h"
#include "headers/coord.h"

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 4096

void send_json(int sock, struct json_object *jobj);
struct json_object *receive_json(int sock);
void navigate_to_target(Drone *drone);

int main() {
    srand(time(NULL));
    Drone drone = {
        .id = rand() % 1000,
        .status = IDLE,
        .coord = {rand() % 40, rand() % 30},
        .target = {0, 0}
    };
    pthread_mutex_init(&drone.lock, NULL);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = inet_addr(SERVER_IP)
    };

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    drone.sock = sock;
    printf("Connected to server at %s:%d\n", SERVER_IP, PORT);

    // Set socket to non-blocking mode with a timeout
    struct timeval tv;
    tv.tv_sec = 1;  // 1 second timeout
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    char drone_id[10];
    snprintf(drone_id, sizeof(drone_id), "D%d", drone.id);
    struct json_object *handshake = json_object_new_object();
    json_object_object_add(handshake, "type", json_object_new_string("HANDSHAKE"));
    json_object_object_add(handshake, "drone_id", json_object_new_string(drone_id));
    struct json_object *capabilities = json_object_new_object();
    json_object_object_add(capabilities, "max_speed", json_object_new_int(30));
    json_object_object_add(capabilities, "battery_capacity", json_object_new_int(100));
    json_object_object_add(capabilities, "payload", json_object_new_string("medical"));
    json_object_object_add(handshake, "capabilities", capabilities);
    send_json(sock, handshake);
    printf("Sent HANDSHAKE: drone_id=%s\n", drone_id);
    json_object_put(handshake);

    struct json_object *ack = receive_json(sock);
    if (!ack || strcmp(json_object_get_string(json_object_object_get(ack, "type")), "HANDSHAKE_ACK") != 0) {
        fprintf(stderr, "Handshake failed\n");
        if (ack) json_object_put(ack);
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("Received HANDSHAKE_ACK\n");
    json_object_put(ack);

    while (1) {
        // Move the drone if it's on a mission
        pthread_mutex_lock(&drone.lock);
        if (drone.status == ON_MISSION) {
            navigate_to_target(&drone);
        }
        
        // Send status update
        struct json_object *status = json_object_new_object();
        json_object_object_add(status, "type", json_object_new_string("STATUS_UPDATE"));
        json_object_object_add(status, "drone_id", json_object_new_string(drone_id));
        json_object_object_add(status, "timestamp", json_object_new_int64(time(NULL)));
        struct json_object *loc = json_object_new_object();
        json_object_object_add(loc, "x", json_object_new_int(drone.coord.x));
        json_object_object_add(loc, "y", json_object_new_int(drone.coord.y));
        json_object_object_add(status, "location", loc);
        json_object_object_add(status, "status", json_object_new_string(drone.status == IDLE ? "idle" : "busy"));
        json_object_object_add(status, "battery", json_object_new_int(85));
        json_object_object_add(status, "speed", json_object_new_int(5));
        send_json(sock, status);
        printf("Sent STATUS_UPDATE: x=%d, y=%d, status=%s\n",
               drone.coord.x, drone.coord.y, drone.status == IDLE ? "idle" : "busy");
        json_object_put(status);
        pthread_mutex_unlock(&drone.lock);

        // Check for messages from server
        struct json_object *msg = receive_json(sock);
        if (!msg) {
            if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
                fprintf(stderr, "Server disconnected\n");
                break;
            }
        } else {
            const char *type = json_object_get_string(json_object_object_get(msg, "type"));
            printf("Received message: type=%s\n", type ? type : "NULL");
            
            if (strcmp(type, "ASSIGN_MISSION") == 0) {
                struct json_object *target = json_object_object_get(msg, "target");
                const char *mission_id = json_object_get_string(json_object_object_get(msg, "mission_id"));
                pthread_mutex_lock(&drone.lock);
                drone.target.x = json_object_get_int(json_object_object_get(target, "x"));
                drone.target.y = json_object_get_int(json_object_object_get(target, "y"));
                drone.status = ON_MISSION;
                strncpy(drone.mission_id, mission_id, sizeof(drone.mission_id) - 1);
                drone.mission_id[sizeof(drone.mission_id) - 1] = '\0';
                printf("Received ASSIGN_MISSION: mission_id=%s, target=(%d, %d)\n",
                       mission_id, drone.target.x, drone.target.y);
                pthread_mutex_unlock(&drone.lock);
            } else if (strcmp(type, "HEARTBEAT") == 0) {
                struct json_object *response = json_object_new_object();
                json_object_object_add(response, "type", json_object_new_string("HEARTBEAT_RESPONSE"));
                json_object_object_add(response, "drone_id", json_object_new_string(drone_id));
                json_object_object_add(response, "timestamp", json_object_new_int64(time(NULL)));
                send_json(sock, response);
                printf("Sent HEARTBEAT_RESPONSE\n");
                json_object_put(response);
            } else if (strcmp(type, "ERROR") == 0) {
                fprintf(stderr, "Error from server: %s\n",
                        json_object_get_string(json_object_object_get(msg, "message")));
            }
            
            json_object_put(msg);
        }
        
        usleep(500000); // Slow down to 0.5 seconds between updates
    }

    close(sock);
    pthread_mutex_destroy(&drone.lock);
    return 0;
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
    static char buffer[BUFFER_SIZE] = {0};
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
            if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Timeout, not a fatal error
                return NULL;
            }
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
        printf("Received %d bytes: %s\n", bytes, buffer + buf_pos - bytes);
    }
}

void navigate_to_target(Drone *drone) {
    // Only move if not already at target
    if (drone->coord.x != drone->target.x || drone->coord.y != drone->target.y) {
        // Move horizontally first, then vertically
        if (drone->coord.x < drone->target.x) {
            drone->coord.x++;
        } 
        else if (drone->coord.x > drone->target.x) {
            drone->coord.x--;
        }
        // Only move vertically if we're aligned horizontally
        else if (drone->coord.y < drone->target.y) {
            drone->coord.y++;
        } 
        else if (drone->coord.y > drone->target.y) {
            drone->coord.y--;
        }
    }

    // Now check if we have arrived - but don't send MISSION_COMPLETE yet
    // We'll let the next status update send our exact position first
    if (drone->coord.x == drone->target.x && drone->coord.y == drone->target.y) {
        printf("[DEBUG] Drone reached target coordinates (%d,%d)\n", 
               drone->target.x, drone->target.y);
        
        // Set status to IDLE immediately
        drone->status = IDLE;
        
        // Send MISSION_COMPLETE in the main thread after the status update
        // so the server knows our exact position first
        char drone_id[10];
        snprintf(drone_id, sizeof(drone_id), "D%d", drone->id);
        struct json_object *complete = json_object_new_object();
        json_object_object_add(complete, "type", json_object_new_string("MISSION_COMPLETE"));
        json_object_object_add(complete, "drone_id", json_object_new_string(drone_id));
        json_object_object_add(complete, "mission_id", json_object_new_string(drone->mission_id));
        json_object_object_add(complete, "timestamp", json_object_new_int64(time(NULL)));
        json_object_object_add(complete, "success", json_object_new_boolean(1));
        json_object_object_add(complete, "details", json_object_new_string("Reached survivor location"));
        
        // Send the message now - our STATUS_UPDATE will be sent first in the main loop
        send_json(drone->sock, complete);
        printf("Sent MISSION_COMPLETE: mission_id=%s\n", drone->mission_id);
        json_object_put(complete);
    }
}