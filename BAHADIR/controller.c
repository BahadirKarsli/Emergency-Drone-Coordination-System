#include "headers/globals.h"
#include "headers/map.h"
#include "headers/drone.h"
#include "headers/survivor.h"
#include "headers/ai.h"
#include "headers/view.h"
#include "headers/server.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

// Global flag for graceful shutdown
volatile sig_atomic_t global_shutdown_flag = 0;

// Thread IDs for cleanup
static pthread_t survivor_thread_id;
static pthread_t ai_thread_id;
static pthread_t server_thread_id;

// Signal handler
void handle_signal(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nReceived shutdown signal. Initiating graceful shutdown...\n");
        global_shutdown_flag = 1;
    }
}

// Cleanup function
void cleanup_resources() {
    printf("Cleaning up resources...\n");
    printf("Waiting for threads to finish...\n");
    
    // Wait for threads to finish
    if (server_thread_id) pthread_join(server_thread_id, NULL);
    if (survivor_thread_id) pthread_join(survivor_thread_id, NULL);
    if (ai_thread_id) pthread_join(ai_thread_id, NULL);
    
    // Cleanup SDL
    quit_all();
    
    // Cleanup lists
    if (survivors) {
        pthread_mutex_lock(&survivors->lock);
        Node *node = survivors->head;
        while (node) {
            free(node->data);
            Node *next = node->next;
            free(node);
            node = next;
        }
        pthread_mutex_unlock(&survivors->lock);
        pthread_mutex_destroy(&survivors->lock);
        free(survivors);
    }
    
    if (helpedsurvivors) {
        pthread_mutex_lock(&helpedsurvivors->lock);
        Node *node = helpedsurvivors->head;
        while (node) {
            free(node->data);
            Node *next = node->next;
            free(node);
            node = next;
        }
        pthread_mutex_unlock(&helpedsurvivors->lock);
        pthread_mutex_destroy(&helpedsurvivors->lock);
        free(helpedsurvivors);
    }
    
    if (drones) {
        pthread_mutex_lock(&drones->lock);
        Node *node = drones->head;
        while (node) {
            Drone *d = (Drone *)node->data;
            pthread_mutex_destroy(&d->lock);
            free(d);
            Node *next = node->next;
            free(node);
            node = next;
        }
        pthread_mutex_unlock(&drones->lock);
        pthread_mutex_destroy(&drones->lock);
        free(drones);
    }
}

int main() {
    // Initialize random seed
    srand(time(NULL));
    
    // Set up signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Initialize map
    printf("Initializing map...\n");
    init_map(40, 30);  // 40 height, 30 width
    printf("Map initialized: %dx%d\n", map.width, map.height);
    printf("Map initialized. map.width=%d, map.height=%d\n", map.width, map.height);
    
    // Initialize lists
    printf("Creating survivor list...\n");
    survivors = create_list(sizeof(Survivor), 1000);  // Max 1000 survivors
    printf("Survivor list created: %p\n", (void*)survivors);
    helpedsurvivors = create_list(sizeof(Survivor), 1000);  // Max 1000 helped survivors
    drones = create_list(sizeof(Drone), 100);  // Max 100 drones
    printf("Helped survivors list: %p, drones list: %p\n", (void*)helpedsurvivors, (void*)drones);
    printf("Global lists initialized.\n");
    
    // Start survivor generator thread
    printf("Starting survivor generator thread...\n");
    if (pthread_create(&survivor_thread_id, NULL, survivor_generator, NULL) != 0) {
        perror("Failed to create survivor generator thread");
        cleanup_resources();
        return 1;
    }
    printf("Survivor generator thread started.\n");
    
    // Start AI controller thread
    if (pthread_create(&ai_thread_id, NULL, ai_controller, NULL) != 0) {
        perror("Failed to create AI controller thread");
        cleanup_resources();
        return 1;
    }
    printf("AI controller thread started.\n");
    
    // Start server thread
    if (pthread_create(&server_thread_id, NULL, run_server_loop, NULL) != 0) {
        perror("Failed to create server thread");
        cleanup_resources();
        return 1;
    }
    printf("Server thread started. Waiting for drone connections...\n");
    
    // Initialize SDL window
    if (init_sdl_window() != 0) {
        fprintf(stderr, "Failed to initialize SDL window\n");
        global_shutdown_flag = 1;
        cleanup_resources();
        return 1;
    }
    printf("SDL Window Initialized. Starting main UI loop.\n");
    
    // Main loop with frame rate limiting
    const int TARGET_FPS = 60;
    const int FRAME_DELAY = 1000 / TARGET_FPS;
    Uint32 frameStart;
    int frameTime;
    
    while (!global_shutdown_flag) {
        frameStart = SDL_GetTicks();
        
        // Handle SDL events
        if (check_events()) {
            global_shutdown_flag = 1;
            break;
        }
        
        // Draw the map and entities
        draw_map();
        
        // Frame rate limiting
        frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < FRAME_DELAY) {
            SDL_Delay(FRAME_DELAY - frameTime);
        }
    }
    
    printf("Exiting main loop. Starting cleanup...\n");
    cleanup_resources();
    return 0;
}