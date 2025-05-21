#include <SDL2/SDL.h>
#include "headers/drone.h"
#include "headers/map.h"
#include "headers/survivor.h"
#include "headers/view.h"
#include "headers/globals.h"
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <stdio.h>

#define GRID_SIZE 30
#define CELL_SIZE 20

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
SDL_Event event;
int window_width, window_height;

const SDL_Color BLACK = {0, 0, 0, 255};
const SDL_Color RED = {255, 0, 0, 255};
const SDL_Color BLUE = {0, 0, 255, 255};
const SDL_Color GREEN = {0, 255, 0, 255};
const SDL_Color WHITE = {255, 255, 255, 255};
const SDL_Color YELLOW = {255, 255, 0, 255};
const SDL_Color GRAY = {128, 128, 128, 255};

int init_sdl_window() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return -1;
    }

    window_width = map.width * CELL_SIZE;
    window_height = map.height * CELL_SIZE;

    if (window_width <= 0 || window_height <= 0) {
        fprintf(stderr, "Map dimensions are invalid. Width: %d, Height: %d\n", map.width, map.height);
        window_width = 800;
        window_height = 600;
    }

    window = SDL_CreateWindow("Drone Simulator", SDL_WINDOWPOS_UNDEFINED,
                             SDL_WINDOWPOS_UNDEFINED, window_width,
                             window_height, SDL_WINDOW_SHOWN);
    if (window == NULL) {
        fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) {
        SDL_DestroyWindow(window);
        fprintf(stderr, "Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    printf("SDL Initialized. Window: %dx%d\n", window_width, window_height);
    return 0;
}

void draw_cell(int x, int y, SDL_Color color) {
    if (!renderer) return;
    SDL_Rect cell_rect;
    cell_rect.x = x * CELL_SIZE;
    cell_rect.y = y * CELL_SIZE;
    cell_rect.w = CELL_SIZE;
    cell_rect.h = CELL_SIZE;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &cell_rect);
}

void draw_drones() {
    if (!drones || !renderer) return;

    pthread_mutex_lock(&drones->lock);
    Node *current = drones->head;
    while (current != NULL) {
        Drone *drone = (Drone *)current->data;
        if (drone) {
            pthread_mutex_lock(&drone->lock);

            SDL_Color drone_color;
            switch (drone->status) {
                case IDLE:
                    drone_color = BLUE;
                    break;
                case ON_MISSION:
                    drone_color = GREEN;
                    break;
                case DISCONNECTED:
                    drone_color = GRAY;
                    break;
                default:
                    drone_color = YELLOW;
                    break;
            }

            if (drone->coord.x >= 0 && drone->coord.x < map.width &&
                drone->coord.y >= 0 && drone->coord.y < map.height) {
                printf("Drawing drone %d at (%d, %d), status=%d\n", drone->id, drone->coord.x, drone->coord.y, drone->status);
                draw_cell(drone->coord.x, drone->coord.y, drone_color);

                if (drone->status == ON_MISSION) {
                    if (drone->target.x >= 0 && drone->target.x < map.width &&
                        drone->target.y >= 0 && drone->target.y < map.height) {
                        SDL_SetRenderDrawColor(renderer, GREEN.r, GREEN.g, GREEN.b, 200);
                        SDL_RenderDrawLine(renderer, 
                                         drone->coord.x * CELL_SIZE + CELL_SIZE / 2, 
                                         drone->coord.y * CELL_SIZE + CELL_SIZE / 2, 
                                         drone->target.x * CELL_SIZE + CELL_SIZE / 2, 
                                         drone->target.y * CELL_SIZE + CELL_SIZE / 2);
                    }
                }
            }
            pthread_mutex_unlock(&drone->lock);
        }
        current = current->next;
    }
    pthread_mutex_unlock(&drones->lock);
}

void draw_survivors() {
    static int last_count = -1;
    if (!survivors || !renderer) return;

    pthread_mutex_lock(&survivors->lock);
    Node *current = survivors->head;
    int count = 0;
    while (current != NULL) {
        count++;
        Survivor *s = (Survivor *)current->data;
        if (s) {
            if (s->coord.x >= 0 && s->coord.x < map.width &&
                s->coord.y >= 0 && s->coord.y < map.height) {
                draw_cell(s->coord.x, s->coord.y, RED);
            }
        }
        current = current->next;
    }
    if (count != last_count) {
        printf("draw_survivors: survivors in list = %d\n", count);
        last_count = count;
    }
    pthread_mutex_unlock(&survivors->lock);

    if (!helpedsurvivors || !renderer) return;
    pthread_mutex_lock(&helpedsurvivors->lock);
    current = helpedsurvivors->head;
    while(current != NULL) {
        Survivor* s = (Survivor*)current->data;
        if (s) {
            if (s->coord.x >= 0 && s->coord.x < map.width &&
                s->coord.y >= 0 && s->coord.y < map.height) {
                SDL_Color helped_survivor_color = {255, 100, 100, 255};
                draw_cell(s->coord.x, s->coord.y, helped_survivor_color);
            }
        }
        current = current->next;
    }
    pthread_mutex_unlock(&helpedsurvivors->lock);
}

void draw_grid() {
    if (!renderer || map.width <= 0 || map.height <= 0) return;
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 200);
    for (int x_pos = 0; x_pos <= map.width; ++x_pos) {
        SDL_RenderDrawLine(renderer, x_pos * CELL_SIZE, 0, x_pos * CELL_SIZE, map.height * CELL_SIZE);
    }
    for (int y_pos = 0; y_pos <= map.height; ++y_pos) {
        SDL_RenderDrawLine(renderer, 0, y_pos * CELL_SIZE, map.width * CELL_SIZE, y_pos * CELL_SIZE);
    }
}

int draw_map() {
    if (!renderer) return -1;

    SDL_SetRenderDrawColor(renderer, WHITE.r, WHITE.g, WHITE.b, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    draw_grid();
    draw_survivors();
    draw_drones();

    SDL_RenderPresent(renderer);
    return 0;
}

int check_events() {
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return 1;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            return 1;
    }
    return 0;
}

void quit_all() {
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    printf("SDL Quit successfully.\n");
}