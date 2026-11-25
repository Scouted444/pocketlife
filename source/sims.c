// PocketLife - Sim needs, actions and free will
#include "game.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char g_notice[64];
int g_noticeTimer;

void notify(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_notice, sizeof(g_notice), fmt, ap);
    va_end(ap);
    g_noticeTimer = 300;
    ui_dirty();
}

static const float DECAY[NEED_COUNT] = { -5.0f, -3.5f, -8.0f, -3.0f, -4.0f, -2.5f };
#define PASSIVE -2   // q.tx marker: sim is the listener in someone else's social action

float simHeight(void) { return f32tofloat(g_wallHeight) * 0.62f; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void sim_init(Sim* s, const char* name, int female, u16 skin, u16 hair, u16 shirt, u16 pants, float x, float z) {
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->female = female;
    s->skin = skin; s->hair = hair; s->shirt = shirt; s->pants = pants;
    s->x = x; s->z = z;
    for (int i = 0; i < NEED_COUNT; i++) s->need[i] = 70 + (rand() % 25);
    s->rel = 25;
    s->hairStyle = female ? 1 : 0;
    s->top = 0;
    s->shoes = RGB15(4, 4, 5);
}

void sim_setLook(Sim* s, int hairStyle, int top, u16 shoes) {
    s->hairStyle = hairStyle % CP_HAIR_STYLES; s->top = top & 1; s->shoes = shoes;
}

float sim_mood(const Sim* s) {
    float m = 0;
    for (int i = 0; i < NEED_COUNT; i++) m += s->need[i];
    return m / NEED_COUNT;
}

const char* sim_actionLabel(const Sim* s) {
    if (!s->qlen) return "Idle";
    const QItem* q = &s->q[0];
    if (q->tx == PASSIVE) return "Listening";
    if (s->state == SS_WALK && q->act != ACT_GOTO) return "Walking...";
    return g_actions[q->act].name;
}

static int capacity(int oi) {
    int m = g_objDefs[W.obj[oi].def].model;
    return (m == MDL_BED_DOUBLE || m == MDL_SOFA) ? 2 : 1;
}
static int usersOf(int oi) {
    int n = 0;
    for (int b = 0; b < MAX_SIMS; b++) if (W.obj[oi].user & (1 << b)) n++;
    return n;
}

static void objCenter(int oi, float* cx, float* cz) {
    int w, d;
    obj_footprint(&W.obj[oi], &w, &d);
    *cx = W.obj[oi].x + w * 0.5f;
    *cz = W.obj[oi].z + d * 0.5f;
}

static const s8 FRONT[4][2] = { {0,1},{1,0},{0,-1},{-1,0} };

static float yawTo(float dx, float dz) { return atan2f(dx, dz) * 57.29578f; }

// ---------------------------------------------------------------- starting / ending actions
static void endAction(int i, bool completed);

static bool startWalk(Sim* s, int tx, int tz) {
    int sx = (int)s->x, sz = (int)s->z;
    if (sx == tx && sz == tz) { s->plen = 0; s->pidx = 0; s->state = SS_WALK; return true; }
    int n = world_findPath(sx, sz, tx, tz, s->path, MAX_PATH);
    if (n < 0) return false;
    s->plen = n; s->pidx = 0;
    s->state = SS_WALK;
    return true;
}

static void popQueue(Sim* s) {
    if (!s->qlen) return;
    for (int k = 0; k < s->qlen - 1; k++) s->q[k] = s->q[k + 1];
    s->qlen--;
}

// begin the item at the front of the queue. returns false if it was dropped.
static bool beginItem(int i) {
    Sim* s = &W.sim[i];
    QItem* q = &s->q[0];
    const ActionDef* a = &g_actions[q->act];
    s->actMin = 0;
    s->earned = 0;
    if (q->tx == PASSIVE) { s->state = SS_ACT; s->pose = a->pose; return true; }
    if (q->act == ACT_GOTO) {
        if (!startWalk(s, q->tx, q->tz)) { notify("%s can't get there.", s->name); return false; }
        return true;
    }
    if (a->use == USE_SOCIAL) {
        if (q->sim < 0 || q->sim >= W.nsim) return false;
        Sim* o = &W.sim[q->sim];
        int ox = (int)o->x, oz = (int)o->z, sx = (int)s->x, sz = (int)s->z;
        if (abs(ox - sx) <= 1 && abs(oz - sz) <= 1 && !(ox == sx && oz == sz)) {
            s->plen = 0; s->pidx = 0; s->state = SS_WALK; return true;
        }
        // find a free tile next to them
        static const s8 N8[8][2] = { {0,1},{1,0},{0,-1},{-1,0},{1,1},{-1,1},{1,-1},{-1,-1} };
        int best = 9999, bx = -1, bz = -1;
        for (int k = 0; k < 8; k++) {
            int tx = ox + N8[k][0], tz = oz + N8[k][1];
            if (!world_walkable(tx, tz)) continue;
            if (k < 4 && !world_canStep(tx, tz, -N8[k][0], -N8[k][1]) && world_walkable(ox, oz)) continue;
            int n = world_findPath(sx, sz, tx, tz, s->path, MAX_PATH);
            if (n >= 0 && n < best) { best = n; bx = tx; bz = tz; }
        }
        if (bx < 0 || !startWalk(s, bx, bz)) { notify("%s can't reach %s.", s->name, o->name); return false; }
        return true;
    }
    if (q->obj == -1) {   // in-place action (passing out on the floor)
        s->state = SS_ACT; s->pose = a->pose; s->homeX = s->x; s->homeZ = s->z;
        return true;
    }
    if (q->obj < 0 || q->obj >= W.nobj) return false;
    if (usersOf(q->obj) >= capacity(q->obj) && !(W.obj[q->obj].user & (1 << i))) {
        notify("Someone is using the %s.", g_objDefs[W.obj[q->obj].def].name);
        return false;
    }
    if (a->moneyPerHour < 0) {
        int cost = -a->moneyPerHour;
        if (W.money < cost) { notify("Can't afford groceries (%d)!", cost); return false; }
    }
    int tx, tz;
    if (!world_useSpot(q->obj, (int)s->x, (int)s->z, &tx, &tz) || !startWalk(s, tx, tz)) {
        notify("%s can't reach the %s.", s->name, g_objDefs[W.obj[q->obj].def].name);
        return false;
    }
    q->tx = tx; q->tz = tz;
    return true;
}

static void startNext(int i) {
    Sim* s = &W.sim[i];
    while (s->qlen) {
        if (s->q[0].obj == -2) { popQueue(s); continue; }   // object was sold
        if (beginItem(i)) return;
        popQueue(s);
    }
    s->state = SS_IDLE;
    s->pose = POSE_STAND;
}

// sim reached the end of its path
static void arrive(int i) {
    Sim* s = &W.sim[i];
    QItem* q = &s->q[0];
    const ActionDef* a = &g_actions[q->act];
    if (q->act == ACT_GOTO) { popQueue(s); startNext(i); return; }

    if (a->use == USE_SOCIAL) {
        Sim* o = &W.sim[q->sim];
        float dx = o->x - s->x, dz = o->z - s->z;
        if (dx * dx + dz * dz > 2.6f) {      // they wandered off: chase (a couple of times)
            if (++q->tz > 3 || !beginItem(i)) { popQueue(s); startNext(i); }
            return;
        }
        // is the other sim available?
        bool busy = o->qlen && (o->q[0].act == ACT_SLEEP || o->q[0].act == ACT_NAP ||
                                o->q[0].act == ACT_TOILET || o->q[0].act == ACT_SHOWER ||
                                o->q[0].act == ACT_BATH || o->q[0].tx == PASSIVE);
        if (busy) { notify("%s is busy right now.", o->name); popQueue(s); startNext(i); return; }
        if (q->act == ACT_HUG && s->rel < 30) {
            notify("%s isn't ready for a hug!", o->name);
            s->rel -= 4; o->rel -= 4; s->need[NEED_FUN] -= 5;
            popQueue(s); startNext(i); return;
        }
        // interrupt them and make them listen
        int j = q->sim;
        if (o->qlen && o->state == SS_ACT) endAction(j, false);
        if (o->qlen < MAX_QUEUE) {
            for (int k = o->qlen; k > 0; k--) o->q[k] = o->q[k - 1];
            o->qlen++;
        }
        o->q[0].act = q->act; o->q[0].obj = -1; o->q[0].sim = i; o->q[0].tx = PASSIVE; o->q[0].tz = 0;
        o->state = SS_ACT; o->actMin = 0; o->pose = POSE_TALK; o->plen = 0;
        o->face = yawTo(s->x - o->x, s->z - o->z);
        s->face = yawTo(dx, dz);
        s->state = SS_ACT; s->pose = a->pose; s->actMin = 0;
        return;
    }

    int oi = q->obj;
    if (oi < 0 || oi >= W.nobj || (usersOf(oi) >= capacity(oi) && !(W.obj[oi].user & (1 << i)))) {
        popQueue(s); startNext(i); return;
    }
    int slot = usersOf(oi);
    W.obj[oi].user |= 1 << i;
    float cx, cz;
    objCenter(oi, &cx, &cz);
    const ModelInfo* m = &g_models[g_objDefs[W.obj[oi].def].model];
    int r = W.obj[oi].rot;
    float fx = FRONT[r][0], fz = FRONT[r][1];
    float h = f32tofloat(m->h);
    s->homeX = s->x; s->homeZ = s->z;
    float side = capacity(oi) > 1 ? (slot ? 0.42f : -0.42f) : 0;  // second user sits/lies beside
    float px = -fz, pz = fx;                                       // perpendicular to front
    switch (a->use) {
    case USE_SIT:
        s->x = cx + fx * 0.08f + px * side; s->z = cz + fz * 0.08f + pz * side;
        s->y = h * 0.42f; s->face = yawTo(fx, fz);
        break;
    case USE_LIE: {
        float H = simHeight();
        s->x = cx + fx * H * 0.5f + px * side; s->z = cz + fz * H * 0.5f + pz * side;
        s->y = (m == &g_models[MDL_BATHTUB]) ? h * 0.35f : h * 0.8f;
        s->face = yawTo(fx, fz);
        break; }
    case USE_STAND_ON:
        s->x = cx; s->z = cz; s->y = 0.03f; s->face = yawTo(fx, fz);
        break;
    default:
        s->face = yawTo(cx - s->x, cz - s->z);
        s->y = 0;
        break;
    }
    if (a->moneyPerHour < 0) { W.money += a->moneyPerHour; ui_dirty(); }
    s->state = SS_ACT;
    s->pose = a->pose;
}

static void endAction(int i, bool completed) {
    Sim* s = &W.sim[i];
    if (!s->qlen) return;
    QItem* q = &s->q[0];
    const ActionDef* a = &g_actions[q->act];
    if (s->state == SS_ACT && q->obj >= 0 && q->obj < W.nobj) {
        W.obj[q->obj].user &= ~(1 << i);
        if (a->use == USE_SIT || a->use == USE_LIE || a->use == USE_STAND_ON) {
            s->x = s->homeX; s->z = s->homeZ;
        }
    }
    if (q->obj == -1 && a->use == USE_LIE) { s->x = s->homeX; s->z = s->homeZ; }
    s->y = 0;
    if (s->earned) {
        W.money += s->earned;
        notify("%s earned %d simoleons.", s->name, s->earned);
    }
    // social outcomes
    if (s->state == SS_ACT && a->use == USE_SOCIAL && q->tx != PASSIVE && q->sim >= 0) {
        Sim* o = &W.sim[q->sim];
        float gain = completed ? 3 : 1;
        if (q->act == ACT_JOKE) {
            bool funny = (rand() % 10) < 4 + (int)s->skill[SKILL_CHARISMA] / 2;
            gain = funny ? 5 : -5;
            notify(funny ? "%s laughed at the joke!" : "%s didn't get the joke...", o->name);
            if (!funny) { o->need[NEED_FUN] -= 15; s->need[NEED_FUN] -= 10; }
        }
        if (q->act == ACT_HUG) gain = 6;
        s->rel = clampf(s->rel + gain, -100, 100);
        o->rel = clampf(o->rel + gain, -100, 100);
        // release the listener
        if (o->qlen && o->q[0].tx == PASSIVE && o->q[0].sim == i) {
            popQueue(o); o->pose = POSE_STAND; o->state = SS_IDLE; o->idleT = 0;
            startNext(q->sim);
        }
    }
    s->pose = POSE_STAND;
    s->state = SS_IDLE;
    (void)completed;
}

bool sim_queue(int i, int act, int obj, int other, int tx, int tz) {
    Sim* s = &W.sim[i];
    if (s->qlen >= MAX_QUEUE) { notify("%s's queue is full.", s->name); return false; }
    // a listener drops out of the conversation when the player gives them orders
    if (s->qlen && s->q[0].tx == PASSIVE) { endAction(i, false); popQueue(s); }
    QItem* q = &s->q[s->qlen++];
    q->act = act; q->obj = obj; q->sim = other; q->tx = tx; q->tz = tz;
    if (s->qlen == 1) startNext(i);
    s->idleT = 0;
    return true;
}

void sim_cancel(int i) {
    Sim* s = &W.sim[i];
    if (!s->qlen) return;
    if (s->q[0].tx == PASSIVE) return;   // can't cancel being talked to
    if (s->state == SS_ACT) endAction(i, false);
    s->qlen = 0;
    s->state = SS_IDLE;
    s->plen = 0;
    s->pose = POSE_STAND;
    s->y = 0;
    s->idleT = 0;
}

// ---------------------------------------------------------------- free will
static float weight(float n) { float d = (100 - n) / 100.0f; return d * d * 3 + 0.05f; }

static void freeWill(int i) {
    Sim* s = &W.sim[i];
    float best = 0.6f;
    int bAct = 0, bObj = -1, bSim = -1;
    for (int oi = 0; oi < W.nobj; oi++) {
        const ObjDef* d = &g_objDefs[W.obj[oi].def];
        if (usersOf(oi) >= capacity(oi)) continue;
        float cx, cz;
        objCenter(oi, &cx, &cz);
        float dist = fabsf(cx - s->x) + fabsf(cz - s->z);
        for (int k = 0; k < 4 && d->actions[k]; k++) {
            int act = d->actions[k];
            const ActionDef* a = &g_actions[act];
            if (act == ACT_WORK || a->moneyPerHour < 0) continue;  // sims don't spend money on their own
            float sc = 0;
            for (int n = 0; n < NEED_COUNT; n++) {
                float g = a->rate[n] * a->minutes / 60.0f;
                if (g > 100 - s->need[n]) g = 100 - s->need[n];
                sc += g * weight(s->need[n]);
            }
            if (act == ACT_SLEEP) {
                int hr = (W.minutes % 1440) / 60;
                bool night = hr >= 21 || hr < 7;
                if (night && s->need[NEED_ENERGY] < 75) sc *= 2.5f;
                else if (s->need[NEED_ENERGY] > 30) sc *= 0.2f;
            }
            sc = sc / 10.0f - dist * 0.08f + (rand() % 100) * 0.004f;
            if (sc > best) { best = sc; bAct = act; bObj = oi; bSim = -1; }
        }
    }
    // social
    for (int j = 0; j < W.nsim; j++) {
        if (j == i) continue;
        Sim* o = &W.sim[j];
        if (o->qlen && (o->q[0].act == ACT_SLEEP || o->q[0].act == ACT_NAP || o->q[0].tx == PASSIVE)) continue;
        float sc = (55 * weight(s->need[NEED_SOCIAL]) + 10 * weight(s->need[NEED_FUN])) / 10.0f;
        if (sc > best) { best = sc; bAct = ACT_CHAT; bObj = -1; bSim = j; }
    }
    if (bAct) sim_queue(i, bAct, bObj, bSim, 0, 0);
    else s->idleT = -2;   // nothing worth doing; wait a bit
}

// ---------------------------------------------------------------- per-frame update
void sim_update(int i, float gameMinutes, float realStep) {
    Sim* s = &W.sim[i];
    float hours = gameMinutes / 60.0f;
    QItem* q = s->qlen ? &s->q[0] : NULL;
    const ActionDef* a = q ? &g_actions[q->act] : NULL;
    bool acting = s->state == SS_ACT && a;

    // needs
    for (int n = 0; n < NEED_COUNT; n++) {
        float d = DECAY[n];
        if (acting && n == NEED_ENERGY && (q->act == ACT_SLEEP || q->act == ACT_NAP)) d = 0;
        if (acting) d += a->rate[n];
        s->need[n] = clampf(s->need[n] + d * hours, 0, 100);
    }
    // skills & pay
    if (acting && a->skill >= 0 && q->tx != PASSIVE) {
        int before = (int)s->skill[a->skill];
        s->skill[a->skill] = clampf(s->skill[a->skill] + hours * 0.3f, 0, 10);
        if ((int)s->skill[a->skill] > before) {
            notify("%s gained a %s point! (%d)", s->name, g_skillNames[a->skill], (int)s->skill[a->skill]);
        }
    }
    if (acting && q->act == ACT_WORK) {
        static float acc[MAX_SIMS];
        acc[i] += hours * (20 + s->skill[SKILL_LOGIC] * 7 + s->skill[SKILL_CHARISMA] * 3);
        while (acc[i] >= 1) { acc[i] -= 1; s->earned++; }
    }

    // emergencies
    if (s->need[NEED_BLADDER] <= 0) {
        s->need[NEED_BLADDER] = 100; s->need[NEED_HYGIENE] = 0; s->need[NEED_FUN] -= 20;
        notify("Oh no! %s had an accident.", s->name);
    }
    if (s->need[NEED_ENERGY] <= 0 && !(acting && (q->act == ACT_SLEEP || q->act == ACT_NAP))) {
        sim_cancel(i);
        notify("%s passed out from exhaustion!", s->name);
        sim_queue(i, ACT_NAP, -1, -1, 0, 0);
        return;
    }

    s->anim += realStep;

    switch (s->state) {
    case SS_IDLE:
        s->pose = POSE_STAND;
        if (s->qlen) { startNext(i); break; }
        s->idleT += realStep / 60.0f;
        if (OPT.freeWill && s->idleT > 2.5f) { s->idleT = 0; freeWill(i); }
        break;
    case SS_WALK: {
        s->pose = POSE_STAND;
        if (s->pidx >= s->plen) { arrive(i); break; }
        float tx = s->path[s->pidx][0] + 0.5f, tz = s->path[s->pidx][1] + 0.5f;
        float dx = tx - s->x, dz = tz - s->z;
        float d = sqrtf(dx * dx + dz * dz);
        float sp = 0.045f * realStep * (OPT.speed >= 3 ? 3.0f : OPT.speed == 2 ? 1.8f : 1.0f);
        if (d <= sp) {
            s->x = tx; s->z = tz; s->pidx++;
            // the world may have changed under us
            if (s->pidx < s->plen) {
                int nx = s->path[s->pidx][0], nz = s->path[s->pidx][1];
                if (!world_canStep((int)s->x, (int)s->z, nx - (int)s->x, nz - (int)s->z)) {
                    if (!beginItem(i)) { popQueue(s); startNext(i); }
                }
            }
        } else {
            s->x += dx / d * sp; s->z += dz / d * sp;
            float want = yawTo(dx, dz);
            float diff = fmodf(want - s->face + 540.0f, 360.0f) - 180.0f;
            s->face += diff * 0.35f;
        }
        break; }
    case SS_ACT:
        if (!q) { s->state = SS_IDLE; break; }
        s->actMin += gameMinutes;
        if (q->tx == PASSIVE) {
            // stop listening if the speaker isn't talking to us any more
            Sim* o = &W.sim[q->sim];
            if (!o->qlen || o->q[0].sim != i || o->state != SS_ACT || s->actMin > a->minutes + 30) {
                popQueue(s); s->state = SS_IDLE; s->pose = POSE_STAND;
            }
            break;
        }
        {
            bool done = s->actMin >= a->minutes;
            if (a->endNeed >= 0 && s->need[a->endNeed] >= 99.5f && s->actMin > 5) done = true;
            if (done) { endAction(i, true); popQueue(s); startNext(i); }
        }
        break;
    }
}
