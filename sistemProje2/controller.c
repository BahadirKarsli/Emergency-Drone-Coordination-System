#include "headers/globals.h"
#include "headers/map.h"
#include "headers/drone.h"
#include "headers/survivor.h"
#include "headers/ai.h"
#include "headers/view.h"
#include "headers/server.h" // For run_server_loop

#include <stdio.h>
#include <stdlib.h> // For srand, exit, EXIT_FAILURE
#include <pthread.h>
#include <time.h>   // For time in srand

// Optional: Global flag for graceful shutdown
// extern int global_shutdown_flag; // Declared in globals.h, defined in globals.c

int main() {
    printf("Initializing map...\n");
    srand(time(NULL)); // Initialize random seed

    // Initialize map (ensure map.width and map.height are set before UI init)
    init_map(40, 30); // Example: 40 width, 30 height
    printf("Map initialized. map.width=%d, map.height=%d\n", map.width, map.height);
    if (map.width <= 0 || map.height <= 0) {
        fprintf(stderr, "Map initialization failed or resulted in invalid dimensions. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    // Initialize global lists
    printf("Creating survivor list...\n");
    survivors = create_list(sizeof(Survivor), 1000); // Max 1000 potential survivors
    printf("Survivor list created: %p\n", (void*)survivors);
    helpedsurvivors = create_list(sizeof(Survivor), 1000);
    drones = create_list(sizeof(Drone), 100);         // Max 100 drones
    printf("Helped survivors list: %p, drones list: %p\n", (void*)helpedsurvivors, (void*)drones);

    if (!survivors || !helpedsurvivors || !drones) {
        fprintf(stderr, "Failed to create one or more essential lists. Exiting.\n");
        // Basic cleanup before exit
        if (survivors) survivors->destroy(survivors);
        if (helpedsurvivors) helpedsurvivors->destroy(helpedsurvivors);
        if (drones) drones->destroy(drones);
        freemap();
        exit(EXIT_FAILURE);
    }
    printf("Global lists initialized.\n");

    // Start survivor generation thread
    printf("Starting survivor generator thread...\n");
    pthread_t survivor_thread_id;
    if (pthread_create(&survivor_thread_id, NULL, survivor_generator, NULL) != 0) {
        perror("Failed to create survivor_generator thread");
        exit(EXIT_FAILURE); // Simplified exit, consider more cleanup
    }
    printf("Survivor generator thread started.\n");

    // Start AI controller thread
    pthread_t ai_thread_id;
    if (pthread_create(&ai_thread_id, NULL, ai_controller, NULL) != 0) {
        perror("Failed to create ai_controller thread");
        // Consider cleanup/joining survivor_thread_id before exiting
        exit(EXIT_FAILURE);
    }
    printf("AI controller thread started.\n");

    // Start server thread
    pthread_t server_thread_id;
    if (pthread_create(&server_thread_id, NULL, run_server_loop, NULL) != 0) {
        perror("Failed to create server_thread (run_server_loop)");
        // Consider cleanup/joining other threads
        exit(EXIT_FAILURE);
    }
    printf("Server thread started. Waiting for drone connections...\n");

    // Initialize SDL and UI
    if (init_sdl_window() != 0) { // init_sdl_window should use map.width/height
        fprintf(stderr, "Failed to initialize SDL window. Exiting.\n");
        // Add more robust thread cancellation/joining here for cleanup
        // pthread_cancel(server_thread_id); pthread_join(server_thread_id, NULL); etc.
        exit(EXIT_FAILURE);
    }
    printf("SDL Window Initialized. Starting main UI loop.\n");

    // Draw the first frame and delay before starting the event loop
    draw_map();
    SDL_Delay(500); // Delay for 0.5 seconds to see the first frame

    // Main UI loop
    // global_shutdown_flag = 0; // Initialize if using graceful shutdown flag
    while (!check_events()) { // check_events() returns 1 if SDL_QUIT
        // if (global_shutdown_flag) break; // Check for graceful shutdown
        draw_map(); // This internally calls draw_grid, draw_survivors, draw_drones
        SDL_Delay(100); // Approx 10 FPS for UI updates
    }

    printf("Exiting UI loop. Initiating cleanup...\n");
    // global_shutdown_flag = 1; // Signal other threads to stop

    // Perform cleanup
    // Ideally, join threads after they have exited cleanly.
    // For simplicity in this step, we might rely on program termination to stop threads,
    // or use pthread_cancel if a clean exit isn't fully implemented in all threads yet.
    // Example: pthread_cancel(server_thread_id); pthread_join(server_thread_id, NULL);
    // Ensure AI and survivor threads also have a way to terminate cleanly or are joined.

    freemap();
    printf("Map freed.\n");
    
    if (survivors) survivors->destroy(survivors);
    printf("Survivors list destroyed.\n");
    
    if (helpedsurvivors) helpedsurvivors->destroy(helpedsurvivors);
    printf("Helped survivors list destroyed.\n");
    
    if (drones) drones->destroy(drones); // Drones list might contain malloc'd Drone structs
                                          // Ensure list->destroy or manual iteration handles freeing them.
    printf("Drones list destroyed.\n");
    
    quit_all(); // SDL cleanup
    printf("Cleanup complete. Exiting application.\n");
    return 0;
}