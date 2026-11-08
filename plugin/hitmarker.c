/* hitmarker: a bbhost plugin that plays sounds of your own in place of the
 * game's - a headshot's, and a bullet's impact.
 *
 * The game plays its sounds by event name ("headshot", "s000020104", ...)
 * through one function, which the plugin hooks. When the name is one of a
 * setting's names, the plugin answers the call as the game does with its
 * sound turned off (an empty handle), so the game's sound never starts, and
 * plays its own WAV instead through miniaudio, on a device of its own.
 *
 * Settings ([hitmarker] in bbhost.toml, or F9 in game):
 *   headshot_sound / hit_sound   WAV/MP3/FLAC, relative to the plugin's folder
 *   headshot_names / hit_names   the game's event names each one replaces
 *   mute_names                   events silenced with no sound in their place
 *   after_hit_names              events silenced only just after a hit or a
 *                                headshot (the impact's pop shared with walls)
 *   (a name ending in * covers every name it starts)
 *   shot_names                   a gunshot's sound: hits and headshots count only
 *                                within SHOT_FRAMES frames after one (the same
 *                                impact events play when an enemy hits you)
 * A headshot wins over a hit: the hit sound of the same moment stops, and
 * none starts in the frames after it.
 *   headshot_volume / hit_volume 0-2, each sound's own
 *
 * Copyright (C) 2026 ambientmf
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. This program is distributed WITHOUT ANY WARRANTY; see
 * the GNU General Public License (LICENSE) for details. */
#include "bbhost_plugin.h"

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The game's sound by name (1.09 eboot, Binary Ninja addresses): (handle
 * out, name, point, ...) -> handle. Two entry points share the shape. */
#define PLAY_NAMED 0x2bd3a80
#define PLAY_NAMED_B 0x2bd3b40
/* What the play function writes into the handle when the game's sound is
 * off: an id that names no sound (the handle's first word) and its pair. */
#define NO_SOUND_ID 0x5ac7e94
#define NO_SOUND_PAIR 0x5ac7e98

#define VOICES 8
#define NAMES_MAX 512

static const BbHostApi* api;
static char dir[1024];
static unsigned frame;

typedef struct {
    const char* key;      /* setting with the file */
    const char* names_key;/* setting with the event names */
    char names[NAMES_MAX];/* ",name,name," for a quick search */
    ma_sound voice[VOICES];
    int loaded;
    unsigned next;
    const char* volume_key;
} Clip;

static ma_engine engine;
static int engine_ok;
static ma_mutex lock;
/* The last two play nothing: the events they name are only silenced, the
 * last one only within NEAR_FRAMES frames of a hit or a headshot. */
enum { HEADSHOT, HIT, MUTE, AFTER_HIT, SHOT, CLIPS };
#define NEAR_FRAMES 5
static Clip clips[CLIPS] = {
    {"headshot_sound", "headshot_names", .volume_key = "headshot_volume"},
    {"hit_sound", "hit_names", .volume_key = "hit_volume"},
    {NULL, "mute_names"},
    {NULL, "after_hit_names"},
    {NULL, "shot_names"},
};
/* A bullet lands 0-30 frames after its shot in the logs; a far one later. */
#define SHOT_FRAMES 60
static unsigned last_shot = 0x80000000u;
static unsigned last_hit = 0x80000000u, last_headshot = 0x80000000u;

BB_PLUGIN_EXPORT const BbPluginInfo bb_plugin_info = {
    BB_PLUGIN_API_VERSION, 0, "hitmarker", "Hitmarker", "0.1.0", "ambientmf",
    "Your own sounds for a headshot and a bullet's impact (a meme mod): the Hunt: Showdown headshot and the CoD hit "
    "marker by default.",
};

BB_PLUGIN_EXPORT const BbOption bb_plugin_options[] = {
    {BB_OPT_FLOAT | BB_OPT_LIVE, "headshot_volume", "Headshot volume", 0, "0.35", 0.0, 2.0},
    {BB_OPT_FLOAT | BB_OPT_LIVE, "hit_volume", "Hit volume", 0, "0.25", 0.0, 2.0},
    {BB_OPT_TEXT | BB_OPT_LIVE, "headshot_sound", "Headshot sound",
     "A WAV, MP3 or FLAC file, in the plugin's folder or a full path.", "headshot.wav"},
    {BB_OPT_TEXT | BB_OPT_LIVE, "hit_sound", "Hit sound",
     "A WAV, MP3 or FLAC file, in the plugin's folder or a full path.", "hit.wav"},
    {BB_OPT_TEXT | BB_OPT_LIVE, "headshot_names", "Headshot events",
     "The game's sound events the headshot sound replaces, comma-separated. z006021060 is a bullet's impact on a "
     "hunter's head.",
     "z006021060"},
    {BB_OPT_TEXT | BB_OPT_LIVE, "hit_names", "Hit events",
     "The game's sound events the hit sound replaces, comma-separated; a name ending in * covers every name it starts "
     "z006021* are a bullet's thuds on a body (z006020* on walls and the ground).",
     "z006021*"},
    {BB_OPT_TEXT | BB_OPT_LIVE, "mute_names", "Muted events",
     "Events always silenced, with nothing in their place.", ""},
    {BB_OPT_TEXT | BB_OPT_LIVE, "after_hit_names", "Muted after a hit",
     "Events silenced only in the frames just after a hit or a headshot: s000006001 is the pop every bullet impact "
     "makes, walls included; c999999990 the game's own headshot sound.",
     "s000006001,c999999990"},
    {BB_OPT_TEXT | BB_OPT_LIVE, "shot_names", "Gunshot events",
     "A gun firing: hits and headshots count only in the second after one of these, so an enemy's blow on you (the "
     "same impact sounds) keeps the game's sound. c000000720 is a hunter's gunshot.",
     "c000000720"},
    {0},
};

static const char* setting(const char* key, const char* fallback) {
    char full[64];
    snprintf(full, sizeof full, "hitmarker.%s", key);
    const char* v = api->config(full);
    return v && v[0] ? v : fallback;
}

static const char* option_default(const char* key) {
    for (const BbOption* o = bb_plugin_options; o->type; o++)
        if (strcmp(o->key, key) == 0) return o->def ? o->def : "";
    return "";
}

/* ",a,b," from "a, b" */
static void set_names(Clip* c, const char* list) {
    size_t n = 0;
    c->names[n++] = ',';
    for (const char* p = list; *p && n + 2 < NAMES_MAX; p++) {
        if (*p == ' ' || *p == '\t') continue;
        if (*p == ',' && c->names[n - 1] == ',') continue;
        c->names[n++] = *p;
    }
    if (c->names[n - 1] != ',') c->names[n++] = ',';
    c->names[n] = 0;
}

/* One entry of ",a,b*," against a name: whole, or its start for "b*". */
static int entry_matches(const char* e, size_t len, const char* name) {
    if (len && e[len - 1] == '*') return strncmp(name, e, len - 1) == 0;
    return strlen(name) == len && strncmp(name, e, len) == 0;
}

/* The name as given, or what follows its last '/' (an event's bank path). */
static int names_has(const Clip* c, const char* name) {
    const char* base = strrchr(name, '/');
    for (const char* e = c->names + 1; *e; ) {
        const char* end = strchr(e, ',');
        if (!end) break;
        size_t len = (size_t)(end - e);
        if (len && (entry_matches(e, len, name) || (base && entry_matches(e, len, base + 1)))) return 1;
        e = end + 1;
    }
    return 0;
}

static void set_volume(Clip* c) {
    if (!c->loaded || !c->volume_key) return;
    float v = (float)strtod(setting(c->volume_key, option_default(c->volume_key)), NULL);
    for (int i = 0; i < VOICES; i++) ma_sound_set_volume(&c->voice[i], v);
}

static void unload(Clip* c) {
    if (!c->loaded) return;
    for (int i = 0; i < VOICES; i++) ma_sound_uninit(&c->voice[i]);
    c->loaded = 0;
}

static void load(Clip* c) {
    unload(c);
    if (!engine_ok || !c->key) return;
    const char* file = setting(c->key, option_default(c->key));
    char path[2048];
    if (file[0] == '/' || file[0] == '\\' || (file[0] && file[1] == ':'))
        snprintf(path, sizeof path, "%s", file);
    else
        snprintf(path, sizeof path, "%s/%s", dir, file);
    /* Decoded once into memory; the other voices share it. */
    if (ma_sound_init_from_file(&engine, path, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, NULL,
                                &c->voice[0]) != MA_SUCCESS) {
        api->log("hitmarker: cannot load %s", path);
        return;
    }
    int i = 1;
    for (; i < VOICES; i++)
        if (ma_sound_init_copy(&engine, &c->voice[0], MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, &c->voice[i]) != MA_SUCCESS)
            break;
    if (i < VOICES) {
        while (--i >= 0) ma_sound_uninit(&c->voice[i]);
        api->log("hitmarker: cannot make voices for %s", path);
        return;
    }
    c->loaded = 1;
    set_volume(c);
    api->log("hitmarker: %s = %s", c->key, path);
}

static void play(Clip* c) {
    if (!c->loaded) return;
    ma_sound* v = &c->voice[c->next++ % VOICES];  /* the oldest voice */
    ma_sound_stop(v);
    ma_sound_seek_to_pcm_frame(v, 0);
    ma_sound_start(v);
}

static void apply_settings(void) {
    set_volume(&clips[HEADSHOT]);
    set_volume(&clips[HIT]);
    for (int i = 0; i < CLIPS; i++) set_names(&clips[i], setting(clips[i].names_key, option_default(clips[i].names_key)));
}

/* The play function: the event's name is the start of its second argument.
 * A name of ours answers with an empty handle (as the game's own does with
 * its sound off) and plays our clip. */
static int on_play(BbHookCtx* ctx, void* user) {
    (void)user;
    const char* name = (const char*)(uintptr_t)ctx->arg[1];
    if (!name) return 0;
    char safe[64];
    size_t n = 0;
    while (n + 1 < sizeof safe && name[n] >= 0x20 && name[n] < 0x7f) {
        safe[n] = name[n];
        n++;
    }
    safe[n] = 0;

    int which = -1, skip = 0;
    ma_mutex_lock(&lock);
    for (int i = 0; i < CLIPS && which < 0; i++)
        if (names_has(&clips[i], safe)) which = i;
    const int armed = frame - last_shot <= SHOT_FRAMES;
    if (!armed && (which == HEADSHOT || which == HIT)) which = -1;
    const int near_hit = frame - last_hit <= NEAR_FRAMES, near_headshot = frame - last_headshot <= NEAR_FRAMES;
    switch (which) {
    case HEADSHOT:
        if (near_hit)
            for (int v = 0; v < VOICES && clips[HIT].loaded; v++) ma_sound_stop(&clips[HIT].voice[v]);
        play(&clips[HEADSHOT]);
        last_headshot = frame;
        skip = 1;
        break;
    case HIT:
        if (!near_headshot) play(&clips[HIT]);
        last_hit = frame;
        skip = 1;
        break;
    case MUTE: skip = 1; break;
    case SHOT: last_shot = frame; break;
    case AFTER_HIT: skip = near_hit || near_headshot; break;
    }
    ma_mutex_unlock(&lock);
    if (!skip) return 0;

    uint32_t empty[2] = {0, 0};
    api->read(NO_SOUND_ID, &empty[0], 4);
    api->read(NO_SOUND_PAIR, &empty[1], 4);
    uint32_t* handle = (uint32_t*)(uintptr_t)ctx->arg[0];
    if (handle) {
        handle[0] = empty[0];
        handle[1] = empty[1];
    }
    ctx->ret = ctx->arg[0];
    return 1;
}

static void on_frame(void* user) {
    (void)user;
    frame++;
}

static void on_option(const char* key, const char* value, void* user) {
    (void)value;
    (void)user;
    ma_mutex_lock(&lock);
    apply_settings();
    if (strcmp(key, "headshot_sound") == 0) load(&clips[HEADSHOT]);
    if (strcmp(key, "hit_sound") == 0) load(&clips[HIT]);
    ma_mutex_unlock(&lock);
}

BB_PLUGIN_EXPORT int bb_plugin_image(const BbHostApi* host) {
    api = host;
    if (api->version < 9) {
        api->log("hitmarker: needs plugin API 9 or newer (this bbhost has %u)", api->version);
        return 1;
    }
    if (!api->eboot_is_109()) {
        api->log("hitmarker: needs the 1.09 eboot");
        return 1;
    }
    snprintf(dir, sizeof dir, "%s", api->plugin_dir("hitmarker"));

    ma_mutex_init(&lock);
    engine_ok = ma_engine_init(NULL, &engine) == MA_SUCCESS;
    if (!engine_ok) api->log("hitmarker: no sound device; the game's sounds stay");
    apply_settings();
    load(&clips[HEADSHOT]);
    load(&clips[HIT]);

    /* Both entry points: push rbp; mov rbp,rsp; push r15/r14/(r13)/r12/rbx; sub rsp,N */
    static const uint8_t pro_a[15] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                      0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x30};
    static const uint8_t pro_b[17] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                      0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18};
    int a = api->hook2(PLAY_NAMED, pro_a, sizeof pro_a, on_play, NULL);
    int b = api->hook2(PLAY_NAMED_B, pro_b, sizeof pro_b, on_play, NULL);
    api->log("hitmarker: hooks %d %d (0 = in place), sounds from %s", a, b, dir);
    if (a != 0 && b != 0) return 1;

    api->on_frame(on_frame, NULL);
    api->on_option("hitmarker", on_option, NULL);
    return 0;
}
