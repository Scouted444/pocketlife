// PocketLife - the lot: walls, floors, objects, pathfinding
#include "game.h"
#include <string.h>

World W;
static s8 occ[LOT_H][LOT_W];     // blocking object index, -1 none
static s8 occWalk[LOT_H][LOT_W]; // walkable object (rug) index, -1 none

static int defByModel(int mdl) {
    for (int i = 0; i < g_objDefCount; i++)
        if (g_objDefs[i].model == mdl && !(g_objDefs[i].flags & OF_NOBUY)) return i;
    return -1;
}

void obj_footprint(const PlacedObj* o, int* w, int* d) {
    const ModelInfo* m = &g_models[g_objDefs[o->def].model];
    if (o->rot & 1) { *w = m->tilesD; *d = m->tilesW; }
    else            { *w = m->tilesW; *d = m->tilesD; }
}

void world_rebuildOcc(void) {
    memset(occ, -1, sizeof(occ));
    memset(occWalk, -1, sizeof(occWalk));
    for (int i = 0; i < W.nobj; i++) {
        PlacedObj* o = &W.obj[i];
        int w, d;
        obj_footprint(o, &w, &d);
        bool walk = g_objDefs[o->def].flags & OF_WALKABLE;
        for (int z = o->z; z < o->z + d; z++)
            for (int x = o->x; x < o->x + w; x++)
                if (x >= 0 && z >= 0 && x < LOT_W && z < LOT_H) {
                    if (walk) occWalk[z][x] = i; else occ[z][x] = i;
                }
    }
}

int world_objAt(int x, int z, bool includeWalkable) {
    if (x < 0 || z < 0 || x >= LOT_W || z >= LOT_H) return -1;
    if (occ[z][x] >= 0) return occ[z][x];
    if (includeWalkable) return occWalk[z][x];
    return -1;
}

bool world_walkable(int x, int z) {
    if (x < 0 || z < 0 || x >= LOT_W || z >= LOT_H) return false;
    return occ[z][x] < 0;
}

static bool edgeOpen(int x, int z, int dx, int dz) {
    u8 e;
    if (dx == 1)       e = W.vw[z][x + 1];
    else if (dx == -1) e = W.vw[z][x];
    else if (dz == 1)  e = W.hw[z + 1][x];
    else               e = W.hw[z][x];
    return e == W_NONE || e == W_DOOR;
}

// can a sim step from (x,z) to (x+dx,z+dz)?
bool world_canStep(int x, int z, int dx, int dz) {
    int nx = x + dx, nz = z + dz;
    if (!world_walkable(nx, nz)) return false;
    if (dx && dz) {
        // no corner cutting: both orthogonal routes must be open
        if (!world_walkable(x + dx, z) || !world_walkable(x, z + dz)) return false;
        if (!edgeOpen(x, z, dx, 0) || !edgeOpen(x + dx, z, 0, dz)) return false;
        if (!edgeOpen(x, z, 0, dz) || !edgeOpen(x, z + dz, dx, 0)) return false;
        // don't squeeze diagonally through a doorway
        if (W.vw[z][x + (dx > 0)] == W_DOOR || W.hw[z + (dz > 0)][x] == W_DOOR) return false;
        if (W.vw[nz][x + (dx > 0)] == W_DOOR || W.hw[z + (dz > 0)][nx] == W_DOOR) return false;
        return true;
    }
    return edgeOpen(x, z, dx, dz);
}

// ---------------------------------------------------------------- BFS
static u16 dist[LOT_H][LOT_W];
static s8 par[LOT_H][LOT_W];
static const s8 DIRS[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };

static void bfs(int sx, int sz) {
    static s16 qx[LOT_W * LOT_H], qz[LOT_W * LOT_H];
    memset(dist, 0xFF, sizeof(dist));
    int h = 0, t = 0;
    if (sx < 0 || sz < 0 || sx >= LOT_W || sz >= LOT_H) return;
    dist[sz][sx] = 0;
    par[sz][sx] = -1;
    qx[t] = sx; qz[t++] = sz;
    while (h < t) {
        int x = qx[h], z = qz[h++];
        for (int d = 0; d < 8; d++) {
            int nx = x + DIRS[d][0], nz = z + DIRS[d][1];
            if (nx < 0 || nz < 0 || nx >= LOT_W || nz >= LOT_H) continue;
            u16 cost = dist[z][x] + (d < 4 ? 10 : 14);
            if (dist[nz][nx] <= cost) continue;
            if (!world_canStep(x, z, DIRS[d][0], DIRS[d][1])) continue;
            if (dist[nz][nx] == 0xFFFF) { qx[t] = nx; qz[t++] = nz; }
            dist[nz][nx] = cost;
            par[nz][nx] = d;
            if (t >= LOT_W * LOT_H) t = LOT_W * LOT_H - 1;
        }
    }
}

static int buildPath(int sx, int sz, int tx, int tz, s8 out[][2], int max) {
    if (dist[tz][tx] == 0xFFFF) return -1;
    s8 tmp[MAX_PATH][2];
    int n = 0, x = tx, z = tz;
    while ((x != sx || z != sz) && n < MAX_PATH) {
        tmp[n][0] = x; tmp[n][1] = z; n++;
        int d = par[z][x];
        if (d < 0) break;
        x -= DIRS[d][0]; z -= DIRS[d][1];
    }
    if (n > max) n = max;
    for (int i = 0; i < n; i++) { out[i][0] = tmp[n - 1 - i][0]; out[i][1] = tmp[n - 1 - i][1]; }
    return n;
}

int world_findPath(int sx, int sz, int tx, int tz, s8 out[][2], int max) {
    // Sim may be standing on a blocked tile (e.g. just got off a bed): let BFS start anyway
    bfs(sx, sz);
    return buildPath(sx, sz, tx, tz, out, max);
}

// pick the best reachable tile to use object oi from. returns false if none.
bool world_useSpot(int oi, int fromX, int fromZ, int* outX, int* outZ) {
    PlacedObj* o = &W.obj[oi];
    int w, d;
    obj_footprint(o, &w, &d);
    bfs(fromX, fromZ);
    static const s8 F[4][2] = { {0,1},{1,0},{0,-1},{-1,0} };
    int best = 0x7FFFFFFF, bx = -1, bz = -1;
    // walk the ring of tiles around the footprint
    for (int z = o->z - 1; z <= o->z + d; z++)
        for (int x = o->x - 1; x <= o->x + w; x++) {
            bool inX = x >= o->x && x < o->x + w, inZ = z >= o->z && z < o->z + d;
            if (inX && inZ) continue;
            if (!inX && !inZ) continue;   // no diagonal corners
            if (x < 0 || z < 0 || x >= LOT_W || z >= LOT_H) continue;
            if (dist[z][x] == 0xFFFF) continue;
            // which way from this tile is the object, and is the edge open?
            int dx = inX ? 0 : (x < o->x ? 1 : -1);
            int dz = inZ ? 0 : (z < o->z ? 1 : -1);
            if (!edgeOpen(x, z, dx, dz)) continue;
            int score = dist[z][x];
            // facing direction of the side we're on
            int side = (dz == -1) ? 0 : (dx == -1) ? 1 : (dz == 1) ? 2 : 3;
            if (side != o->rot) score += 400;   // strongly prefer the front
            (void)F;
            if (score < best) { best = score; bx = x; bz = z; }
        }
    if (bx < 0) return false;
    *outX = bx; *outZ = bz;
    return true;
}

bool world_canPlace(int def, int x, int z, int rot, int ignore) {
    PlacedObj t = { (u8)def, (s8)x, (s8)z, (u8)rot, 0 };
    int w, d;
    obj_footprint(&t, &w, &d);
    bool walk = g_objDefs[def].flags & OF_WALKABLE;
    for (int zz = z; zz < z + d; zz++)
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || zz < 0 || xx >= LOT_W || zz >= LOT_H) return false;
            int a = occ[zz][xx];
            if (a >= 0 && a != ignore) return false;
            if (walk && occWalk[zz][xx] >= 0 && occWalk[zz][xx] != ignore) return false;
            // no walls running through the middle of an object
            if (xx > x && W.vw[zz][xx] != W_NONE) return false;
            if (zz > z && W.hw[zz][xx] != W_NONE) return false;
        }
    // don't drop things on sims
    if (!walk)
        for (int i = 0; i < W.nsim; i++) {
            int sx = (int)W.sim[i].x, sz = (int)W.sim[i].z;
            if (sx >= x && sx < x + w && sz >= z && sz < z + d) return false;
        }
    return true;
}

int world_addObj(int def, int x, int z, int rot) {
    if (def < 0 || W.nobj >= MAX_OBJS) return -1;
    if (!world_canPlace(def, x, z, rot, -1)) return -1;
    PlacedObj* o = &W.obj[W.nobj];
    o->def = def; o->x = x; o->z = z; o->rot = rot & 3; o->user = 0;
    W.nobj++;
    world_rebuildOcc();
    return W.nobj - 1;
}

void world_removeObj(int i) {
    if (i < 0 || i >= W.nobj) return;
    // fix up any sims that reference objects
    for (int s = 0; s < W.nsim; s++) {
        Sim* sm = &W.sim[s];
        for (int k = 0; k < sm->qlen; k++) {
            if (sm->q[k].obj == i) sm->q[k].obj = -2;          // invalid, will be dropped
            else if (sm->q[k].obj > i) sm->q[k].obj--;
        }
    }
    for (int k = i; k < W.nobj - 1; k++) W.obj[k] = W.obj[k + 1];
    W.nobj--;
    world_rebuildOcc();
}

void world_setWall(int horiz, int x, int z, u8 type) {
    if (horiz) { if (x >= 0 && x < LOT_W && z >= 0 && z <= LOT_H) W.hw[z][x] = type; }
    else       { if (x >= 0 && x <= LOT_W && z >= 0 && z < LOT_H) W.vw[z][x] = type; }
}
u8 world_getWall(int horiz, int x, int z) {
    if (horiz) return (x >= 0 && x < LOT_W && z >= 0 && z <= LOT_H) ? W.hw[z][x] : 0;
    return (x >= 0 && x <= LOT_W && z >= 0 && z < LOT_H) ? W.vw[z][x] : 0;
}

int world_polyEstimate(void) {
    int p = 0;
    for (int z = 0; z <= LOT_H; z++)
        for (int x = 0; x <= LOT_W; x++) {
            if (x < LOT_W && W.hw[z][x]) p += g_models[MDL_WALL - 1 + W.hw[z][x]].polys;
            if (z < LOT_H && W.vw[z][x]) p += g_models[MDL_WALL - 1 + W.vw[z][x]].polys;
        }
    for (int i = 0; i < W.nobj; i++) p += g_models[g_objDefs[W.obj[i].def].model].polys;
    // roughly 40% of a closed mesh faces away from the camera and is culled
    return p * 6 / 10 + 160;   // + floors, sims, cursor
}

// ---------------------------------------------------------------- starter house
static void hwall(int z, int x0, int x1, u8 t) { for (int x = x0; x <= x1; x++) W.hw[z][x] = t; }
static void vwall(int x, int z0, int z1, u8 t) { for (int z = z0; z <= z1; z++) W.vw[z][x] = t; }
static void floorRect(int x0, int z0, int x1, int z1, u8 f) {
    for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) W.floor[z][x] = f;
}
static void put(int mdl, int x, int z, int rot) {
    int d = defByModel(mdl);
    if (d < 0) return;
    if (world_addObj(d, x, z, rot) >= 0) return;
    // the real model might be bigger than planned: try nudging it
    static const s8 nudge[][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1} };
    for (int i = 0; i < 8; i++)
        if (world_addObj(d, x + nudge[i][0], z + nudge[i][1], rot) >= 0) return;
}

void world_newGame(void) {
    memset(&W, 0, sizeof(W));
    // the house: tiles x 2..13, z 1..8
    floorRect(2, 1, 13, 8, FL_WOOD);
    floorRect(2, 1, 5, 4, FL_TILE);     // bathroom
    floorRect(10, 1, 13, 4, FL_CARPET); // bedroom
    floorRect(2, 5, 5, 8, FL_TILE);     // kitchen
    floorRect(7, 9, 7, 11, FL_STONE);   // front path
    hwall(1, 2, 13, W_WALL);
    hwall(9, 2, 13, W_WALL);
    vwall(2, 1, 8, W_WALL);
    vwall(14, 1, 8, W_WALL);
    vwall(6, 1, 4, W_WALL);             // bathroom
    hwall(5, 2, 5, W_WALL);
    vwall(10, 1, 4, W_WALL);            // bedroom
    hwall(5, 10, 13, W_WALL);
    W.hw[5][3] = W_DOOR;
    W.hw[5][11] = W_DOOR;
    W.hw[9][7] = W_DOOR;                // front door
    W.hw[9][4] = W_WINDOW; W.hw[9][11] = W_WINDOW; W.hw[9][12] = W_WINDOW;
    W.hw[1][7] = W_WINDOW; W.hw[1][8] = W_WINDOW; W.hw[1][12] = W_WINDOW;
    W.vw[3][2] = W_WINDOW; W.vw[6][14] = W_WINDOW; W.vw[7][2] = W_WINDOW;
    world_rebuildOcc();

    // bathroom
    put(MDL_TOILET, 2, 1, 0);
    put(MDL_BSINK, 3, 1, 0);
    put(MDL_SHOWER, 5, 1, 0);
    // bedroom
    put(MDL_BED_DOUBLE, 11, 1, 0);
    put(MDL_SIDETABLE, 10, 1, 0);
    put(MDL_LAMP, 13, 1, 0);
    put(MDL_DRESSER, 13, 3, 3);
    // dining
    put(MDL_TABLE, 7, 2, 0);
    put(MDL_CHAIR, 7, 1, 0);
    put(MDL_CHAIR, 7, 3, 2);
    put(MDL_BOOKCASE, 6, 1, 0);
    put(MDL_PLANT, 9, 1, 0);
    // kitchen
    put(MDL_FRIDGE, 2, 5, 1);
    put(MDL_STOVE, 2, 6, 1);
    put(MDL_COUNTER, 2, 7, 1);
    put(MDL_KSINK, 2, 8, 1);
    put(MDL_TRASH, 5, 8, 0);
    // living room
    put(MDL_TV, 10, 5, 0);
    put(MDL_RUG, 10, 6, 0);
    put(MDL_SOFA, 10, 8, 2);
    put(MDL_COMPUTER, 13, 7, 3);
    put(MDL_ARMCHAIR, 8, 6, 1);
    put(MDL_RADIO, 6, 5, 0);
    put(MDL_PLANT, 13, 5, 0);

    W.money = 1500;
    W.minutes = 8 * 60;      // day 1, 8am
    W.lastBillDay = 0;
}
