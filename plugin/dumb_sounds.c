/* Dumb Sounds (dumb_sounds): a bbhost plugin that plays dumb sounds in
 * place of the game's: the CoD hit marker on a bullet's impact, the Hunt:
 * Showdown headshot, a parry and Beast Roar, each picked in F9.
 *
 * The game plays its sounds by event name ("c999999990", "z006021058", ...)
 * through one function, which the plugin hooks. Its rules (sounds.txt in the
 * plugin's folder, below) name events and the files to play for them: for a
 * rule's event the plugin answers the call as the game does with its sound
 * turned off (an empty handle), so the game's sound never starts, and plays
 * its own file through miniaudio, on a device of its own.
 *
 * Settings ([dumb_sounds] in bbhost.toml, or F9 in game):
 *   volume       0-2, every sound of the plugin's
 *   log_sounds   every event the game plays, by name, to sounds.log in the
 *                plugin's folder, to find the names for a rule
 *   reload       an action: sounds.txt read again
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

#include <ctype.h>
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
#define MAX_CLIPS 32
#define MAX_RULES 64
#define TEXT_MAX 256
/* "Just after" a rule's sound: the game's own sounds for the same moment
 * come within a frame or two of each other. */
#define NEAR_FRAMES 5
/* A bullet lands 0-30 frames after its shot in the logs; a far one later. */
#define SHOT_FRAMES 60

static const char DEFAULT_RULES[] =
    "# Dumb Sounds: the game's sound events and what plays in their place.\n"
    "# One rule a line; the first rule whose event matches is the one used.\n"
    "#\n"
    "#   NAME  EVENT  FILE  [VOLUME]  [OPTIONS]\n"
    "#\n"
    "# NAME     yours, one word (sounds.log shows it). parry, headshot, hit and\n"
    "#          beast_roar are picked in F9 > Dumb Sounds (which sound, or Off),\n"
    "#          and their volume there, once moved, replaces VOLUME here\n"
    "# EVENT    the game's sound event (F9 > Dumb Sounds > Log sounds writes every\n"
    "#          one the game plays to sounds.log); a name ending in * covers\n"
    "#          every name it starts\n"
    "# FILE     a WAV, MP3 or FLAC in this folder, or a full path; - silences\n"
    "#          the event with nothing in its place\n"
    "# VOLUME   0-2, 1 the file as it is (the default)\n"
    "# OPTIONS  shot            only in the second after a gunshot (the shot\n"
    "#                          line's events), so an enemy's blow on you, which\n"
    "#                          plays the same impact events, keeps its sound\n"
    "#          priority=N      a higher rule's sound cuts off a lower one's from\n"
    "#                          the same moment, and blocks it just after\n"
    "#          mute=a,b        these events are silenced just after this one\n"
    "#\n"
    "# A rule whose file does not load leaves the game's sound alone.\n"
    "\n"
    "# A hunter's gunshot.\n"
    "shot c000000720\n"
    "\n"
    "# A gun parry: the stagger.\n"
    "parry     c999999991  parry.wav     priority=3\n"
    "# A bullet on a hunter's head; c999999990 is the game's own headshot sound.\n"
    "headshot  z006021060  headshot.wav  shot priority=2 mute=s000006001,c999999990\n"
    "# A bullet on a body (z006020* is a wall or the ground); s000006001 the\n"
    "# pop every bullet's impact makes.\n"
    "hit       z006021*    hit.wav       shot priority=1 mute=s000006001\n"
    "# The Beast Roar hunter tool's roar (z005000027 is its blast landing, a\n"
    "# moment later).\n"
    "beast_roar  s000202002  link-fall-down.wav\n";

typedef struct {
    char file[TEXT_MAX];
    ma_sound voice[VOICES];
    int loaded;
    unsigned next;
} Clip;

typedef struct {
    char name[32];
    char event[64];
    char file[TEXT_MAX]; /* sounds.txt's; "-" silence */
    int clip;            /* -1: silence, -2: no sound (the game's stays) */
    float file_volume;   /* sounds.txt's */
    float volume;        /* F9's for the name, when set; else file_volume */
    int shot, priority;
    char mute[TEXT_MAX];     /* ",a,b," */
    unsigned fired;      /* the frame it last played */
} Rule;

/* The built-in rules' sounds, picked in F9 (<name>_sound); the files are
 * leveled to play at volume 1 (tools/level-sounds.sh). "Custom" plays
 * custom_<name>.wav, the player's to overwrite with their own: the plugin
 * makes it, a copy of the rule's first sound, when it is missing, so an
 * update that brings new files leaves a player's own alone. "Off" leaves
 * the game's own sound. */
typedef struct {
    const char* rule;
    const char* label;
    const char* file;
} Choice;

static const Choice CHOICES[] = {
    {"hit", "CoD", "hit.wav"},
    {"hit", "Custom", "custom_hit.wav"},
    {"headshot", "Hunt", "headshot.wav"},
    {"headshot", "Custom", "custom_headshot.wav"},
    {"parry", "Chapelle", "parry.wav"},
    {"parry", "MGS alert", "mgs-alert.wav"},
    {"parry", "Custom", "custom_parry.wav"},
    {"beast_roar", "Link", "link-fall-down.wav"},
    {"beast_roar", "Mario hoo-hoo", "sm64_mario_hoohoo.wav"},
    {"beast_roar", "Mario hurt", "sm64_mario_hurt.wav"},
    {"beast_roar", "Mario oof", "sm64_mario_oof.wav"},
    {"beast_roar", "Mario whoa", "sm64_mario_whoa.wav"},
    {"beast_roar", "Windows XP error", "xp-error.wav"},
    {"beast_roar", "Custom", "custom_beast_roar.wav"},
};

static const BbHostApi* api;
static char dir[1024];
static unsigned frame;
static ma_engine engine;
static int engine_ok;
static ma_mutex lock;
static Clip clips[MAX_CLIPS];
static int nclips;
static Rule rules[MAX_RULES];
static int nrules;
static char shots[TEXT_MAX] = ",";  /* ",a,b," */
static unsigned last_shot = 0x80000000u;
static FILE* sound_log;
static int log_on;

BB_PLUGIN_EXPORT const BbPluginInfo bb_plugin_info = {
    BB_PLUGIN_API_VERSION, 0, "dumb_sounds", "Dumb Sounds", "0.1.0", "ambientmf",
    "Dumb sounds in place of the game's: the CoD hit marker on your bullets, the Hunt: Showdown headshot, a parry "
    "and Beast Roar, each picked in F9; sounds.txt in its folder names more.",
};

BB_PLUGIN_EXPORT const BbOption bb_plugin_options[] = {
    {BB_OPT_FLOAT | BB_OPT_LIVE, "volume", "Volume", "Every sound of the plugin's (sounds.txt sets each one's own).",
     "1.0", 0.0, 2.0},
    {BB_OPT_HEADING, "gun", "Gun",
     "A bullet on a body, on a hunter's head, and a gun parry. Custom: custom_<sound>.wav in the plugin's folder, "
     "yours to overwrite (then Reload sounds.txt). Off: the game's sound."},
    {BB_OPT_CHOICE | BB_OPT_LIVE, "hit_sound", "Hit marker", 0, "CoD", 0, 0, "CoD|Custom|Off"},
    {BB_OPT_FLOAT | BB_OPT_LIVE, "hit_volume", "Hit marker volume", 0, "1.0", 0.0, 2.0},
    {BB_OPT_CHOICE | BB_OPT_LIVE, "headshot_sound", "Headshot", 0, "Hunt", 0, 0, "Hunt|Custom|Off"},
    {BB_OPT_FLOAT | BB_OPT_LIVE, "headshot_volume", "Headshot volume", 0, "1.0", 0.0, 2.0},
    {BB_OPT_CHOICE | BB_OPT_LIVE, "parry_sound", "Parry", 0, "Chapelle", 0, 0, "Chapelle|MGS alert|Custom|Off"},
    {BB_OPT_FLOAT | BB_OPT_LIVE, "parry_volume", "Parry volume", 0, "1.0", 0.0, 2.0},
    {BB_OPT_HEADING, "tools", "Hunter tools"},
    {BB_OPT_CHOICE | BB_OPT_LIVE, "beast_roar_sound", "Beast Roar", 0, "Link", 0, 0,
     "Link|Mario hoo-hoo|Mario hurt|Mario oof|Mario whoa|Windows XP error|Custom|Off"},
    {BB_OPT_FLOAT | BB_OPT_LIVE, "beast_roar_volume", "Beast Roar volume", 0, "1.0", 0.0, 2.0},
    {BB_OPT_HEADING, "sounds", "Sounds"},
    {BB_OPT_BOOL | BB_OPT_LIVE, "log_sounds", "Log sounds",
     "Every sound event the game plays, by name, to sounds.log in the plugin's folder, to find the names for "
     "sounds.txt.",
     "false"},
    {BB_OPT_ACTION, "reload", "Reload sounds.txt"},
    {0},
};

static const char* setting(const char* key, const char* fallback) {
    char full[64];
    snprintf(full, sizeof full, "dumb_sounds.%s", key);
    const char* v = api->config(full);
    return v && v[0] ? v : fallback;
}

/* A name against a pattern: whole, or its start for "abc*". */
static int matches(const char* pat, size_t len, const char* name) {
    if (len && pat[len - 1] == '*') return strncmp(name, pat, len - 1) == 0;
    return strlen(name) == len && strncmp(name, pat, len) == 0;
}

/* The name, or what follows its last '/' (an event's bank path), against
 * one pattern or each of ",a,b,". */
static int name_is(const char* pat, const char* name) {
    const char* base = strrchr(name, '/');
    return matches(pat, strlen(pat), name) || (base && matches(pat, strlen(pat), base + 1));
}

static int list_has(const char* list, const char* name) {
    const char* base = strrchr(name, '/');
    for (const char* e = list + 1; *e;) {
        const char* end = strchr(e, ',');
        if (!end) break;
        size_t len = (size_t)(end - e);
        if (len && (matches(e, len, name) || (base && matches(e, len, base + 1)))) return 1;
        e = end + 1;
    }
    return 0;
}

/* ",a,b," from "a,b" */
static void set_list(char* out, size_t cap, const char* in) {
    snprintf(out, cap, ",%s,", in);
}

static void unload_all(void) {
    for (int i = 0; i < nclips; i++)
        if (clips[i].loaded)
            for (int v = 0; v < VOICES; v++) ma_sound_uninit(&clips[i].voice[v]);
    nclips = 0;
    nrules = 0;
    shots[0] = ',';
    shots[1] = 0;
}

/* A file's clip, loaded once for all the rules that name it; -2 when it
 * does not load. */
static int clip_for(const char* file) {
    for (int i = 0; i < nclips; i++)
        if (strcmp(clips[i].file, file) == 0) return clips[i].loaded ? i : -2;
    if (nclips == MAX_CLIPS || !engine_ok) return -2;
    Clip* c = &clips[nclips++];
    memset(c, 0, sizeof *c);
    snprintf(c->file, sizeof c->file, "%s", file);
    char path[2048];
    if (file[0] == '/' || file[0] == '\\' || (file[0] && file[1] == ':'))
        snprintf(path, sizeof path, "%s", file);
    else
        snprintf(path, sizeof path, "%s/%s", dir, file);
    /* Decoded once into memory; the other voices share it. */
    if (ma_sound_init_from_file(&engine, path, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, NULL,
                                &c->voice[0]) != MA_SUCCESS) {
        api->log("dumb_sounds: cannot load %s", path);
        return -2;
    }
    int v = 1;
    for (; v < VOICES; v++)
        if (ma_sound_init_copy(&engine, &c->voice[0], MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, &c->voice[v]) != MA_SUCCESS)
            break;
    if (v < VOICES) {
        while (--v >= 0) ma_sound_uninit(&c->voice[v]);
        api->log("dumb_sounds: cannot make voices for %s", path);
        return -2;
    }
    c->loaded = 1;
    return nclips - 1;
}

/* Each custom_<name>.wav that is missing, made from its rule's first sound. */
static void make_custom_files(void) {
    for (size_t c = 0; c < sizeof CHOICES / sizeof CHOICES[0]; c++) {
        if (strcmp(CHOICES[c].label, "Custom") != 0) continue;
        char to[1100], from[1100];
        snprintf(to, sizeof to, "%s/%s", dir, CHOICES[c].file);
        FILE* t = fopen(to, "rb");
        if (t) {
            fclose(t);
            continue;
        }
        const char* first = NULL;
        for (size_t k = 0; k < c && !first; k++)
            if (strcmp(CHOICES[k].rule, CHOICES[c].rule) == 0) first = CHOICES[k].file;
        if (!first) continue;
        snprintf(from, sizeof from, "%s/%s", dir, first);
        FILE* f = fopen(from, "rb");
        if (!f) continue;
        if ((t = fopen(to, "wb"))) {
            char buf[16384];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) fwrite(buf, 1, n, t);
            fclose(t);
            api->log("dumb_sounds: made %s from %s", CHOICES[c].file, first);
        }
        fclose(f);
    }
}

static void load_rules(void) {
    unload_all();
    char path[1100];
    snprintf(path, sizeof path, "%s/sounds.txt", dir);
    FILE* f = fopen(path, "r");
    if (!f) {
        if ((f = fopen(path, "w"))) {
            fputs(DEFAULT_RULES, f);
            fclose(f);
            api->log("dumb_sounds: wrote the default %s", path);
        }
        f = fopen(path, "r");
    }
    if (!f) {
        api->log("dumb_sounds: cannot read %s", path);
        return;
    }
    char line[1024];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        n++;
        char* hash = strchr(line, '#');
        if (hash) *hash = 0;
        char* tok[8];
        int nt = 0;
        for (char* t = strtok(line, " \t\r\n"); t && nt < 8; t = strtok(NULL, " \t\r\n")) tok[nt++] = t;
        if (nt == 0) continue;
        if (strcmp(tok[0], "shot") == 0) {
            for (int i = 1; i < nt; i++) {
                size_t len = strlen(shots);
                snprintf(shots + len, sizeof shots - len, "%s,", tok[i]);
            }
            continue;
        }
        if (nt < 3 || nrules == MAX_RULES) {
            api->log("dumb_sounds: sounds.txt line %d: a name, an event and a file", n);
            continue;
        }
        Rule* r = &rules[nrules];
        memset(r, 0, sizeof *r);
        snprintf(r->name, sizeof r->name, "%s", tok[0]);
        snprintf(r->event, sizeof r->event, "%s", tok[1]);
        r->file_volume = 1.0f;
        r->fired = 0x80000000u;
        r->mute[0] = ',';
        int i = 3;
        if (i < nt && (isdigit((unsigned char)tok[i][0]) || tok[i][0] == '.'))
            r->file_volume = (float)strtod(tok[i++], NULL);
        for (; i < nt; i++) {
            if (strcmp(tok[i], "shot") == 0) r->shot = 1;
            else if (strncmp(tok[i], "priority=", 9) == 0) r->priority = atoi(tok[i] + 9);
            else if (strncmp(tok[i], "mute=", 5) == 0) set_list(r->mute, sizeof r->mute, tok[i] + 5);
            else api->log("dumb_sounds: sounds.txt line %d: %s?", n, tok[i]);
        }
        snprintf(r->file, sizeof r->file, "%s", tok[2]);
        nrules++;
    }
    fclose(f);
    api->log("dumb_sounds: %d rules from %s", nrules, path);
}

static const char* option_default(const char* key) {
    for (const BbOption* o = bb_plugin_options; o->type; o++)
        if (o->key && strcmp(o->key, key) == 0) return o->def ? o->def : "";
    return "";
}

/* Each rule's sound and volume. A built-in rule plays the sound picked in
 * F9 (<name>_sound; "Off": none, the game's stays), another its file; the
 * volume is F9's for the name once set there, else the file's. A file that
 * does not load leaves the game's sound. */
static void rule_setup(void) {
    for (int i = 0; i < nrules; i++) {
        Rule* r = &rules[i];
        char key[48];
        const char* file = r->file;
        snprintf(key, sizeof key, "%s_sound", r->name);
        const char* pick = setting(key, option_default(key));
        if (strcmp(pick, "Off") == 0) {
            r->clip = -2;
            continue;
        }
        for (size_t c = 0; c < sizeof CHOICES / sizeof CHOICES[0]; c++)
            if (strcmp(CHOICES[c].rule, r->name) == 0 && strcmp(CHOICES[c].label, pick) == 0)
                file = CHOICES[c].file;
        r->clip = strcmp(file, "-") == 0 ? -1 : clip_for(file);
        snprintf(key, sizeof key, "%s_volume", r->name);
        const char* v = setting(key, "");
        r->volume = v[0] ? (float)strtod(v, NULL) : r->file_volume;
    }
}

static void apply_settings(void) {
    rule_setup();
    if (engine_ok) ma_engine_set_volume(&engine, (float)strtod(setting("volume", "1.0"), NULL));
    log_on = strcmp(setting("log_sounds", "false"), "true") == 0;
    if (log_on && !sound_log) {
        char path[1100];
        snprintf(path, sizeof path, "%s/sounds.log", dir);
        sound_log = fopen(path, "w");
    } else if (!log_on && sound_log) {
        fclose(sound_log);
        sound_log = NULL;
    }
}

static void play(Rule* r) {
    Clip* c = &clips[r->clip];
    ma_sound* v = &c->voice[c->next++ % VOICES];  /* the oldest voice */
    ma_sound_stop(v);
    ma_sound_set_volume(v, r->volume);
    ma_sound_seek_to_pcm_frame(v, 0);
    ma_sound_start(v);
}

/* Whether the game's sound `name` is skipped, and why: a rule's name (its
 * file played in its place), or "muted" (one a rule just before silences);
 * NULL keeps the game's sound. */
static const char* decide(const char* name) {
    if (list_has(shots, name)) last_shot = frame;
    for (int i = 0; i < nrules; i++)
        if (rules[i].clip != -2 && frame - rules[i].fired <= NEAR_FRAMES && list_has(rules[i].mute, name))
            return "muted";
    Rule* r = NULL;
    for (int i = 0; i < nrules && !r; i++)
        if (rules[i].clip != -2 && name_is(rules[i].event, name) &&
            (!rules[i].shot || frame - last_shot <= SHOT_FRAMES))
            r = &rules[i];
    if (!r) return NULL;
    int blocked = 0;
    for (int i = 0; i < nrules; i++) {
        Rule* o = &rules[i];
        if (o == r || frame - o->fired > NEAR_FRAMES) continue;
        if (o->priority > r->priority) blocked = 1;
        else if (o->priority < r->priority && o->clip >= 0 && o->clip != r->clip)
            for (int v = 0; v < VOICES; v++) ma_sound_stop(&clips[o->clip].voice[v]);
    }
    r->fired = frame;
    if (!blocked && r->clip >= 0) play(r);
    return r->name;
}

/* The play function: the event's name is the start of its second argument.
 * A skipped one is answered with an empty handle, as the game's own call
 * answers with its sound off. */
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

    ma_mutex_lock(&lock);
    const char* why = decide(safe);
    const int skip = why != NULL;
    if (sound_log) fprintf(sound_log, "%u %s%s%s\n", frame, safe, skip ? " -> " : "", skip ? why : "");
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
    if (sound_log && frame % 60 == 0) {
        ma_mutex_lock(&lock);
        if (sound_log) fflush(sound_log);
        ma_mutex_unlock(&lock);
    }
}

static void on_option(const char* key, const char* value, void* user) {
    (void)key;
    (void)value;
    (void)user;
    ma_mutex_lock(&lock);
    apply_settings();
    ma_mutex_unlock(&lock);
}

static void on_action(const char* action, void* user) {
    (void)user;
    if (strcmp(action, "reload") != 0) return;
    ma_mutex_lock(&lock);
    load_rules();
    rule_setup();
    ma_mutex_unlock(&lock);
    char text[64];
    snprintf(text, sizeof text, "Dumb Sounds: %d sounds", nrules);
    api->show_message(text, 2.0f);
}

BB_PLUGIN_EXPORT int bb_plugin_image(const BbHostApi* host) {
    api = host;
    if (api->version < 9) {
        api->log("dumb_sounds: needs plugin API 9 or newer (this bbhost has %u)", api->version);
        return 1;
    }
    if (!api->eboot_is_109()) {
        api->log("dumb_sounds: needs the 1.09 eboot");
        return 1;
    }
    snprintf(dir, sizeof dir, "%s", api->plugin_dir("dumb_sounds"));

    ma_mutex_init(&lock);
    engine_ok = ma_engine_init(NULL, &engine) == MA_SUCCESS;
    if (!engine_ok) api->log("dumb_sounds: no sound device; the game's sounds stay");
    make_custom_files();
    load_rules();
    apply_settings();

    /* Both entry points: push rbp; mov rbp,rsp; push r15/r14/(r13)/r12/rbx; sub rsp,N */
    static const uint8_t pro_a[15] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                      0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x30};
    static const uint8_t pro_b[17] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                      0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18};
    int a = api->hook2(PLAY_NAMED, pro_a, sizeof pro_a, on_play, NULL);
    int b = api->hook2(PLAY_NAMED_B, pro_b, sizeof pro_b, on_play, NULL);
    api->log("dumb_sounds: hooks %d %d (0 = in place), sounds from %s", a, b, dir);
    if (a != 0 && b != 0) return 1;

    api->on_frame(on_frame, NULL);
    api->on_option("dumb_sounds", on_option, NULL);
    api->on_action("dumb_sounds", on_action, NULL);
    return 0;
}
