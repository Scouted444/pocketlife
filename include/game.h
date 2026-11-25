// PocketLife - shared game definitions
#pragma once
#include <nds.h>
#include <stdbool.h>
#include "gen_assets.h"

#define LOT_W 16
#define LOT_H 12
#define MAX_OBJS 48
#define MAX_SIMS 2
#define MAX_QUEUE 4
#define MAX_PATH 96
#define POLY_BUDGET 1850

// ---------------------------------------------------------------- needs
enum { NEED_HUNGER, NEED_ENERGY, NEED_BLADDER, NEED_HYGIENE, NEED_FUN, NEED_SOCIAL, NEED_COUNT };
enum { SKILL_COOKING, SKILL_LOGIC, SKILL_CHARISMA, SKILL_COUNT };

// how a Sim physically uses an object
enum { USE_FACE, USE_SIT, USE_LIE, USE_STAND_ON, USE_SOCIAL, USE_NONE };
enum { POSE_STAND, POSE_SIT, POSE_LIE, POSE_DANCE, POSE_TALK, POSE_USE, POSE_WAVE };

enum {
    ACT_NONE, ACT_SNACK, ACT_COOK, ACT_SLEEP, ACT_NAP, ACT_TOILET, ACT_SHOWER, ACT_BATH,
    ACT_WASH, ACT_SPEECH, ACT_TV, ACT_RELAX, ACT_WORK, ACT_GAMES, ACT_CHATNET, ACT_READ,
    ACT_DANCE, ACT_WATER, ACT_LAUNDRY, ACT_EAT_TABLE, ACT_GOTO, ACT_CHAT, ACT_JOKE, ACT_HUG,
    ACT_COUNT
};

typedef struct {
    const char* name;
    s16 rate[NEED_COUNT];   // need change per game hour (on top of normal decay)
    u16 minutes;           // max duration in game minutes
    s8 endNeed;            // stop early when this need is full (-1 = never)
    u8 use;                // USE_*
    u8 pose;               // POSE_*
    s8 skill;              // SKILL_* gained (-1 none)
    s8 moneyPerHour;       // + earns, - costs
} ActionDef;

// ---------------------------------------------------------------- objects
enum { CAT_KITCHEN, CAT_BATH, CAT_BED, CAT_LIVING, CAT_FUN, CAT_DECOR, CAT_COUNT };
#define OF_WALKABLE 1
#define OF_NOBUY    2

typedef struct {
    const char* name;
    u8 model;
    u16 price;
    u8 cat;
    u8 flags;
    u8 actions[4];
} ObjDef;

typedef struct {
    u8 def;      // index into g_objDefs
    s8 x, z;     // top-left tile of footprint
    u8 rot;      // 0..3, front faces +z,+x,-z,-x
    u8 user;     // bitmask of sims using it
} PlacedObj;

// ---------------------------------------------------------------- walls / floors
enum { W_NONE, W_WALL, W_WINDOW, W_DOOR };
enum { FL_GRASS, FL_WOOD, FL_TILE, FL_CARPET, FL_STONE, FL_COUNT };

// ---------------------------------------------------------------- sims
enum { SS_IDLE, SS_WALK, SS_ACT };

typedef struct { u8 act; s8 obj; s8 sim; s8 tx, tz; } QItem;

typedef struct {
    char name[12];
    u8 female;
    u8 hairStyle, top;     // CP hair 0..5, 0 hoodie / 1 tee
    u16 skin, hair, shirt, pants, shoes;
    float x, z, y, face;
    float need[NEED_COUNT];
    float skill[SKILL_COUNT];
    float rel;               // relationship with the other sim, -100..100
    u8 state;
    QItem q[MAX_QUEUE];
    u8 qlen;
    s8 path[MAX_PATH][2];
    u8 plen, pidx;
    float actMin;            // minutes spent in current action
    float anim;
    u8 pose;
    float homeX, homeZ;      // where to step back to after sit/lie
    float idleT;
    u16 earned;
} Sim;

// ---------------------------------------------------------------- world state
typedef struct {
    u8 floor[LOT_H][LOT_W];
    u8 hw[LOT_H + 1][LOT_W];   // edge along x on the north side of tile (x,z)
    u8 vw[LOT_H][LOT_W + 1];   // edge along z on the west side of tile (x,z)
    PlacedObj obj[MAX_OBJS];
    u8 nobj;
    Sim sim[MAX_SIMS];
    u8 nsim;
    s32 money;
    u32 minutes;               // game minutes since day 1 00:00
    u32 lastBillDay;
    u8 active;
} World;

extern World W;
extern const ActionDef g_actions[ACT_COUNT];
extern const ObjDef g_objDefs[];
extern const int g_objDefCount;
extern const char* const g_catNames[CAT_COUNT];
extern const char* const g_needNames[NEED_COUNT];
extern const char* const g_skillNames[SKILL_COUNT];

// options
typedef struct {
    u8 camTop;       // 1 = top-down, 0 = 2.5D
    u8 camRot;       // 0..3
    u8 zoom;         // 0..2
    u8 walls;        // 0 up, 1 cutaway, 2 down
    u8 speed;        // 0 pause, 1..3
    u8 freeWill;
    u8 outline;
} Options;
extern Options OPT;

// ---------------------------------------------------------------- world.c
void world_newGame(void);
void world_rebuildOcc(void);
int  world_objAt(int x, int z, bool includeWalkable);
void obj_footprint(const PlacedObj* o, int* w, int* d);
bool world_canPlace(int def, int x, int z, int rot, int ignore);
int  world_addObj(int def, int x, int z, int rot);
void world_removeObj(int i);
bool world_canStep(int x, int z, int dx, int dz);
bool world_walkable(int x, int z);
int  world_findPath(int sx, int sz, int tx, int tz, s8 out[][2], int max);
bool world_useSpot(int oi, int fromX, int fromZ, int* outX, int* outZ);
int  world_polyEstimate(void);
void world_setWall(int horiz, int x, int z, u8 type);
u8   world_getWall(int horiz, int x, int z);

// ---------------------------------------------------------------- sims.c
void sim_init(Sim* s, const char* name, int female, u16 skin, u16 hair, u16 shirt, u16 pants, float x, float z);
void sim_setLook(Sim* s, int hairStyle, int top, u16 shoes);
void sim_update(int i, float gameMinutes, float realStep);
bool sim_queue(int i, int act, int obj, int other, int tx, int tz);
void sim_cancel(int i);
float sim_mood(const Sim* s);
const char* sim_actionLabel(const Sim* s);
void notify(const char* fmt, ...);
extern char g_notice[64];
extern int g_noticeTimer;

// ---------------------------------------------------------------- render.c
void render_init(void);
void render_scene(int mode, int cx, int cz, int heldDef, int heldRot, int buildEdge, bool ghostOk);
void render_cas(const Sim* s, float spin);
void cam_update(float targetX, float targetZ);
void cam_screenDir(int pad, int* dx, int* dz);
int  render_cullMode(void);
extern int g_lastPolys;

// ---------------------------------------------------------------- ui.c
enum { MODE_LIVE, MODE_BUY, MODE_BUILD };
void ui_init(void);
void ui_dirty(void);

// ---------------------------------------------------------------- save.c
bool save_init(void);
bool save_write(void);
bool save_read(void);
bool save_exists(void);
