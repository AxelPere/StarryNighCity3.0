#include "pico/stdlib.h"
#include "st7796_pico.h"
#include "cityAssets.h"

static int radius = 250; 
static int base = 50;

int main() {
    stdio_init_all();

    tft_init();
    init_stars();
    init_cars();
    init_lamps(radius);
    init_moon();

    srand(1234); // Seed the random number generator
    generate_buildings(radius, 50);

    float angleY = 0.0f;
    float angleX = 0.5f; // fixed tilt angle for X axis

    while (true) {
        // --- RENDERING ---
        fill_screen(COLOR_NIGHT_SKY);
        draw_stars(angleY, angleX);
        draw_moon(angleY, angleX);

        // Draw solid island ground base
        draw_ground_base(angleY, angleX, base, radius);

        update_cars(radius);
        draw_cars(angleY, angleX, base);
        draw_lamps(angleY, angleX, base);

        // Sort and Draw Buildings
        sort_buildings_by_depth(buildings, angleY);
        for (int i = 0; i < num_buildings; i++) {
            draw_building(buildings[i].x, buildings[i].z, buildings[i].width, buildings[i].height,
                          buildings[i].depth, angleY, angleX, base, buildings[i].color);
        }

        tft_present();
        
        angleY += 0.025f; // Slowly rotate the view around the Y axis

        if(angleY > 6.283185307179586f) {
            angleY = 0;
        }
    }
}