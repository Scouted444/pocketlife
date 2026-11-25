// PocketLife - bottom-screen touch UI and all player input
#include "game.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Options OPT = { 0, 0, 1, 1, 1, 1, 1 };

static PrintConsole s_con;
static u16* s_bm;
static bool s_dirty = true;
void ui_dirty(void) { s_dirty = true; }

// ---------------------------------------------------------------- palette
#define COL_BG     RGB15(3, 6, 9)
#define COL_PANEL  RGB15(6, 11, 15)
#define COL_PANEL2 RGB15(9, 15, 20)
#define COL_HI     RGB15(6, 22, 12)
#define COL_HI2    RGB15(10, 27, 16)
#define COL_EDGE   RGB15(14, 21, 26)
#define COL_RED    RGB15(24, 6, 5)
#define COL_GOLD   RGB15(28, 23, 6)

// ---------------------------------------------------------------- drawing
static void fill(int x, int y, int w, int h, u16 c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 256) w = 256 - x;
    if (y + h > 192) h = 192 - y;
    if (w <= 0 || h <= 0) return;
    c |= BIT(15);
    for (int j = 0; j < h; j++) {
        u16* p = s_bm + (y + j) * 256 + x;
        for (int i = 0; i < w; i++) p[i] = c;
    }
}
static void frame(int x, int y, int w, int h, u16 fillc, u16 edge) {
    fill(x, y, w, h, edge);
    fill(x + 1, y + 1, w - 2, h - 2, fillc);
}
static void icon(int x, int y, const u16* px) {
    for (int j = 0; j < 32; j++)
        for (int i = 0; i < 32; i++) {
            u16 c = px[j * 32 + i];
            if (c) s_bm[(y + j) * 256 + x + i] = c;
        }
}
static void text(int col, int row, const char* fmt, ...) {
    char buf[40];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[32 - col > 0 ? 32 - col : 0] = 0;
    printf("\x1b[%d;%dH%s", row, col, buf);
}
static void bar(int x, int y, int w, float v) {
    fill(x, y, w, 6, RGB15(2, 3, 4));
    int n = (int)(v * (w - 2) / 100.0f);
    if (n < 0) n = 0;
    u16 c = v > 60 ? RGB15(6, 26, 8) : v > 30 ? RGB15(28, 24, 4) : RGB15(28, 6, 4);
    fill(x + 1, y + 1, n, 4, c);
}

// ---------------------------------------------------------------- touch buttons
typedef struct { s16 x, y, w, h; s16 id, arg; } Btn;
static Btn s_btn[48];
static int s_nbtn;
static void addBtn(int x, int y, int w, int h, int id, int arg) {
    if (s_nbtn < 48) { Btn b = { x, y, w, h, id, arg }; s_btn[s_nbtn++] = b; }
}
// a text button on the 8px grid
static void button(int col, int row, int wch, const char* label, int id, int arg, bool hot) {
    frame(col * 8 - 2, row * 8 - 3, wch * 8 + 4, 14, hot ? COL_HI : COL_PANEL2, hot ? COL_HI2 : COL_EDGE);
    text(col + (wch - (int)strlen(label)) / 2, row, "%s", label);
    addBtn(col * 8 - 2, row * 8 - 3, wch * 8 + 4, 14, id, arg);
}
static int hitTest(int px, int py, int* arg) {
    for (int i = s_nbtn - 1; i >= 0; i--) {
        Btn* b = &s_btn[i];
        if (px >= b->x && py >= b->y && px < b->x + b->w && py < b->y + b->h) { *arg = b->arg; return b->id; }
    }
    return -1;
}

void ui_init(void) {
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&s_con, 0, BgType_Text4bpp, BgSize_T_256x256, 0, 1, false, true);
    int bg = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 2, 0);
    bgSetPriority(bg, 3);
    bgSetPriority(s_con.bgId, 0);
    s_bm = bgGetGfxPtr(bg);
    consoleSelect(&s_con);
}

static void beginDraw(void) {
    consoleSelect(&s_con);
    consoleClear();
    for (int i = 0; i < 256 * 192; i++) s_bm[i] = COL_BG | BIT(15);
    s_nbtn = 0;
}

// ---------------------------------------------------------------- game UI state
enum { B_TAB = 1, B_SPEED, B_MENU, B_SIM, B_QUEUE, B_CAT, B_PAGE, B_TOOL, B_OPT, B_CAS, B_TITLE, B_EDGE, B_ROT, B_SELL };
enum { T_WALL, T_WINDOW, T_DOOR, T_ERASE, T_FLOOR_WOOD, T_FLOOR_TILE, T_FLOOR_CARPET, T_FLOOR_STONE, T_FLOOR_GRASS, T_COUNT };
static const char* const TOOL_NAMES[T_COUNT] = { "Wall $10", "Window $40", "Door $60", "Erase", "Wood $2", "Tile $2", "Carpet $2", "Stone $2", "Grass" };

typedef struct { char label[24]; u8 act; s8 obj; s8 sim; s8 tx, tz; s8 special; } MenuItem;

static int s_mode = MODE_LIVE;
static int s_cx = 8, s_cz = 7;
static MenuItem s_menu[8];
static int s_menuN, s_menuSel;
static bool s_menuOpen;
static int s_page, s_held = -1, s_heldRot, s_tool;
static bool s_moving;
static PlacedObj s_moveOrig;
static int s_edge;          // 0 north edge, 1 west edge
static int s_prevSpeed = 1;
static int s_buyList[64], s_buyN;

static void buildBuyList(void) {
    s_buyN = 0;
    for (int c = 0; c < CAT_COUNT; c++)
        for (int i = 0; i < g_objDefCount; i++)
            if (!(g_objDefs[i].flags & OF_NOBUY) && g_objDefs[i].cat == c) s_buyList[s_buyN++] = i;
}

static void setMode(int m) {
    if (m == s_mode) return;
    if (s_mode == MODE_BUY && s_held >= 0) {
        if (s_moving) { world_addObj(s_moveOrig.def, s_moveOrig.x, s_moveOrig.z, s_moveOrig.rot); s_moving = false; }
        s_held = -1;
    }
    if (m != MODE_LIVE && s_mode == MODE_LIVE) { s_prevSpeed = OPT.speed; OPT.speed = 0; }
    if (m == MODE_LIVE) OPT.speed = s_prevSpeed ? s_prevSpeed : 1;
    s_mode = m;
    s_menuOpen = false;
    ui_dirty();
}

static const char* clockStr(void) {
    static char b[24];
    u32 m = W.minutes % 1440;
    int h = m / 60, mi = m % 60;
    snprintf(b, sizeof(b), "Day %lu %2d:%02d%s", (unsigned long)(W.minutes / 1440 + 1), h % 12 == 0 ? 12 : h % 12, mi, h < 12 ? "am" : "pm");
    return b;
}

static void drawTopBar(void) {
    fill(0, 0, 256, 15, COL_PANEL);
    fill(0, 15, 256, 1, COL_EDGE);
    text(0, 0, "\x1b[33m$%ld\x1b[39m", (long)W.money);
    text(8, 0, "%s", clockStr());
    for (int i = 0; i < 4; i++) {
        bool on = OPT.speed == i;
        frame(196 + i * 15, 1, 14, 12, on ? COL_HI : COL_PANEL2, on ? COL_HI2 : COL_EDGE);
        addBtn(196 + i * 15, 0, 15, 15, B_SPEED, i);
    }
    // speed glyphs drawn as tiny bars so they fit 14px buttons
    for (int i = 0; i < 4; i++) {
        int x = 199 + i * 15;
        if (i == 0) { fill(x + 1, 4, 2, 6, RGB15(31, 31, 31)); fill(x + 5, 4, 2, 6, RGB15(31, 31, 31)); }
        else for (int k = 0; k < i; k++) for (int r = 0; r < 3; r++) fill(x + k * 3 + r, 4 + r, 1, 6 - r * 2, RGB15(31, 31, 31));
    }
}

static void drawTabs(void) {
    static const char* const tabs[4] = { "LIVE", "BUY", "BUILD", "OPTIONS" };
    int cur = s_mode;
    for (int i = 0; i < 4; i++) {
        bool on = (i == cur && i < 3);
        frame(i * 64, 170, 64, 22, on ? COL_HI : COL_PANEL, on ? COL_HI2 : COL_EDGE);
        text(i * 8 + (8 - (int)strlen(tabs[i])) / 2, 22, "%s", tabs[i]);
        addBtn(i * 64, 168, 64, 24, B_TAB, i);
    }
}

static void drawNotice(void) {
    if (g_noticeTimer > 0) {
        fill(0, 152, 256, 16, RGB15(12, 9, 2));
        text(0, 19, "%.32s", g_notice);
    }
}

// ---------------------------------------------------------------- LIVE
static void drawLive(void) {
    Sim* s = &W.sim[W.active];
    if (s_menuOpen) {
        frame(4, 18, 248, 132, COL_PANEL, COL_EDGE);
        text(2, 3, "\x1b[32m%s\x1b[39m", s_menuN ? "What should we do?" : "");
        for (int i = 0; i < s_menuN; i++)
            button(3, 5 + i * 2, 26, s_menu[i].label, B_MENU, i, i == s_menuSel);
        return;
    }
    // left: sim card
    frame(2, 18, 124, 132, COL_PANEL, COL_EDGE);
    text(1, 3, "\x1b[32m%s\x1b[39m", s->name);
    text(1, 4, "%.15s", sim_actionLabel(s));
    for (int k = 1; k < s->qlen && k < 4; k++) {
        button(1, 3 + k * 2, 14, g_actions[s->q[k].act].name, B_QUEUE, k, false);
    }
    if (s->qlen) button(1, 11, 14, "Cancel (B)", B_QUEUE, 0, false);
    for (int k = 0; k < SKILL_COUNT; k++) {
        text(1, 13 + k, "%-8s", g_skillNames[k]);
        int lv = (int)s->skill[k];
        for (int p = 0; p < 10; p++)
            fill(74 + p * 5, (13 + k) * 8 + 1, 4, 5, p < lv ? COL_GOLD : RGB15(5, 7, 9));
    }
    // right: needs
    frame(130, 18, 124, 132, COL_PANEL, COL_EDGE);
    for (int n = 0; n < NEED_COUNT; n++) {
        text(17, 3 + n * 2, "%s", g_needNames[n]);
        bar(136, 3 * 8 + n * 16 + 9, 112, s->need[n]);
    }
    // sim switch buttons
    for (int i = 0; i < W.nsim; i++)
        button(1 + i * 8, 17, 7, W.sim[i].name, B_SIM, i, i == W.active);
    if (W.nsim > 1) text(18, 17, "Rel %+d", (int)s->rel);
}

// ---------------------------------------------------------------- BUY
static void drawBuy(void) {
    frame(2, 18, 252, 132, COL_PANEL, COL_EDGE);
    int per = 18, pages = (s_buyN + per - 1) / per;
    if (s_page >= pages) s_page = 0;
    for (int k = 0; k < per; k++) {
        int idx = s_page * per + k;
        if (idx >= s_buyN) break;
        int def = s_buyList[idx];
        int x = 6 + (k % 6) * 41, y = 21 + (k / 6) * 38;
        bool sel = s_held == def && !s_moving;
        frame(x, y, 38, 36, sel ? COL_HI : COL_PANEL2, sel ? COL_HI2 : COL_EDGE);
        icon(x + 3, y + 2, g_icons[g_objDefs[def].model]);
        addBtn(x, y, 38, 36, B_CAT, def);
    }
    button(1, 17, 3, "<", B_PAGE, -1, false);
    button(28, 17, 3, ">", B_PAGE, 1, false);
    if (s_held >= 0) {
        const ObjDef* d = &g_objDefs[s_held];
        text(5, 17, "%.14s %s$%d", d->name, s_moving ? "move " : "", s_moving ? 0 : d->price);
        button(22, 17, 5, "Rot", B_ROT, 0, false);
    } else {
        text(5, 17, "Page %d/%d  Y=sell", s_page + 1, pages);
    }
}

// ---------------------------------------------------------------- BUILD
static void drawBuild(void) {
    frame(2, 18, 252, 132, COL_PANEL, COL_EDGE);
    for (int t = 0; t < T_COUNT; t++) {
        int c = t < 4 ? 1 : 17, r = 3 + (t < 4 ? t : t - 4) * 2;
        button(c, r, 14, TOOL_NAMES[t], B_TOOL, t, t == s_tool);
    }
    button(1, 13, 14, s_edge ? "Edge: West (Y)" : "Edge: North(Y)", B_EDGE, 0, false);
    text(1, 15, "A: build  B: erase");
    text(1, 16, "Hold A + D-pad to drag");
}

// ---------------------------------------------------------------- OPTIONS
static bool s_optOpen;
static void drawOptions(void) {
    frame(2, 18, 252, 148, COL_PANEL, COL_EDGE);
    char b[32];
    button(1, 3, 14, OPT.camTop ? "View: Top-down" : "View: 2.5D", B_OPT, 0, false);
    button(17, 3, 14, "Rotate (L/R)", B_OPT, 1, false);
    snprintf(b, sizeof(b), "Zoom: %d", OPT.zoom + 1);
    button(1, 5, 14, b, B_OPT, 2, false);
    static const char* const wl[3] = { "Walls: Up", "Walls: Cutaway", "Walls: Down" };
    button(17, 5, 14, wl[OPT.walls], B_OPT, 3, false);
    button(1, 7, 14, OPT.freeWill ? "Free Will: On" : "Free Will: Off", B_OPT, 4, false);
    button(17, 7, 14, "Save Game", B_OPT, 5, false);
    button(1, 9, 14, "Load Game", B_OPT, 6, false);
    button(17, 9, 14, "Back to LIVE", B_OPT, 7, false);
    text(1, 12, "X: top-down / 2.5D view");
    text(1, 13, "L/R: rotate  SELECT: zoom");
    text(1, 14, "START: pause  Y: switch sim");
    text(1, 16, "Polys %d/2048  est %d", g_lastPolys, world_polyEstimate());
    text(1, 17, "Kit models: %d/%d", g_assetsFromKit, MDL_COUNT);
}

static void drawGame(void) {
    beginDraw();
    drawTopBar();
    if (s_optOpen) drawOptions();
    else if (s_mode == MODE_LIVE) drawLive();
    else if (s_mode == MODE_BUY) drawBuy();
    else drawBuild();
    drawNotice();
    drawTabs();
}

// ---------------------------------------------------------------- actions
static void openMenu(void) {
    s_menuN = 0; s_menuSel = 0;
    Sim* me = &W.sim[W.active];
    // another sim?
    for (int j = 0; j < W.nsim; j++) {
        if (j == W.active) continue;
        if ((int)W.sim[j].x == s_cx && (int)W.sim[j].z == s_cz) {
            static const u8 soc[3] = { ACT_CHAT, ACT_JOKE, ACT_HUG };
            for (int k = 0; k < 3; k++) {
                MenuItem* m = &s_menu[s_menuN++];
                snprintf(m->label, sizeof(m->label), "%s", g_actions[soc[k]].name);
                m->act = soc[k]; m->obj = -1; m->sim = j; m->special = 0;
            }
            MenuItem* m = &s_menu[s_menuN++];
            snprintf(m->label, sizeof(m->label), "Switch to %s", W.sim[j].name);
            m->special = 1; m->sim = j;
            goto done;
        }
    }
    {
        int oi = world_objAt(s_cx, s_cz, true);
        if (oi >= 0) {
            const ObjDef* d = &g_objDefs[W.obj[oi].def];
            for (int k = 0; k < 4 && d->actions[k]; k++) {
                MenuItem* m = &s_menu[s_menuN++];
                snprintf(m->label, sizeof(m->label), "%s", g_actions[d->actions[k]].name);
                m->act = d->actions[k]; m->obj = oi; m->sim = -1; m->special = 0;
            }
            if (!s_menuN && !(d->flags & OF_WALKABLE)) { notify("Nothing to do with the %s.", d->name); return; }
        }
        if (world_walkable(s_cx, s_cz)) {
            MenuItem* m = &s_menu[s_menuN++];
            snprintf(m->label, sizeof(m->label), "Go Here");
            m->act = ACT_GOTO; m->obj = -1; m->sim = -1; m->tx = s_cx; m->tz = s_cz; m->special = 0;
        }
    }
done:
    if (!s_menuN) return;
    MenuItem* m = &s_menu[s_menuN++];
    snprintf(m->label, sizeof(m->label), "Never mind");
    m->special = 2;
    s_menuOpen = true;
    (void)me;
    ui_dirty();
}

static void pickMenu(int i) {
    MenuItem* m = &s_menu[i];
    s_menuOpen = false;
    ui_dirty();
    if (m->special == 2) return;
    if (m->special == 1) { W.active = m->sim; return; }
    sim_queue(W.active, m->act, m->obj, m->sim, m->tx, m->tz);
}

static void buyPlace(void) {
    if (s_held < 0) {
        // pick up an existing object to move it
        int oi = world_objAt(s_cx, s_cz, true);
        if (oi < 0) return;
        if (W.obj[oi].user) { notify("Someone is using that."); return; }
        s_moveOrig = W.obj[oi];
        s_held = W.obj[oi].def; s_heldRot = W.obj[oi].rot; s_moving = true;
        s_cx = W.obj[oi].x; s_cz = W.obj[oi].z;
        world_removeObj(oi);
        ui_dirty();
        return;
    }
    const ObjDef* d = &g_objDefs[s_held];
    if (!world_canPlace(s_held, s_cx, s_cz, s_heldRot, -1)) { notify("Can't place that there."); return; }
    if (!s_moving) {
        if (W.money < d->price) { notify("Not enough simoleons!"); return; }
        int est = world_polyEstimate() + g_models[d->model].polys * 6 / 10;
        if (est > POLY_BUDGET) { notify("Lot is full (DS polygon limit)."); return; }
        if (W.nobj >= MAX_OBJS) { notify("Too many objects on the lot."); return; }
    }
    if (world_addObj(s_held, s_cx, s_cz, s_heldRot) >= 0) {
        if (!s_moving) { W.money -= d->price; notify("Bought %s for $%d.", d->name, d->price); }
        else { s_moving = false; s_held = -1; }
    }
    ui_dirty();
}

static void buySell(void) {
    int oi = world_objAt(s_cx, s_cz, true);
    if (oi < 0) return;
    if (W.obj[oi].user) { notify("Someone is using that."); return; }
    const ObjDef* d = &g_objDefs[W.obj[oi].def];
    int refund = d->price * 7 / 10;
    W.money += refund;
    notify("Sold %s for $%d.", d->name, refund);
    world_removeObj(oi);
    ui_dirty();
}

static bool edgeSplitsObject(int horiz, int x, int z) {
    int a = world_objAt(x, z, true);
    int b = horiz ? world_objAt(x, z - 1, true) : world_objAt(x - 1, z, true);
    return a >= 0 && a == b;
}

static void buildApply(bool erase) {
    int horiz = s_edge == 0;
    if (!erase && s_tool >= T_FLOOR_WOOD) {
        int f = s_tool == T_FLOOR_GRASS ? FL_GRASS : FL_WOOD + (s_tool - T_FLOOR_WOOD);
        if (W.floor[s_cz][s_cx] == f) return;
        int cost = f == FL_GRASS ? 0 : 2;
        if (W.money < cost) { notify("Not enough simoleons!"); return; }
        W.floor[s_cz][s_cx] = f; W.money -= cost;
        ui_dirty();
        return;
    }
    if (erase || s_tool == T_ERASE) {
        if (world_getWall(horiz, s_cx, s_cz)) { world_setWall(horiz, s_cx, s_cz, W_NONE); ui_dirty(); }
        return;
    }
    static const u8 TY[3] = { W_WALL, W_WINDOW, W_DOOR };
    static const int COST[3] = { 10, 40, 60 };
    u8 ty = TY[s_tool];
    if (world_getWall(horiz, s_cx, s_cz) == ty) return;
    if (edgeSplitsObject(horiz, s_cx, s_cz)) { notify("An object is in the way."); return; }
    if (W.money < COST[s_tool]) { notify("Not enough simoleons!"); return; }
    if (world_polyEstimate() + g_models[MDL_WALL + s_tool].polys > POLY_BUDGET) { notify("Lot is full (DS polygon limit)."); return; }
    world_setWall(horiz, s_cx, s_cz, ty);
    W.money -= COST[s_tool];
    ui_dirty();
}

static void doOption(int o) {
    switch (o) {
    case 0: OPT.camTop ^= 1; break;
    case 1: OPT.camRot = (OPT.camRot + 1) & 3; break;
    case 2: OPT.zoom = (OPT.zoom + 1) % 3; break;
    case 3: OPT.walls = (OPT.walls + 1) % 3; break;
    case 4: OPT.freeWill ^= 1; break;
    case 5: notify(save_write() ? "Game saved." : "Save failed (no SD card?)"); break;
    case 6:
        if (save_read()) { world_rebuildOcc(); notify("Game loaded."); }
        else notify("No save found.");
        break;
    case 7: s_optOpen = false; setMode(MODE_LIVE); break;
    }
    ui_dirty();
}

// ---------------------------------------------------------------- per-frame
static void handleTouch(void) {
    if (!(keysDown() & KEY_TOUCH)) return;
    touchPosition tp;
    touchRead(&tp);
    int arg = 0, id = hitTest(tp.px, tp.py, &arg);
    switch (id) {
    case B_TAB:
        if (arg == 3) { s_optOpen = !s_optOpen; ui_dirty(); }
        else { s_optOpen = false; setMode(arg); }
        break;
    case B_SPEED: OPT.speed = arg; if (s_mode != MODE_LIVE) s_prevSpeed = arg; ui_dirty(); break;
    case B_MENU: pickMenu(arg); break;
    case B_SIM: W.active = arg; s_cx = (int)W.sim[arg].x; s_cz = (int)W.sim[arg].z; ui_dirty(); break;
    case B_QUEUE: {
        Sim* s = &W.sim[W.active];
        if (arg == 0) sim_cancel(W.active);
        else if (arg < s->qlen) { for (int k = arg; k < s->qlen - 1; k++) s->q[k] = s->q[k + 1]; s->qlen--; }
        ui_dirty();
        break; }
    case B_CAT:
        if (s_moving) break;
        s_held = (s_held == arg) ? -1 : arg; s_heldRot = 0; ui_dirty();
        break;
    case B_PAGE: {
        int pages = (s_buyN + 17) / 18;
        s_page = (s_page + arg + pages) % pages; ui_dirty();
        break; }
    case B_ROT: s_heldRot = (s_heldRot + 1) & 3; break;
    case B_TOOL: s_tool = arg; ui_dirty(); break;
    case B_EDGE: s_edge ^= 1; ui_dirty(); break;
    case B_OPT: doOption(arg); break;
    }
}

static void moveCursor(void) {
    int pad = keysDownRepeat() & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
    if (!pad || s_menuOpen) return;
    int dx, dz;
    cam_screenDir(pad, &dx, &dz);
    s_cx += dx; s_cz += dz;
    if (s_cx < 0) s_cx = 0;
    if (s_cz < 0) s_cz = 0;
    if (s_cx >= LOT_W) s_cx = LOT_W - 1;
    if (s_cz >= LOT_H) s_cz = LOT_H - 1;
    if (s_mode == MODE_BUILD && (keysHeld() & KEY_A)) buildApply(false);
    if (s_mode == MODE_BUILD && (keysHeld() & KEY_B)) buildApply(true);
}

static void tickWorld(void) {
    static const float RATE[4] = { 0, 1.0f / 30, 1.0f / 10, 1.0f / 3 };
    float gm = RATE[OPT.speed];
    // everybody asleep: fast-forward like the PC game does
    bool allAsleep = W.nsim > 0;
    for (int i = 0; i < W.nsim; i++)
        if (!(W.sim[i].qlen && W.sim[i].state == SS_ACT && W.sim[i].q[0].act == ACT_SLEEP)) allAsleep = false;
    if (allAsleep && OPT.speed) gm = 1.0f / 2;
    static float acc;
    acc += gm;
    int whole = (int)acc;
    acc -= whole;
    W.minutes += whole;
    for (int i = 0; i < W.nsim; i++) sim_update(i, gm, OPT.speed ? 1.0f : 0.0f);
    // bills every 3 days at 9am
    u32 day = W.minutes / 1440, hour = (W.minutes % 1440) / 60;
    if (day > 0 && day % 3 == 0 && day != W.lastBillDay && hour >= 9) {
        W.lastBillDay = day;
        int bill = 40;
        for (int i = 0; i < W.nobj; i++) bill += g_objDefs[W.obj[i].def].price / 40;
        W.money -= bill;
        notify("Bills paid: $%d.", bill);
    }
}

void ui_game(void) {
    buildBuyList();
    s_mode = MODE_LIVE;
    s_cx = (int)W.sim[0].x; s_cz = (int)W.sim[0].z;
    keysSetRepeat(14, 4);
    ui_dirty();
    while (1) {
        scanKeys();
        int kd = keysDown();
        handleTouch();
        moveCursor();

        if (kd & KEY_X) { OPT.camTop ^= 1; ui_dirty(); }
        if (kd & KEY_L) OPT.camRot = (OPT.camRot + 3) & 3;
        if (kd & KEY_R) OPT.camRot = (OPT.camRot + 1) & 3;
        if (kd & KEY_SELECT) { OPT.zoom = (OPT.zoom + 1) % 3; ui_dirty(); }
        if (kd & KEY_START) {
            if (s_mode == MODE_LIVE) {
                if (OPT.speed) { s_prevSpeed = OPT.speed; OPT.speed = 0; }
                else OPT.speed = s_prevSpeed ? s_prevSpeed : 1;
            }
            ui_dirty();
        }

        if (s_mode == MODE_LIVE) {
            if (s_menuOpen) {
                if (kd & KEY_UP) { s_menuSel = (s_menuSel + s_menuN - 1) % s_menuN; ui_dirty(); }
                if (kd & KEY_DOWN) { s_menuSel = (s_menuSel + 1) % s_menuN; ui_dirty(); }
                if (kd & KEY_A) pickMenu(s_menuSel);
                else if (kd & KEY_B) { s_menuOpen = false; ui_dirty(); }
            } else {
                if (kd & KEY_A) openMenu();
                if (kd & KEY_B) { sim_cancel(W.active); ui_dirty(); }
                if ((kd & KEY_Y) && W.nsim > 1) { W.active = (W.active + 1) % W.nsim; ui_dirty(); }
            }
        } else if (s_mode == MODE_BUY) {
            if (kd & KEY_A) buyPlace();
            if (kd & KEY_Y) { if (s_held >= 0) s_heldRot = (s_heldRot + 1) & 3; else buySell(); }
            if (kd & KEY_B) {
                if (s_moving) { world_addObj(s_moveOrig.def, s_moveOrig.x, s_moveOrig.z, s_moveOrig.rot); s_moving = false; }
                s_held = -1; ui_dirty();
            }
        } else {
            if (kd & KEY_A) buildApply(false);
            if (kd & KEY_B) buildApply(true);
            if (kd & KEY_Y) { s_edge ^= 1; ui_dirty(); }
        }

        tickWorld();
        if (g_noticeTimer > 0 && --g_noticeTimer == 0) ui_dirty();

        // camera follows the cursor in build/buy, the active sim in live mode (until you move the cursor)
        cam_update(s_cx + 0.5f, s_cz + 0.5f);
        bool ok = s_held >= 0 && world_canPlace(s_held, s_cx, s_cz, s_heldRot, -1);
        render_scene(s_mode, s_cx, s_cz, s_held, s_heldRot, s_edge, ok);

        static int tick;
        if (++tick >= 20 && s_mode == MODE_LIVE) { tick = 0; s_dirty = true; }
        swiWaitForVBlank();
        if (s_dirty) { s_dirty = false; drawGame(); }
    }
}

// ---------------------------------------------------------------- title + create-a-sim
static const char* const MNAMES[] = { "Sam", "Leo", "Max", "Theo", "Jay", "Milo", "Omar", "Finn" };
static const char* const FNAMES[] = { "Ava", "Mia", "Zoe", "Ruby", "Ivy", "Nora", "Lily", "Maya" };
static const u16 SKIN[] = { RGB15(31, 25, 20), RGB15(29, 22, 16), RGB15(25, 18, 12), RGB15(20, 13, 8), RGB15(14, 9, 6), RGB15(31, 27, 23) };
static const u16 HAIR[] = { RGB15(4, 3, 3), RGB15(12, 7, 3), RGB15(28, 23, 10), RGB15(24, 9, 4), RGB15(22, 22, 22), RGB15(6, 12, 26), RGB15(28, 12, 20) };
static const u16 CLOTH[] = { RGB15(24, 6, 6), RGB15(6, 12, 26), RGB15(6, 22, 10), RGB15(28, 24, 6), RGB15(20, 8, 24), RGB15(29, 29, 29), RGB15(6, 6, 8), RGB15(30, 16, 6), RGB15(8, 22, 24) };
#define NEL(a) (int)(sizeof(a) / sizeof(a[0]))

int ui_title(void) {
    bool has = save_exists();
    int sel = 0;
    float spin = 0;
    bool redraw = true;
    OPT.camTop = 0;
    while (1) {
        scanKeys();
        int kd = keysDown();
        if ((kd & (KEY_UP | KEY_DOWN)) && has) { sel ^= 1; redraw = true; }
        if (redraw) {
        redraw = false;
        beginDraw();
        // logo: a big plumbob
        for (int y = 0; y < 40; y++) {
            int w = y < 20 ? y : 40 - y;
            fill(128 - w, 22 + y, w * 2, 1, y < 20 ? RGB15(8, 29, 10) : RGB15(4, 20, 6));
        }
        text(11, 8, "\x1b[32mP O C K E T\x1b[39m");
        text(12, 9, "L  I  F  E");
        button(10, 12, 12, "New Game", B_TITLE, 0, sel == 0);
        if (has) button(10, 14, 12, "Continue", B_TITLE, 1, sel == 1);
        text(1, 19, "Furniture: Kenney Furniture Kit");
        text(1, 20, "(CC0) - %d/%d models loaded", g_assetsFromKit, MDL_COUNT);
        text(1, 22, "X: top-down/2.5D  L/R: rotate");
        }
        int arg = -1;
        if (kd & KEY_TOUCH) {
            touchPosition tp; touchRead(&tp);
            if (hitTest(tp.px, tp.py, &arg) == B_TITLE) return arg;
        }
        if (kd & (KEY_A | KEY_START)) return sel;
        spin += 0.25f;
        OPT.camRot = ((int)(spin / 90)) & 3;
        cam_update(8, 5);
        render_scene(-1, -5, -5, -1, 0, 0, false);
        swiWaitForVBlank();
    }
}

void ui_cas(void) {
    // two housemates, editable one after another
    int skin[2] = { 1, 3 }, hair[2] = { 1, 0 }, shirt[2] = { 1, 0 }, pants[2] = { 6, 1 }, name[2] = { 0, 0 }, fem[2] = { 0, 1 };
    for (int who = 0; who < 2; who++) {
        int row = 0;
        float spin = 0;
        bool done = false;
        while (!done) {
            scanKeys();
            int kd = keysDown();
            if (kd & KEY_UP) row = (row + 5) % 6;
            if (kd & KEY_DOWN) row = (row + 1) % 6;
            int dlt = (kd & KEY_RIGHT) ? 1 : (kd & KEY_LEFT) ? -1 : 0;
            int touchedRow = -1;
            if (kd & KEY_TOUCH) {
                touchPosition tp; touchRead(&tp);
                int arg, id = hitTest(tp.px, tp.py, &arg);
                if (id == B_CAS) { touchedRow = arg >> 1; dlt = (arg & 1) ? 1 : -1; }
                if (id == B_TITLE) done = true;
            }
            if (touchedRow >= 0) row = touchedRow;
            if (dlt) {
                switch (row) {
                case 0: name[who] = (name[who] + dlt + 8) % 8; break;
                case 1: fem[who] ^= 1; break;
                case 2: skin[who] = (skin[who] + dlt + NEL(SKIN)) % NEL(SKIN); break;
                case 3: hair[who] = (hair[who] + dlt + NEL(HAIR)) % NEL(HAIR); break;
                case 4: shirt[who] = (shirt[who] + dlt + NEL(CLOTH)) % NEL(CLOTH); break;
                case 5: pants[who] = (pants[who] + dlt + NEL(CLOTH)) % NEL(CLOTH); break;
                }
            }
            if (kd & (KEY_A | KEY_START)) done = true;

            const char* nm = fem[who] ? FNAMES[name[who]] : MNAMES[name[who]];
            Sim t;
            sim_init(&t, nm, fem[who], SKIN[skin[who]], HAIR[hair[who]], CLOTH[shirt[who]], CLOTH[pants[who]], 0, 0);
            static int lastKd = -1;
            if (kd || lastKd == -1 || who != lastKd) {
            lastKd = who;
            beginDraw();
            fill(0, 0, 256, 15, COL_PANEL);
            text(1, 0, "\x1b[32mCREATE-A-SIM\x1b[39m  housemate %d/2", who + 1);
            static const char* const lab[6] = { "Name", "Body", "Skin", "Hair", "Shirt", "Pants" };
            for (int r = 0; r < 6; r++) {
                bool on = r == row;
                frame(4, 20 + r * 22, 248, 20, on ? COL_PANEL2 : COL_PANEL, on ? COL_HI2 : COL_EDGE);
            }
            // values (rows are 22px apart, so place text by pixel row / 8)
            for (int r = 0; r < 6; r++) {
                int ty = (20 + r * 22 + 6) / 8;
                printf("\x1b[%d;2H%-6s", ty, lab[r]);
                button(10, ty, 3, "<", B_CAS, r * 2, false);
                button(27, ty, 3, ">", B_CAS, r * 2 + 1, false);
                u16 sw = 0;
                switch (r) {
                case 0: printf("\x1b[%d;15H%s", ty, nm); break;
                case 1: printf("\x1b[%d;14H%s", ty, fem[who] ? "Feminine" : "Masculine"); break;
                case 2: sw = SKIN[skin[who]]; break;
                case 3: sw = HAIR[hair[who]]; break;
                case 4: sw = CLOTH[shirt[who]]; break;
                case 5: sw = CLOTH[pants[who]]; break;
                }
                if (sw) frame(14 * 8, 20 + r * 22 + 4, 9 * 8, 12, sw, RGB15(31, 31, 31));
            }
            button(9, 21, 14, who ? "Move In!" : "Next Sim", B_TITLE, 0, true);
            }
            spin += 2;
            render_cas(&t, spin);
            swiWaitForVBlank();
        }
        const char* nm = fem[who] ? FNAMES[name[who]] : MNAMES[name[who]];
        if (who == 1 && fem[0] == fem[1] && name[0] == name[1]) nm = fem[1] ? FNAMES[(name[1] + 1) % 8] : MNAMES[(name[1] + 1) % 8];
        sim_init(&W.sim[who], nm, fem[who], SKIN[skin[who]], HAIR[hair[who]], CLOTH[shirt[who]], CLOTH[pants[who]],
                 7.5f + who, 10.5f);
    }
    W.nsim = 2;
    W.active = 0;
    notify("Welcome home, %s and %s!", W.sim[0].name, W.sim[1].name);
}
