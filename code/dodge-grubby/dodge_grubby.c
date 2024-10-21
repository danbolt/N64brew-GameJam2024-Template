#include <libdragon.h>
#include "../../core.h"
#include "../../minigame.h"
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

#include "emcee_image_data.h"
#include "sixtwelve.h"
#include "sixtwelve_helpers.h"

#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

const MinigameDef minigame_def = {
    .gamename = "Dodge Grubby",
    .developername = "Daniel Savage",
    .description = "Avoid Grubby as he moves around the environment.",
    .instructions = "Use the analog stick to move."
};

T3DViewport viewport;

T3DMat4 base_model;
T3DMat4FP base_model_fp;

const T3DVec3 camera_position = { { 0, 30, 30 } };
const T3DVec3 camera_target = { { 0, 0, 5 } };

#define CIRCLE_VERT_COUNT 16
#define CIRCLE_MODEL_RADIUS 100
#define INV_CIRCLE_MODEL_RADIUS 0.01f
T3DVertPacked circle_verts[CIRCLE_VERT_COUNT] __attribute__((aligned(16)));

const uint8_t ambient_light[4] = {0xff, 0xff, 0xff, 0xff};

const rdpq_texparms_t hud_default_tex_params = { 0 };

surface_t sixtwelve_surface;

#define PLAYER_RADIUS 2.f
#define PLAYER_RADIUS_SQUARED (PLAYER_RADIUS * PLAYER_RADIUS)
#define DOUBLE_PLAYER_RADIUS (PLAYER_RADIUS * 2.0)
#define DOUBLE_PLAYER_RADIUS_SQUARED (DOUBLE_PLAYER_RADIUS * DOUBLE_PLAYER_RADIUS)
#define PLAYER_SHADOW_SCALE (PLAYER_RADIUS * INV_CIRCLE_MODEL_RADIUS)

typedef struct {
    float position[2];

    T3DVec3 input_vector;
} PlayerState;

typedef struct {
    float t;
    float arena_radius;

    PlayerState player_states[MAXPLAYERS];
} GameState;

typedef struct {
    T3DMat4 transform;
    T3DMat4FP transform_fixed;
} PlayerDrawState;

typedef struct {
    T3DMat4 arena_transform;
    T3DMat4FP arena_transform_fixed;

    PlayerDrawState player_draw_states[MAXPLAYERS];
} DrawState;

GameState current_state;

#define NUMBER_OF_DRAW_STATES 3
int current_draw_state;
DrawState draw_states[3];

#define MAX_PLAYER_MOVE_SPEED 5.0

// These should probably be re-evaluated since 2011
#define INPUT_STICK_MIN 7
#define INPUT_STICK_MAX_HORIZONTAL 70
#define INPUT_STICK_MAX_VERTICAL 65

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

void populate_player_input_vector(const joypad_inputs_t* input, T3DVec3* output_vector)
{
    // If the d-pad is being used, apply that and ignore the control stick
    if (input->btn.d_up || input->btn.d_down || input->btn.d_left || input->btn.d_right)
    {
        if (input->btn.d_up)
        {
            output_vector->v[2] = -1.f;
        }
        else if (input->btn.d_down)
        {
            output_vector->v[2] = 1.f;
        }

        if (input->btn.d_right)
        {
            output_vector->v[0] = 1.f;
        }
        else if (input->btn.d_left)
        {
            output_vector->v[0] = -1.f;
        }
    }
    else
    {
        // Otherwise, use the control stick for input
        int8_t x_stick_clamped = CLAMP(input->stick_x, -INPUT_STICK_MAX_HORIZONTAL, INPUT_STICK_MAX_HORIZONTAL);
        if ((x_stick_clamped < INPUT_STICK_MIN) && (x_stick_clamped > -INPUT_STICK_MIN))
        {
            x_stick_clamped = 0;
        }

        int8_t y_stick_clamped = CLAMP(input->stick_y, -INPUT_STICK_MAX_VERTICAL, INPUT_STICK_MAX_VERTICAL);
        if ((y_stick_clamped < INPUT_STICK_MIN) && (y_stick_clamped > -INPUT_STICK_MIN))
        {
            y_stick_clamped = 0;
        }

        output_vector->v[0] = (float)x_stick_clamped / (float)INPUT_STICK_MAX_HORIZONTAL;
        output_vector->v[2] = (float)y_stick_clamped / (float)INPUT_STICK_MAX_VERTICAL * -1.0;
    }

    // Don't allow players to extend past [0.0, 1.0]
    if (t3d_vec3_len2(output_vector) > 1.f)
    {
        t3d_vec3_norm(output_vector);
    }
}

void populate_ai_input_vector(const GameState* state, T3DVec3* output_vector)
{
    // TODO: Make this more interesting
    output_vector->v[0] = 0.f;
    output_vector->v[2] = 0.f;
}

void tick_game_state(GameState* state, float delta)
{
    // Tick time
    state->t += delta;

    const uint32_t playercount = core_get_playercount();

    // Clear movement input
    for (int i = 0; i < MAXPLAYERS; i++) {
        state->player_states[i].input_vector.v[0] = 0.f;
        state->player_states[i].input_vector.v[2] = 0.f;
    }

    // Handle movement input
    for (int i = 0; i < MAXPLAYERS; i++)
    {
        const bool player_is_human = i < playercount;

        if (player_is_human)
        {
            const joypad_port_t joypad_port = core_get_playercontroller(i);
            const joypad_inputs_t input = joypad_get_inputs(joypad_port);
            populate_player_input_vector(&input, &(state->player_states[i].input_vector));
        }
        else
        {
            populate_ai_input_vector(state, &(state->player_states[i].input_vector));
        }
    }

    // Step player movement
    // TODO: Alternate input priority per tick
    const float shortened_arena_radius = (state->arena_radius - PLAYER_RADIUS);
    const float shortened_arena_radius_squared = shortened_arena_radius * shortened_arena_radius;
    for (int i = 0; i < MAXPLAYERS; i++)
    {
        state->player_states[i].position[0] += state->player_states[i].input_vector.v[0] * MAX_PLAYER_MOVE_SPEED * delta;
        state->player_states[i].position[1] += state->player_states[i].input_vector.v[2] * MAX_PLAYER_MOVE_SPEED * delta;

        // Push away from other players
        for (int j = 0; j < MAXPLAYERS; j++)
        {
            // We don't need to detect collision with ourselves
            if (i == j)
            {
                continue;
            }

            const float deltaX = state->player_states[i].position[0] - state->player_states[j].position[0];
            const float deltaY = state->player_states[i].position[1] - state->player_states[j].position[1];
            const float distance_to_other_player_squared = (deltaX * deltaX) + (deltaY * deltaY);

            if (distance_to_other_player_squared < DOUBLE_PLAYER_RADIUS_SQUARED)
            {
                const float angle_to_overlap = atan2(deltaY, deltaX);

                state->player_states[i].position[0] = (cos(angle_to_overlap) * DOUBLE_PLAYER_RADIUS) + state->player_states[j].position[0];
                state->player_states[i].position[1] = (sin(angle_to_overlap) * DOUBLE_PLAYER_RADIUS) + state->player_states[j].position[1];
            }
        }

        // Clamp to inside of the arena
        const float dist_to_center_of_area_squared = state->player_states[i].position[0] * state->player_states[i].position[0] + state->player_states[i].position[1] * state->player_states[i].position[1];
        if (dist_to_center_of_area_squared >= shortened_arena_radius_squared)
        {
            const float angle_to_center = atan2(state->player_states[i].position[1], state->player_states[i].position[0]);

            state->player_states[i].position[0] = cos(angle_to_center) * shortened_arena_radius;
            state->player_states[i].position[1] = sin(angle_to_center) * shortened_arena_radius;
        }
    }
}

void populate_draw_state(const GameState* state, DrawState* to_draw)
{
    const float arena_scale = INV_CIRCLE_MODEL_RADIUS * state->arena_radius;
    t3d_mat4_identity(&(to_draw->arena_transform));
    t3d_mat4_scale(&(to_draw->arena_transform), arena_scale, arena_scale, arena_scale);
    t3d_mat4_to_fixed(&(to_draw->arena_transform_fixed), &(to_draw->arena_transform));

    for (int i = 0; i < MAXPLAYERS; i++)
    {
        t3d_mat4_identity(&(to_draw->player_draw_states[i].transform));
        t3d_mat4_translate(&(to_draw->player_draw_states[i].transform), state->player_states[i].position[0], 0.0, state->player_states[i].position[1]);
        t3d_mat4_scale(&(to_draw->player_draw_states[i].transform), PLAYER_SHADOW_SCALE, PLAYER_SHADOW_SCALE, PLAYER_SHADOW_SCALE);
        t3d_mat4_to_fixed(&(to_draw->player_draw_states[i].transform_fixed), &(to_draw->player_draw_states[i].transform));
    }

    data_cache_hit_writeback(to_draw, sizeof(DrawState));
}

void render_draw_state(const DrawState* to_draw)
{
    // Draw the arena
    t3d_matrix_push(UncachedAddr(&(to_draw->arena_transform_fixed)));
    t3d_vert_load(UncachedAddr(circle_verts), 0, CIRCLE_VERT_COUNT * 2);
    t3d_matrix_pop(1);

    for (int i = 1; i < (CIRCLE_VERT_COUNT * 2) - 1; i++)
    {
        t3d_tri_draw(0, i, i + 1);
    }
    t3d_tri_draw(0, (CIRCLE_VERT_COUNT * 2) - 1, 1);

    t3d_tri_sync();

    rdpq_set_prim_color((color_t){0, 0, 0, 0xff});
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

    // Draw each player's shadow
    for (int pi = 0; pi < MAXPLAYERS; pi++)
    {
        t3d_matrix_push(UncachedAddr(&(to_draw->player_draw_states[pi].transform_fixed)));
        t3d_vert_load(UncachedAddr(circle_verts), 0, CIRCLE_VERT_COUNT * 2);
        t3d_matrix_pop(1);

        for (int i = 1; i < (CIRCLE_VERT_COUNT * 2) - 1; i++)
        {
            t3d_tri_draw(0, i, i + 1);
        }
        t3d_tri_draw(0, (CIRCLE_VERT_COUNT * 2) - 1, 1);

        t3d_tri_sync();
    }
}

void init_game_state(GameState* state)
{
    state->t = 0.f;
    state->arena_radius = 20.f;

    for (int i = 0; i < MAXPLAYERS; i++)
    {
        const float theta = ((float)i / (float)MAXPLAYERS) * T3D_PI * 2.0f;
        state->player_states[i].position[0] = cos(theta) * state->arena_radius * 0.75f;
        state->player_states[i].position[1] = sin(theta) * state->arena_radius * 0.75f;
    }
}

void minigame_init()
{
    generate_circle_model();

    sixtwelve_surface = surface_make_linear(sixtwelve_tex, FMT_IA4, SIXTWELVE_TEXTURE_WIDTH, SIXTWELVE_TEXTURE_HEIGHT);

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();

    t3d_init((T3DInitParams){});

    viewport = t3d_viewport_create();
    t3d_mat4_identity(&base_model);
    t3d_mat4_to_fixed(&base_model_fp, &base_model);
    data_cache_hit_writeback(&base_model_fp, sizeof(T3DMat4FP));

    current_draw_state = 0;

    init_game_state(&current_state);
}

void minigame_fixedloop(float deltatime) {
    tick_game_state(&current_state, deltatime);
}

const char* test_str = "The fish was delish, and it made quite a dish.";

void minigame_loop(float deltatime)
{
    t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(50.0f), 10.0f, 100.0f);
    t3d_viewport_look_at(&viewport, &camera_position, &camera_target, &(T3DVec3){{0,1,0}});

    // const float subtick_delta = (float)(core_get_subtick() * DELTATIME);
    // GameState interpolation_state = current_state;
    // tick_game_state(&interpolation_state, subtick_delta);
    // populate_draw_state(&interpolation_state, &(draw_states[current_draw_state]));

    populate_draw_state(&current_state, &(draw_states[current_draw_state]));

    rdpq_attach(display_get(), NULL);
    t3d_frame_start();
    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_persp(false);
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_zbuf(false, false);

    t3d_viewport_attach(&viewport);

    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    t3d_screen_clear_color(RGBA32(0xff, 0x22, 0x22, 0x0));

    t3d_light_set_ambient(ambient_light);
    t3d_state_set_drawflags(T3D_FLAG_SHADED);

    t3d_matrix_push(UncachedAddr(&base_model_fp));
    render_draw_state(&(draw_states[current_draw_state]));
    t3d_matrix_pop(1);

    // 2D HUD   
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(128);
    rdpq_sync_load();
    rdpq_tex_upload(TILE0, &sixtwelve_surface, &hud_default_tex_params);

    int xStep = 0;
    for (int i = 0; test_str[i] != '\0'; i++)
    {
        const sixtwelve_character_info* info = sixtwelve_get_character_info(test_str[i]);

        rdpq_texture_rectangle(TILE0, xStep + 12 + 0 + info->x_offset, 12 + 0 + info->y_offset, xStep + 12 + info->width + info->x_offset, info->height + 12 + 0 + info->y_offset, info->x, info->y);

        xStep += info->x_advance;
    }

    rdpq_detach_show();

    current_draw_state = (current_draw_state + 1) % NUMBER_OF_DRAW_STATES;
}

void minigame_cleanup()
{
    t3d_destroy();
    display_close();
}