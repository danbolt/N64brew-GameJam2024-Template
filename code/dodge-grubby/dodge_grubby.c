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

const T3DVec3 camera_position = { { 0, 0, -30 } };
const T3DVec3 camera_target = { { 0, 0,   0 } };

T3DVertPacked verts[2] __attribute__((aligned(16)));

const uint8_t ambient_light[4] = {0xff, 0xff, 0xff, 0xff};

void minigame_init()
{
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();

    t3d_init((T3DInitParams){});

    viewport = t3d_viewport_create();
    t3d_mat4_identity(&base_model);
    t3d_mat4_scale(&base_model, 0.4f, 0.4f, 0.4f);
    t3d_mat4_to_fixed(&base_model_fp, &base_model);
    data_cache_hit_writeback(&base_model_fp, sizeof(T3DMat4FP));

    uint16_t norm = t3d_vert_pack_normal(&(T3DVec3){{ 0, 0, 1}});
    verts[0] = (T3DVertPacked){
        .posA = {-16, -16, 0}, .rgbaA = 0xFF0000FF, .normA = norm,
        .posB = { 16, -16, 0}, .rgbaB = 0x00FF00FF, .normB = norm,
    };
    verts[1] = (T3DVertPacked){
        .posA = { 16,  16, 0}, .rgbaA = 0x0000FFFF, .normA = norm,
        .posB = {-16,  16, 0}, .rgbaB = 0xFF00FFFF, .normB = norm,
    };
    data_cache_hit_writeback(verts, sizeof(T3DVertPacked) * 2);
}

void minigame_fixedloop(float deltatime) { }

void minigame_loop(float deltatime)
{
    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(85.0f), 10.0f, 100.0f);
    t3d_viewport_look_at(&viewport, &camera_position, &camera_target, &(T3DVec3){{0,1,0}});

    rdpq_attach(display_get(), NULL);
    t3d_frame_start();
    rdpq_mode_zbuf(false, false);

    t3d_viewport_attach(&viewport);

    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_screen_clear_color(RGBA32(0x22, 0x22, 0x22, 0x0));

    t3d_light_set_ambient(ambient_light);

    t3d_state_set_drawflags(T3D_FLAG_SHADED);

    t3d_matrix_push(UncachedAddr(&base_model_fp));
    t3d_vert_load(UncachedAddr(verts), 0, 4);
    t3d_matrix_pop(1);
    t3d_tri_draw(0, 1, 2);
    t3d_tri_draw(2, 3, 0);

    t3d_tri_sync();

    rdpq_detach_show();
}

void minigame_cleanup()
{
    t3d_destroy();
    display_close();
}