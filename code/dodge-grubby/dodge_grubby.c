#include <libdragon.h>
#include "../../core.h"
#include "../../minigame.h"
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

const MinigameDef minigame_def = {
    .gamename = "Dodge Grubby",
    .developername = "Daniel Savage",
    .description = "Avoid Grubby as he moves around the environment.",
    .instructions = "Use the analog stick to move."
};

T3DViewport viewport;

T3DMat4 base_model;
T3DMat4FP base_model_fp;

const T3DVec3 camera_position = { { 0, 15, -30 } };
const T3DVec3 camera_target = { { 0, 0, 0 } };

#define CIRCLE_VERT_COUNT 16
#define CIRCLE_MODEL_RADIUS 100
#define INV_CIRCLE_MODEL_RADIUS 0.01f
T3DVertPacked circle_verts[CIRCLE_VERT_COUNT] __attribute__((aligned(16)));

const uint8_t ambient_light[4] = {0xff, 0xff, 0xff, 0xff};

void generate_circle_model()
{
    circle_verts[0] = (T3DVertPacked){
        .posA = {  0,  0, 0 }, .rgbaA = 0xFFFFFFFF,
        .posB = {  CIRCLE_MODEL_RADIUS,  0, 0 }, .rgbaB = 0xFFFFFFFF,
    };

    for (int i = 1; i < CIRCLE_VERT_COUNT; i++)
    {
        const float theta0 = ((float)((i * 2) + 0) / (float)(CIRCLE_VERT_COUNT * 2)) * T3D_PI * 2.0;
        const float theta1 = ((float)((i * 2) + 1) / (float)(CIRCLE_VERT_COUNT * 2)) * T3D_PI * 2.0;

        circle_verts[i] = (T3DVertPacked){
            .posA = {  (int16_t)( cos(theta0) * CIRCLE_MODEL_RADIUS ), 0, (int16_t)( sin(theta0) * CIRCLE_MODEL_RADIUS ) }, .rgbaA = 0xFFFFFFFF,
            .posB = {  (int16_t)( cos(theta1) * CIRCLE_MODEL_RADIUS ), 0, (int16_t)( sin(theta1) * CIRCLE_MODEL_RADIUS ) }, .rgbaB = 0xFFFFFFFF,
        };
    }
    data_cache_hit_writeback(circle_verts, sizeof(circle_verts));
}

void minigame_init()
{
    generate_circle_model();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();

    t3d_init((T3DInitParams){});

    viewport = t3d_viewport_create();
    t3d_mat4_identity(&base_model);
    t3d_mat4_scale(&base_model, INV_CIRCLE_MODEL_RADIUS, INV_CIRCLE_MODEL_RADIUS, INV_CIRCLE_MODEL_RADIUS);
    t3d_mat4_scale(&base_model, 10.f, 10.f, 10.f);
    t3d_mat4_to_fixed(&base_model_fp, &base_model);
    data_cache_hit_writeback(&base_model_fp, sizeof(T3DMat4FP));
}

void minigame_fixedloop(float deltatime) { }

void minigame_loop(float deltatime)
{
    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(85.0f), 10.0f, 100.0f);
    t3d_viewport_look_at(&viewport, &camera_position, &camera_target, &(T3DVec3){{0,1,0}});

    rdpq_attach(display_get(), NULL);
    t3d_frame_start();
    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_persp(false);
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_zbuf(false, false);

    t3d_viewport_attach(&viewport);

    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_screen_clear_color(RGBA32(0x22, 0x22, 0x22, 0x0));

    t3d_light_set_ambient(ambient_light);

    t3d_state_set_drawflags(T3D_FLAG_SHADED);

    t3d_matrix_push(UncachedAddr(&base_model_fp));
    t3d_vert_load(UncachedAddr(circle_verts), 0, CIRCLE_VERT_COUNT * 2);
    t3d_matrix_pop(1);
    for (int i = 1; i < (CIRCLE_VERT_COUNT * 2) - 1; i++) {
        t3d_tri_draw(0, i, i + 1);
    }
    t3d_tri_draw(0, (CIRCLE_VERT_COUNT * 2) - 1, 1);

    t3d_tri_sync();

    rdpq_detach_show();
}

void minigame_cleanup()
{
    t3d_destroy();
    display_close();
}