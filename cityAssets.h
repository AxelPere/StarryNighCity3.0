#ifndef CITYASSETS_H
#define CITYASSETS_H

#include "st7796_pico.h"

/* 3D building drawing functions */

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    int x, y;
} Point2D;

Vec3 rotate_y(Vec3 p, float angle) {
    Vec3 result;
    result.x = p.x * cosf(angle) + p.z * sinf(angle);
    result.y = p.y;
    result.z = -p.x * sinf(angle) + p.z * cosf(angle);
    return result;
}

Vec3 rotate_x(Vec3 p, float angle) {
    Vec3 result;
    result.x = p.x;
    result.y = p.y * cosf(angle) - p.z * sinf(angle);
    result.z = p.y * sinf(angle) + p.z * cosf(angle);
    return result;
}

// Converts 3D (x, y, z) to 2D screen coordinates
Point2D project(Vec3 p, float fov, int cam_dist) {
    Point2D out;

    // adjust the object's position relative to the camera
    p.y -= 30.0f;
    p.z += 50.0f;
    
    // Move the object away from the camera along the Z axis so it sits in front of us
    float z_eff = p.z + cam_dist;
    if (z_eff < 0.1f) z_eff = 0.1f; // Prevent division by zero

    // Perspective projection formula
    // We add TFT_WIDTH/2 and TFT_HEIGHT/2 to center the origin (0,0) on the screen
    out.x = (int)((p.x * fov) / z_eff) + (TFT_WIDTH / 2);
    out.y = (int)((p.y * fov) / z_eff) + (TFT_HEIGHT / 2);
    
    return out;
}

// Fast integer hash to scramble window coordinates and eliminate lines/stripes
unsigned int hash_window(int col, int row, float bx, float bz) {
    int seed_x = (int)(bx * 10.0f);
    int seed_z = (int)(bz * 10.0f);

    unsigned int h = col * 374761393U + row * 668265263U + seed_x * 1274126177U + seed_z * 2654435761U;
    h = (h ^ (h >> 13)) * 1274126177U;
    return h ^ (h >> 16);
}

// Updated to take window_col directly instead of screen pixel_x (kills swimming & stripes)
uint16_t get_window_color(float x, float z, int step_index, int window_col, uint16_t base_color) {
    int window_row = step_index / 4; // Floor height spacing

    unsigned int hash = hash_window(window_col, window_row, x, z);

    // 25% of windows lit for a realistic night aesthetic
    if ((hash % 100) < 30) {
        int color_type = hash % 2;
        if (color_type == 0) {
            return RGB565(31, 55, 20); // Warm Yellow
        } else {
            return RGB565(31, 63, 31); // Bright White
        } 
    }

    return base_color; // Clean dark building wall
}

// Fills a 4-corner 2D polygon (quad) on screen using interpolated scanlines
void fill_quad(Point2D p0, Point2D p1, Point2D p2, Point2D p3, int steps, uint16_t color) {
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;

        // Interpolate along Edge 1 (p0 -> p3)
        int x_left = p0.x + (int)(t * (p3.x - p0.x));
        int y_left = p0.y + (int)(t * (p3.y - p0.y));

        // Interpolate along Edge 2 (p1 -> p2)
        int x_right = p1.x + (int)(t * (p2.x - p1.x));
        int y_right = p1.y + (int)(t * (p2.y - p1.y));

        // Fill across the face
        draw_line(x_left, y_left, x_right, y_right, color);
    }
}

// Window-enabled fill quad anchored to world-space dimensions
void fill_quad_windows(Point2D p0, Point2D p1, Point2D p2, Point2D p3, int steps, float x, float z, float face_width, uint16_t color) {
    for (int i = 0; i <= steps; i++) {
        float t_height = (float)i / (float)steps;

        int x_l = p0.x + (int)(t_height * (p3.x - p0.x));
        int y_l = p0.y + (int)(t_height * (p3.y - p0.y));
        int x_r = p1.x + (int)(t_height * (p2.x - p1.x));
        int y_r = p1.y + (int)(t_height * (p2.y - p1.y));

        int x_left = (x_l < x_r) ? x_l : x_r;
        int x_right = (x_l < x_r) ? x_r : x_l;
        int y_left_actual = (x_l < x_r) ? y_l : y_r;
        int y_right_actual = (x_l < x_r) ? y_r : y_l;

        int width_span = x_right - x_left;
        if (width_span <= 0) {
            draw_line(x_l, y_l, x_r, y_r, color);
            continue;
        }

        // Use 4-pixel wide chunks to lock windows in place and eliminate sub-pixel sparkling
        int chunk_size = 4;
        for (int px = 0; px < width_span; px += chunk_size) {
            int current_x = x_left + px;
            int next_x = current_x + chunk_size;
            if (next_x > x_right) next_x = x_right;

            float sub_t_width = (float)px / (float)width_span;
            int cur_y = y_left_actual + (int)(sub_t_width * (y_right_actual - y_left_actual));

            // Stable column index based on fixed pixel chunks from the left edge
            int window_col = px / chunk_size;

            uint16_t line_color = get_window_color(x, z, i, window_col, color);

            draw_line(current_x, cur_y, next_x, cur_y, line_color);
        }
    }
}

/*
    grid size: total width and length of underlying coordinate square
    step: distance between consecutive grid lines
    base: y-coordinate of the ground plane
    angleY: rotation angle around the Y axis (horizontal rotation)
    angleX: rotation angle around the X axis (vertical tilt)
    radius: radius of the circular ground grid
*/
void draw_ground_grid(float angleY, float angleX, int grid_size, int step, int base, float radius) {
    float radius_sq = radius * radius;

    for(int i = 0; i <= grid_size; i += step) {
        // Draw lines along the X axis
        Vec3 p1 = {(float) i - grid_size/2.0f, base, (float) -grid_size/2.0f};
        Vec3 p2 = {(float) i - grid_size/2.0f, base, (float) grid_size/2.0f};

        if (fabsf(p1.x) <= radius) {
            p1.z = -sqrt(radius_sq - (p1.x * p1.x));
            p2.z = sqrt(radius_sq - (p2.x * p2.x));

            Vec3 rp1 = rotate_y(p1, angleY);
            Vec3 rp2 = rotate_y(p2, angleY);
            rp1 = rotate_x(rp1, angleX);
            rp2 = rotate_x(rp2, angleX);

            Point2D screen_p1 = project(rp1, 300.0f, 400);
            Point2D screen_p2 = project(rp2, 300.0f, 400);

            draw_line(screen_p1.x, screen_p1.y, screen_p2.x, screen_p2.y, COLOR_WHITE);
        }

        // Draw lines along the Z axis
        Vec3 p3 = {(float) -grid_size/2.0f, base, (float) i -grid_size/2.0f};
        Vec3 p4 = {(float) grid_size/2.0f, base, (float) i -grid_size/2.0f};

        if (fabsf(p3.z) <= radius) {
            p3.x = -sqrt(radius_sq - (p3.z * p3.z));
            p4.x = sqrt(radius_sq - (p4.z * p4.z));

            Vec3 rp3 = rotate_y(p3, angleY);
            Vec3 rp4 = rotate_y(p4, angleY);
            rp3 = rotate_x(rp3, angleX);
            rp4 = rotate_x(rp4, angleX);

            Point2D screen_p3 = project(rp3, 300.0f, 400);
            Point2D screen_p4 = project(rp4, 300.0f, 400);
            draw_line(screen_p3.x, screen_p3.y, screen_p4.x, screen_p4.y, COLOR_GRID_LINE);
        }
    }
}

// Helper function to clip a 3D line against the camera near plane (z > 0)
bool clip_line_z(Vec3 *p1, Vec3 *p2, float near_z) {
    bool p1_visible = (p1->z > near_z);
    bool p2_visible = (p2->z > near_z);

    if (p1_visible && p2_visible) return true;  // Both visible
    if (!p1_visible && !p2_visible) return false; // Both hidden behind camera

    // One point is behind the camera, interpolate to find the intersection point
    float t = (near_z - p1->z) / (p2->z - p1->z);
    Vec3 clipped;
    clipped.x = p1->x + t * (p2->x - p1->x);
    clipped.y = p1->y + t * (p2->y - p1->y);
    clipped.z = near_z;

    if (!p1_visible) {
        *p1 = clipped;
    } else {
        *p2 = clipped;
    }
    return true;
}

// Draws a solid filled disk for the ocean with smooth clipping
// void draw_water_plane(float angleY, float angleX, int base, float water_radius) {
//     float radius_sq = water_radius * water_radius;

//     for (int z = (int)-water_radius; z <= (int)water_radius; z += 2) {
//         float x_limit = sqrtf(radius_sq - (z * z));
        
//         Vec3 p1 = {-x_limit, base + 2, (float)z};
//         Vec3 p2 = {x_limit, base + 2, (float)z};

//         Vec3 rp1 = rotate_x(rotate_y(p1, angleY), angleX);
//         Vec3 rp2 = rotate_x(rotate_y(p2, angleY), angleX);

//         if (clip_line_z(&rp1, &rp2, -499.0f)) {
//             Point2D sp1 = project(rp1, 300.0f, 400);
//             Point2D sp2 = project(rp2, 300.0f, 400);
//             draw_line(sp1.x, sp1.y, sp2.x, sp2.y, RGB565(2, 5, 12));
//         }
//     }
// }

// Draws a solid filled disk for the island ground with smooth clipping
void draw_ground_base(float angleY, float angleX, int base, float radius) {
    float radius_sq = radius * radius;

    // Determine drawing direction based on camera angle so it's always back-to-front
    int start_z = (int)-radius;
    int end_z = (int)radius;
    int step_z = 2;

    // If camera is rotated, flip the loop direction to maintain correct painter's order
    if (cosf(angleY) < 0.0f) {
        start_z = (int)radius;
        end_z = (int)-radius;
        step_z = -2;
    }

    for (int z = start_z; z != end_z; z += step_z) {
        float x_limit = sqrtf(radius_sq - (z * z));
        
        Vec3 p1 = {-x_limit, base, (float)z};
        Vec3 p2 = {x_limit, base, (float)z};

        Vec3 rp1 = rotate_x(rotate_y(p1, angleY), angleX);
        Vec3 rp2 = rotate_x(rotate_y(p2, angleY), angleX);

        if (clip_line_z(&rp1, &rp2, -499.0f)) {
            Point2D sp1 = project(rp1, 300.0f, 400);
            Point2D sp2 = project(rp2, 300.0f, 400);

            draw_line(sp1.x, sp1.y, sp2.x, sp2.y, COLOR_BLACK);
        }
    }
}

// Helper to check if a quad/face is facing the camera using 2D cross product
// Flipped the sign to correctly cull backfaces based on screen-space Y-down coordinates
int is_face_visible(Point2D p0, Point2D p1, Point2D p4) {
    int dx1 = p1.x - p0.x;
    int dy1 = p1.y - p0.y;
    int dx2 = p4.x - p0.x;
    int dy2 = p4.y - p0.y;
    
    // Changed from > 0 to < 0
    return (dx1 * dy2 - dy1 * dx2) < 0;
}

void draw_building(float x, float z, float width, float height, float depth, float angleY, float angleX, int base, uint16_t color) {
    Vec3 corners[8] = {
        (Vec3){x - width/2,  base, z - depth/2}, // bottom front left
        (Vec3){x + width/2,  base, z - depth/2}, // bottom front right
        (Vec3){x + width/2,  base, z + depth/2}, // bottom back right
        (Vec3){x - width/2,  base, z + depth/2}, // bottom back left
        (Vec3){x - width/2, base - height, z - depth/2}, // top front left
        (Vec3){x + width/2, base - height, z - depth/2}, // top front right
        (Vec3){x + width/2, base - height, z + depth/2}, // top back right
        (Vec3){x - width/2, base - height, z + depth/2} // top back left
    };

    Point2D pts[8];

    for(int i = 0; i < 8; i++){
        Vec3 rotated = rotate_y(corners[i], angleY);
        rotated = rotate_x(rotated, angleX);
        pts[i] = project(rotated, 300.0f, 400);
    }

    // Measure the exact vertical height of the building in screen pixels
    int pixel_height = abs(pts[4].y - pts[0].y);
    if (pixel_height < 1) pixel_height = 1;
    
    // Steps now perfectly match screen pixels (capped so it never exceeds world height)
    int steps = pixel_height;
    if (steps > (int)height) steps = (int)height;

    // BACKFACE CULLING: Only draw faces that are actually facing the camera ---
    
    // Front face (0, 1, 5, 4)
    if (is_face_visible(pts[0], pts[1], pts[4])) {
        fill_quad_windows(pts[0], pts[1], pts[5], pts[4], steps, x, z, width, color);
    }
    // Right face (1, 2, 6, 5)
    if (is_face_visible(pts[1], pts[2], pts[5])) {
        fill_quad_windows(pts[1], pts[2], pts[6], pts[5], steps, x, z, depth, color);
    }
    // Back face (2, 3, 7, 6)
    if (is_face_visible(pts[2], pts[3], pts[6])) {
        fill_quad_windows(pts[2], pts[3], pts[7], pts[6], steps, x, z, width, color);
    }
    // Left face (3, 0, 4, 7)
    if (is_face_visible(pts[3], pts[0], pts[7])) {
        fill_quad_windows(pts[3], pts[0], pts[4], pts[7], steps, x, z, depth, color);
    }
    
    // Top face is always visible from your top-down camera tilt angle
    fill_quad(pts[4], pts[5], pts[6], pts[7], steps, color);
    // fill_quad_windows(pts [0], pts [1], pts [2], pts [3], steps, x, z, color); // bottom face

    // // wire frame
    // for (int i = 0; i < 8; i++) {
    //     for (int j = i + 1; j < 8; j++) {
    //         draw_line(pts[i].x, pts[i].y, pts[j].x, pts[j].y, COLOR_WHITE);
    //     }
    // }
}
#define MAX_STARS 200

typedef struct {
    float azimuth;   // Horizontal angle (0 to 2*PI)
    float elevation; // Vertical angle (0 to PI/2, keeping them in the upper sky)
} Star;

// Generate stars once at startup
Star stars[MAX_STARS];
void init_stars() {
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].azimuth = ((float)(rand() % 1000) / 1000.0f) * 6.28318f; // Full 360 degrees around
        
        // 2.1f multiplier controls total vertical spread height from bottom to top
        // Subtraction 0.6f shifts the range so that stars can appear below the horizon as well
        stars[i].elevation = ((float)(rand() % 1000) / 1000.0f) * 2.1f - 0.6f;
    }
}

void draw_stars(float angleY, float angleX) {
    for (int i = 0; i < MAX_STARS; i++) {
        float r = 400.0f;
        float x = r * cosf(stars[i].elevation) * sinf(stars[i].azimuth);
        float y = -r * sinf(stars[i].elevation); 
        float z = r * cosf(stars[i].elevation) * cosf(stars[i].azimuth);

        Vec3 star_pos = {x, y, z};

        Vec3 rotated = rotate_y(star_pos, angleY);
        rotated = rotate_x(rotated, angleX);

        // Draw if in front of the camera
        if (rotated.z > 0) {
            Point2D screen_star = project(rotated, 300.0f, 400);
            
            if (screen_star.x >= 0 && screen_star.x < 480 && screen_star.y >= 0 && screen_star.y < 320) {
                fb_set(screen_star.x, screen_star.y, COLOR_WHITE);
            }
        }
    }
}

typedef struct {
    float x, z;       // World coordinates
    float speed;      // Movement speed
    int axis;         // 0 = moving along X road, 1 = moving along Z road
    uint16_t color;   // Body color
} Car;

#define MAX_CARS 100
Car cars[MAX_CARS];

void init_cars() {
    int grid_step = 50;
    int max_idx = 3; // Gives positions like -125, -75, -25, 25, 75, 125

    for (int i = 0; i < MAX_CARS; i++) {
        cars[i].axis = rand() % 2;
        
        int r1 = (rand() % (max_idx * 2)) - max_idx;
        int r2 = (rand() % (max_idx * 2)) - max_idx;

        // Force cars to sit strictly on the road channels (+25 offset from building intersections)
        if (cars[i].axis == 0) {
            cars[i].x = (float)(r1 * grid_step);
            cars[i].z = (float)(r2 * grid_step) + 25.0f; // Road channel along Z
        } else {
            cars[i].x = (float)(r1 * grid_step) + 25.0f; // Road channel along X
            cars[i].z = (float)(r2 * grid_step);
        }

        cars[i].speed = 1.0f + ((float)(rand() % 5) * 0.2f);
        
        // Base low brightness values so cars look dim and realistic at night
        int r = rand() % 10 + 2;
        int g = rand() % 15 + 2;
        int b = rand() % 10 + 2;

        // Randomly boost one channel to create distinct body colors (maroon, navy, bronze, dark green)
        int tint = rand() % 5;
        if (tint == 0) r += 8;       // Dark Red / Maroon
        else if (tint == 1) b += 8;  // Dark Navy Blue
        else if (tint == 2) { r += 6; g += 6; } // Dark Bronze / Amber
        else if (tint == 3) g += 6;  // Dark Emerald

        // Clamp to valid RGB565 channel limits (R: 0-31, G: 0-63, B: 0-31)
        if (r > 31) r = 31;
        if (g > 63) g = 63;
        if (b > 31) b = 31;

        cars[i].color = RGB565(b, g, r);
    }
}

void update_cars(float radius) {
    float bound = 175.0f; // Matches the outer road limits so they wrap cleanly

    for (int i = 0; i < MAX_CARS; i++) {
        if (cars[i].axis == 0) {
            cars[i].x += cars[i].speed;
            if (cars[i].x > bound) {
                cars[i].x = -bound; // Cleanly loop back to the other side of the city
            }
        } else {
            cars[i].z += cars[i].speed;
            if (cars[i].z > bound) {
                cars[i].z = -bound; // Cleanly loop back
            }
        }
    }
}

void draw_cars(float angleY, float angleX, int base) {
    for (int i = 0; i < MAX_CARS; i++) {
        // Correct length/width orientation based on travel axis
        float length = 12.0f;
        float width = 6.0f;
        float h = 4.0f;
        float cy = (float)base - 2.0f;

        float x_len = (cars[i].axis == 0) ? length : width;
        float z_len = (cars[i].axis == 0) ? width : length;

        Vec3 corners[8] = {
            {cars[i].x - x_len/2, cy,     cars[i].z - z_len/2},
            {cars[i].x + x_len/2, cy,     cars[i].z - z_len/2},
            {cars[i].x + x_len/2, cy,     cars[i].z + z_len/2},
            {cars[i].x - x_len/2, cy,     cars[i].z + z_len/2},
            {cars[i].x - x_len/2, cy - h, cars[i].z - z_len/2},
            {cars[i].x + x_len/2, cy - h, cars[i].z - z_len/2},
            {cars[i].x + x_len/2, cy - h, cars[i].z + z_len/2},
            {cars[i].x - x_len/2, cy - h, cars[i].z + z_len/2}
        };

        Point2D pts[8];
        for (int j = 0; j < 8; j++) {
            Vec3 rotated = rotate_y(corners[j], angleY);
            rotated = rotate_x(rotated, angleX);
            pts[j] = project(rotated, 300.0f, 400);
        }

        fill_quad(pts[4], pts[5], pts[6], pts[7], 4, cars[i].color);
        
        if (pts[4].x >= 0 && pts[4].x < 480 && pts[4].y >= 0 && pts[4].y < 320) {
            fb_set(pts[4].x, pts[4].y, COLOR_YELLOW); // Headlight
        }
    }
}

typedef struct {
    float x, z;
} StreetLamp;

#define MAX_LAMPS 100
StreetLamp lamps[MAX_LAMPS];

void init_lamps(float radius) {
    int grid_step = 50;
    int max_steps = (int)(radius / grid_step);
    int count = 0;
    float safe_radius = radius - 30.0f; 

    // Place lamps along the road edges/sidewalks of each grid coordinate
    for (int rx = -max_steps; rx <= max_steps && count < MAX_LAMPS; rx++) {
        for (int rz = -max_steps; rz <= max_steps && count < MAX_LAMPS; rz++) {
            // Position them alongside the road channels (matching car lanes)
            float x = (float)(rx * grid_step) + 20.0f;
            float z = (float)(rz * grid_step); // Staggered along the axis

            if ((x * x + z * z) <= (safe_radius * safe_radius)) {
                lamps[count].x = x;
                lamps[count].z = z;
                count++;
            }
        }
    }
}

void draw_lamps(float angleY, float angleX, int base) {
    for (int i = 0; i < MAX_LAMPS; i++) {
        float lamp_height = 18.0f;
        float cy = (float)base;

        Vec3 p_bottom = {lamps[i].x, cy, lamps[i].z};
        Vec3 p_top    = {lamps[i].x, cy - lamp_height, lamps[i].z};

        Vec3 rp1 = rotate_x(rotate_y(p_bottom, angleY), angleX);
        Vec3 rp2 = rotate_x(rotate_y(p_top, angleY), angleX);

        // Use clip_line_z so lamps in the front don't get unfairly culled out
        if (clip_line_z(&rp1, &rp2, -499.0f)) {
            Point2D sp1 = project(rp1, 300.0f, 400);
            Point2D sp2 = project(rp2, 300.0f, 400);
            
            // Draw pole
            draw_line(sp1.x, sp1.y, sp2.x, sp2.y, RGB565(12, 24, 12));

            // Draw glowing bulb cluster
            if (sp2.x >= 1 && sp2.x < 479 && sp2.y >= 1 && sp2.y < 319) {
                fb_set(sp2.x, sp2.y, RGB565(31, 63, 20)); 
                
                fb_set(sp2.x + 1, sp2.y, RGB565(31, 45, 0));
                fb_set(sp2.x - 1, sp2.y, RGB565(31, 45, 0));
                fb_set(sp2.x, sp2.y + 1, RGB565(31, 45, 0));
                fb_set(sp2.x, sp2.y - 1, RGB565(31, 45, 0));
            }
        }
    }
}

typedef struct {
    float azimuth;   // Random horizontal position around the sky
    float elevation; // Height angle in the sky
    float radius;    // Size of the moon
    uint16_t color;  // Pale glowing color
} Moon;

Moon moon;

void init_moon() {
    // Pick a random starting angle anywhere around the 360-degree horizon
    moon.azimuth = ((float)(rand() % 1000) / 1000.0f) * 6.28318f;
    moon.elevation = 0.5f; // High up in the upper sky
    moon.radius = 16.0f;    // Nice large size
    moon.color = RGB565(31, 63, 28); // Soft pale warm white/yellow glow
}

void draw_moon(float angleY, float angleX) {
    float r = 400.0f;
    float x = r * cosf(moon.elevation) * sinf(moon.azimuth);
    float y = -r * sinf(moon.elevation); 
    float z = r * cosf(moon.elevation) * cosf(moon.azimuth);

    Vec3 moon_pos = {x, y, z};

    Vec3 rotated = rotate_y(moon_pos, angleY);
    rotated = rotate_x(rotated, angleX);

    // Only draw if it's in front of the camera
    if (rotated.z > 0) {
        Point2D sp = project(rotated, 300.0f, 400);

        int mr = (int)moon.radius;
        for (int dy = -mr; dy <= mr; dy++) {
            for (int dx = -mr; dx <= mr; dx++) {
                if (dx * dx + dy * dy <= mr * mr) {
                    int px = sp.x + dx;
                    int py = sp.y + dy;
                    if (px >= 0 && px < 480 && py >= 0 && py < 320) {
                        // Create a soft crater/rim shading effect on the outer edge
                        if (dx * dx + dy * dy > (mr - 3) * (mr - 3)) {
                            fb_set(px, py, RGB565(20, 42, 18)); // Soft pale rim
                        } else {
                            fb_set(px, py, moon.color);         // Bright core
                        }
                    }
                }
            }
        }
    }
}

#define MAX_BUILDINGS 50

typedef struct {
    float x, z;
    float width, height, depth;
    uint16_t color;
} Building;

int num_buildings = MAX_BUILDINGS;
Building buildings[MAX_BUILDINGS];

static inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

uint16_t random_building_color(void) {
    // Base brightness, scaled independently per channel's max (R:31, G:63, B:31)
    int level = rand() % 10 + 3; // 3..12 out of 31 dark to medium-dark

    int r_dark = level;
    int g_dark = level * 2;      // green's range is 2x, keep proportional -> stays grey
    int b_dark = level;

    // Tiny per-channel jitter so buildings aren't perfectly flat/identical
    r_dark += (rand() % 3) - 1;
    g_dark += (rand() % 3) - 1;
    b_dark += (rand() % 3) - 1;

    // Occasional subtle tint: ~30% warm (brick/concrete), ~30% cool (glass/blue-grey), else neutral
    int tint = rand() % 10;
    if (tint < 3) {
        r_dark += 2; // warm
    } else if (tint < 6) {
        b_dark += 2; // cool
    }

    r_dark = clampi(r_dark, 1, 31);
    g_dark = clampi(g_dark, 1, 63);
    b_dark = clampi(b_dark, 1, 31);

    return RGB565(b_dark, g_dark, r_dark);
}

void generate_buildings(float radius, int step) {
    float safe_radius = radius - 40.0f; // Leaves a nice perimeter margin for water/roads

    for (int i = 0; i < num_buildings; i++) {
        float x, z;
        
        // Keep picking random grid points until one lands safely INSIDE the circle
        do {
            int max_steps = (int)(radius / step);
            int rx = (rand() % (max_steps * 2)) - max_steps;
            int rz = (rand() % (max_steps * 2)) - max_steps;
            
            x = (float)(rx * step);
            z = (float)(rz * step);
            
        } while ((x * x + z * z) > (safe_radius * safe_radius));

        buildings[i].x = x;
        buildings[i].z = z;

        // Random dimensions (width and depth stay uniform or varied)
        buildings[i].width  = (float)((rand() % 20) + 20);  
        buildings[i].depth  = (float)((rand() % 20) + 20);  

        // Find distance from the center (0,0)
        float dist_from_center = sqrtf(x * x + z * z);
        
        // Normalize distance: 0.0 at the center, 1.0 at the outer safe edge
        float edge_factor = dist_from_center / safe_radius;
        if (edge_factor > 1.0f) edge_factor = 1.0f;

        // Center buildings are skyscrapers (tall range), edge buildings are short
        int min_possible_height = 30;
        int max_possible_height = 200;
        
        // Invert edge_factor so 0 is at the edge and 1 is at the center
        float center_closeness = 1.0f - edge_factor;

        // Interpolate height based on how close it is to the center, plus some random variation
        float base_height = min_possible_height + (center_closeness * (max_possible_height - min_possible_height));
        buildings[i].height = base_height + (float)(rand() % 30); // Add a little organic randomness

        // Pick a random color
        buildings[i].color = random_building_color();

        // Check for overlaps
        for (int j = 0; j < num_buildings; j++) {
            if (i != j) {
                float dx = buildings[i].x - buildings[j].x;
                float dz = buildings[i].z - buildings[j].z;
                float min_dist = (buildings[i].width + buildings[j].width) * 0.5f;
                if ((dx * dx + dz * dz) < (min_dist * min_dist)) {
                    i--;
                    break;
                }
            }
        }
    }
}

float get_rotated_depth(Building b, float camera_angle) {
    Vec3 center = { b.x, 0.0f, b.z };
    Vec3 rot = rotate_y(center, camera_angle);
    return rot.z; // Return transformed Z depth
}

void sort_buildings_by_depth(Building arr[], float camera_angle) {
    for (int j = 0; j < num_buildings; j++) {
        int max = j;
        for (int i = j + 1; i < num_buildings; i++) {
            // Compare rotated Z depths instead of raw world z coordinates
            if (get_rotated_depth(arr[i], camera_angle) > get_rotated_depth(arr[max], camera_angle)) {
                max = i;
            }
        }
        // Swap
        Building temp = arr[j];
        arr[j] = arr[max];
        arr[max] = temp;
    }
}

#endif /* CITYASSETS_H */