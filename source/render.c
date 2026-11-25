// PocketLife - 3D rendering on the top screen (top-down <-> 2.5D isometric)
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int g_lastPolys;
static int s_cull = POLY_CULL_BACK;
static int s_tex[FL_COUNT];
static u32 s_bodyFmt;
void render_initSims(void);

#define REG_DIFAMB (*(vu32*)0x040004C0)
#define POLY_RAM_COUNT (*(vu16*)0x04000604)
#define NP(x, y, z) ((u32)(((x) & 0x3FF) | (((y) & 0x3FF) << 10) | (((z) & 0x3FF) << 20)))

// ---------------------------------------------------------------- camera
static float camPitch = 90, camYaw = 0, camZoom = 6.5f, camX = 8, camZ = 6;
static const float ZOOMS[3] = { 4.0f, 6.2f, 9.0f };

static float angDiff(float a, float b) { return fmodf(b - a + 540.0f, 360.0f) - 180.0f; }

void cam_update(float tx, float tz) {
    float tp = OPT.camTop ? 90.0f : 34.0f;
    float ty = OPT.camTop ? OPT.camRot * 90.0f : 45.0f + OPT.camRot * 90.0f;
    camPitch += (tp - camPitch) * 0.15f;
    camYaw += angDiff(camYaw, ty) * 0.15f;
    camZoom += (ZOOMS[OPT.zoom] - camZoom) * 0.2f;
    camX += (tx - camX) * 0.18f;
    camZ += (tz - camZ) * 0.18f;
}

void cam_screenDir(int pad, int* dx, int* dz) {
    float y = camYaw * 0.0174533f;
    float fx = -sinf(y), fz = -cosf(y);   // screen up
    float rx = cosf(y), rz = -sinf(y);    // screen right
    float vx = 0, vz = 0;
    if (pad & KEY_UP)    { vx += fx; vz += fz; }
    if (pad & KEY_DOWN)  { vx -= fx; vz -= fz; }
    if (pad & KEY_RIGHT) { vx += rx; vz += rz; }
    if (pad & KEY_LEFT)  { vx -= rx; vz -= rz; }
    *dx = 0; *dz = 0;
    if (vx == 0 && vz == 0) return;
    float a = atan2f(vz, vx);
    int oct = (int)floorf(a / 0.785398f + 0.5f);
    static const s8 O[8][2] = { {1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1} };
    oct = ((oct % 8) + 8) % 8;
    *dx = O[oct][0]; *dz = O[oct][1];
}

static void setCamera(float tx, float ty, float tz, float zoom, float pitch, float yaw) {
    float hw = zoom, hh = zoom * 0.75f;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof32(floattof32(-hw), floattof32(hw), floattof32(-hh), floattof32(hh), floattof32(0.5f), floattof32(80.0f));
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    float p = pitch * 0.0174533f, y = yaw * 0.0174533f, D = 35.0f;
    float ex = tx + D * cosf(p) * sinf(y), ey = ty + D * sinf(p), ez = tz + D * cosf(p) * cosf(y);
    float ux = -sinf(y), uz = -cosf(y);
    gluLookAtf32(floattof32(ex), floattof32(ey), floattof32(ez),
                 floattof32(tx), floattof32(ty), floattof32(tz),
                 floattof32(ux * 0.7f), floattof32(0.7f), floattof32(uz * 0.7f));
    // the light is set in world space, after the camera
    glLight(0, RGB15(25, 25, 23), floattov10(-0.34f), floattov10(-0.84f), floattov10(-0.40f));
    glLight(1, RGB15(6, 7, 9), floattov10(0.5f), floattov10(-0.3f), floattov10(0.6f));
}

// ---------------------------------------------------------------- textures
static u16 texbuf[16 * 16];
static u32 rng = 12345;
static int rnd(int n) { rng = rng * 1103515245u + 12345u; return (rng >> 16) % n; }
static u16 C(int r, int g, int b) {
    r = r < 0 ? 0 : r > 31 ? 31 : r; g = g < 0 ? 0 : g > 31 ? 31 : g; b = b < 0 ? 0 : b > 31 ? 31 : b;
    return RGB15(r, g, b) | BIT(15);
}

static void makeTex(int type) {
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            u16 c = 0;
            switch (type) {
            case FL_GRASS: { int n = rnd(5); c = C(8 + n / 2, 17 + n, 6 + n / 2); if (rnd(17) == 0) c = C(14, 23, 8); break; }
            case FL_WOOD: {
                int plank = y / 4, off = (plank & 1) ? 8 : 0;
                int seam = (y % 4 == 3) || ((x + off) % 16 == 0);
                int g = rnd(3) + ((x * 7 + plank * 13) % 5 == 0);
                c = seam ? C(13, 8, 4) : C(22 + g - plank % 2, 15 + g, 8 + g / 2);
                break; }
            case FL_TILE: {
                int grout = (x % 8 == 0) || (y % 8 == 0);
                int chk = ((x / 8) + (y / 8)) & 1;
                c = grout ? C(18, 18, 17) : chk ? C(29, 29, 27) : C(21, 25, 28);
                break; }
            case FL_CARPET: { int n = rnd(4); c = C(14 + n, 9 + n / 2, 20 + n); break; }
            case FL_STONE: {
                int row = y / 4, off = (row & 1) ? 4 : 0;
                int seam = (y % 4 == 0) || ((x + off) % 8 == 0);
                int n = rnd(3);
                c = seam ? C(11, 11, 11) : C(19 + n, 19 + n, 18 + n);
                break; }
            }
            texbuf[y * 16 + x] = c;
        }
    glGenTextures(1, &s_tex[type]);
    glBindTexture(GL_TEXTURE_2D, s_tex[type]);
    glTexImage2D(0, 0, GL_RGBA, TEXTURE_SIZE_16, TEXTURE_SIZE_16, 0,
                 TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T, (u8*)texbuf);
}

// ---------------------------------------------------------------- culling self-test
// the DS counts stored polygons, so we can find out which winding it treats as
// the front face instead of guessing.
static int countTri(int cullMode) {
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrthof32(floattof32(-1), floattof32(1), floattof32(-1), floattof32(1), floattof32(0.1f), floattof32(10));
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glPolyFmt(POLY_ALPHA(31) | cullMode);
    glBindTexture(GL_TEXTURE_2D, 0);
    glColor(RGB15(31, 31, 31));
    glBegin(GL_TRIANGLES);
    glVertex3v16(floattov16(-0.5f), floattov16(-0.5f), floattov16(-1));   // counter-clockwise on screen
    glVertex3v16(floattov16(0.5f), floattov16(-0.5f), floattov16(-1));
    glVertex3v16(floattov16(0.0f), floattov16(0.5f), floattov16(-1));
    glEnd();
    int n = 0;
    glGetInt(GL_GET_POLYGON_RAM_COUNT, &n);
    glFlush(0);
    swiWaitForVBlank();
    return n;
}

int render_cullMode(void) { return s_cull; }

void render_init(void) {
    glInit();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ANTIALIAS);
    glEnable(GL_BLEND);
    glEnable(GL_OUTLINE);
    for (int i = 0; i < 8; i++) glSetOutlineColor(i, RGB15(4, 4, 6));
    glClearColor(15, 21, 27, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    vramSetBankA(VRAM_A_TEXTURE);
    for (int t = 0; t < FL_COUNT; t++) makeTex(t);
    render_initSims();
    glMaterialf(GL_SPECULAR, 0);
    glMaterialf(GL_EMISSION, 0);
    glMaterialf(GL_AMBIENT, RGB15(12, 12, 12));
    glMaterialf(GL_DIFFUSE, RGB15(24, 24, 24));

    int back = countTri(POLY_CULL_BACK), front = countTri(POLY_CULL_FRONT);
    if (back > 0 && front == 0) s_cull = POLY_CULL_BACK;       // OpenGL-style CCW fronts
    else if (back == 0 && front > 0) s_cull = POLY_CULL_FRONT; // hardware sees it the other way round
    else s_cull = POLY_CULL_NONE;
}

// ---------------------------------------------------------------- primitives
static inline v16 V(float f) { return floattov16(f); }

static void box(float x0, float y0, float z0, float x1, float y1, float z1, u16 col) {
    u16 c = col & 0x7FFF;
    u16 amb = RGB15(((c & 31) * 5) >> 3, (((c >> 5) & 31) * 5) >> 3, (((c >> 10) & 31) * 5) >> 3);
    REG_DIFAMB = c | BIT(15) | (amb << 16);
    glBegin(GL_QUADS);
    glNormal(NP(0, 0, inttov10(1) - 1));
    glVertex3v16(V(x0), V(y0), V(z1)); glVertex3v16(V(x1), V(y0), V(z1));
    glVertex3v16(V(x1), V(y1), V(z1)); glVertex3v16(V(x0), V(y1), V(z1));
    glNormal(NP(0, 0, -inttov10(1) + 1));
    glVertex3v16(V(x1), V(y0), V(z0)); glVertex3v16(V(x0), V(y0), V(z0));
    glVertex3v16(V(x0), V(y1), V(z0)); glVertex3v16(V(x1), V(y1), V(z0));
    glNormal(NP(inttov10(1) - 1, 0, 0));
    glVertex3v16(V(x1), V(y0), V(z1)); glVertex3v16(V(x1), V(y0), V(z0));
    glVertex3v16(V(x1), V(y1), V(z0)); glVertex3v16(V(x1), V(y1), V(z1));
    glNormal(NP(-inttov10(1) + 1, 0, 0));
    glVertex3v16(V(x0), V(y0), V(z0)); glVertex3v16(V(x0), V(y0), V(z1));
    glVertex3v16(V(x0), V(y1), V(z1)); glVertex3v16(V(x0), V(y1), V(z0));
    glNormal(NP(0, inttov10(1) - 1, 0));
    glVertex3v16(V(x0), V(y1), V(z1)); glVertex3v16(V(x1), V(y1), V(z1));
    glVertex3v16(V(x1), V(y1), V(z0)); glVertex3v16(V(x0), V(y1), V(z0));
    glNormal(NP(0, -inttov10(1) + 1, 0));
    glVertex3v16(V(x0), V(y0), V(z0)); glVertex3v16(V(x1), V(y0), V(z0));
    glVertex3v16(V(x1), V(y0), V(z1)); glVertex3v16(V(x0), V(y0), V(z1));
    glEnd();
}

static void flatQuad(float x0, float z0, float x1, float z1, float y, u16 col, int alpha, int id) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glPolyFmt(POLY_ALPHA(alpha) | POLY_CULL_NONE | POLY_ID(id));
    glColor(col);
    glBegin(GL_QUADS);
    glVertex3v16(V(x0), V(y), V(z0)); glVertex3v16(V(x0), V(y), V(z1));
    glVertex3v16(V(x1), V(y), V(z1)); glVertex3v16(V(x1), V(y), V(z0));
    glEnd();
}

static void texQuad(float x0, float z0, float x1, float z1, float y, int tex) {
    glBindTexture(GL_TEXTURE_2D, s_tex[tex]);
    glColor(RGB15(31, 31, 31));
    glBegin(GL_QUADS);
    float u = (x1 - x0) * 16, v = (z1 - z0) * 16;
    glTexCoord2t16(0, 0);                         glVertex3v16(V(x0), V(y), V(z0));
    glTexCoord2t16(0, floattot16(v));             glVertex3v16(V(x0), V(y), V(z1));
    glTexCoord2t16(floattot16(u), floattot16(v)); glVertex3v16(V(x1), V(y), V(z1));
    glTexCoord2t16(floattot16(u), 0);             glVertex3v16(V(x1), V(y), V(z0));
    glEnd();
}

static void callModel(int mdl) { glCallList((u32*)g_models[mdl].list); }

// ---------------------------------------------------------------- lot
static void drawFloors(void) {
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(0));
    // grass under everything (drawn in 4x4 blocks so the texture coords stay small)
    for (int bz = -12; bz < LOT_H + 12; bz += 8)
        for (int bx = -12; bx < LOT_W + 12; bx += 8)
            texQuad(bx, bz, bx + 8, bz + 8, -0.01f, FL_GRASS);
    for (int z = 0; z < LOT_H; z++) {
        int x = 0;
        while (x < LOT_W) {
            int f = W.floor[z][x];
            if (f == FL_GRASS) { x++; continue; }
            int x1 = x;
            while (x1 + 1 < LOT_W && W.floor[z][x1 + 1] == f) x1++;
            texQuad(x, z, x1 + 1, z + 1, 0.0f, f);
            x = x1 + 1;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

static float s_ex, s_ez;   // horizontal direction toward the camera, set per frame
static bool wallCut(float mx, float mz, float fx, float fz) {
    if (OPT.walls == 2) return true;
    if (OPT.walls == 0 || camPitch > 70) return false;
    return (mx - fx) * s_ex + (mz - fz) * s_ez > -0.3f;
}

static void drawWalls(float fx, float fz) {
    s_ex = sinf(camYaw * 0.0174533f); s_ez = cosf(camYaw * 0.0174533f);
    glPolyFmt(POLY_ALPHA(31) | s_cull | POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_ID(60));
    float low = OPT.walls == 2 ? 0.08f : 0.16f;
    for (int z = 0; z <= LOT_H; z++)
        for (int x = 0; x <= LOT_W; x++) {
            if (x < LOT_W && W.hw[z][x]) {
                glPushMatrix();
                glTranslatef32(floattof32(x + 0.5f), 0, floattof32(z));
                if (wallCut(x + 0.5f, z, fx, fz)) glScalef32(inttof32(1), floattof32(low), inttof32(1));
                callModel(MDL_WALL - 1 + W.hw[z][x]);
                glPopMatrix(1);
            }
            if (z < LOT_H && W.vw[z][x]) {
                glPushMatrix();
                glTranslatef32(floattof32(x), 0, floattof32(z + 0.5f));
                glRotateY(90);
                if (wallCut(x, z + 0.5f, fx, fz)) glScalef32(inttof32(1), floattof32(low), inttof32(1));
                callModel(MDL_WALL - 1 + W.vw[z][x]);
                glPopMatrix(1);
            }
        }
    // corner posts fill the gaps where perpendicular walls meet
    float t = f32tofloat(g_models[MDL_WALL].d) * 0.5f + 0.005f;
    float H = f32tofloat(g_wallHeight);
    for (int z = 0; z <= LOT_H; z++)
        for (int x = 0; x <= LOT_W; x++) {
            bool h = (x > 0 && W.hw[z][x - 1]) || (x < LOT_W && W.hw[z][x]);
            bool v = (z > 0 && W.vw[z - 1][x]) || (z < LOT_H && W.vw[z][x]);
            if (!h || !v) continue;
            float hh = wallCut(x, z, fx, fz) ? H * low : H;
            box(x - t, 0, z - t, x + t, hh, z + t, g_wallColor);
        }
}

static void drawObject(int def, float cx, float cz, int rot, int id, int alpha) {
    glPolyFmt(POLY_ALPHA(alpha) | s_cull | POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_ID(id));
    glPushMatrix();
    glTranslatef32(floattof32(cx), 0, floattof32(cz));
    if (rot) glRotateY(rot * 90.0f);
    callModel(g_objDefs[def].model);
    glPopMatrix(1);
}

static void drawObjects(void) {
    for (int i = 0; i < W.nobj; i++) {
        PlacedObj* o = &W.obj[i];
        int w, d;
        obj_footprint(o, &w, &d);
        drawObject(o->def, o->x + w * 0.5f, o->z + d * 0.5f, o->rot, 1 + (i % 50), 31);
    }
}

// ---------------------------------------------------------------- sims
// Urbz-style street Sims: smooth-shaded parts from tools/character.py, coloured
// at draw time, with a drawn face texture (blinks, smiles, frowns).
extern float simHeight(void);

enum { FACE_NEUTRAL, FACE_HAPPY, FACE_SAD, FACE_BLINK, FACE_COUNT };
static int s_faceTex[2][FACE_COUNT];
static int s_stageTex[2];
static u16 facebuf[32 * 32];

static void fpix(int x, int y, u16 c) { if (x >= 0 && y >= 0 && x < 32 && y < 32) facebuf[y * 32 + x] = c | BIT(15); }
static void fell(float cx, float cy, float rx, float ry, u16 c) {
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) fpix(x, y, c);
        }
}
static void fline(int x0, int y0, int x1, int y1, int th, u16 c) {
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    if (!n) n = 1;
    for (int i = 0; i <= n; i++) {
        int x = x0 + (x1 - x0) * i / n, y = y0 + (y1 - y0) * i / n;
        for (int t = 0; t < th; t++) fpix(x, y + t, c);
    }
}

static void makeFace(int fem, int expr) {
    memset(facebuf, 0, sizeof(facebuf));             // alpha 0 = see-through to the skin
    u16 ink = RGB15(4, 3, 3), white = RGB15(31, 31, 30), iris = RGB15(7, 5, 4), lip = RGB15(17, 6, 6);
    for (int side = 0; side < 2; side++) {
        float ex = side ? 21.5f : 10.5f;
        int sgn = side ? 1 : -1;
        if (expr == FACE_BLINK) {
            fline((int)ex - 4, 13, (int)ex + 4, 13, 2, ink);
        } else {
            fell(ex, 12.5f, 4.2f, 5.0f, ink);          // outline
            fell(ex, 12.5f, 3.3f, 4.1f, white);
            fell(ex - sgn * 0.6f, 13.2f, 2.3f, 2.9f, iris);
            fpix((int)(ex - sgn * 0.6f) - 1, 11, white);  // sparkle
            if (expr == FACE_HAPPY)   // happy squint: cut off the bottom of the eye
                for (int y = 15; y < 19; y++) for (int x = -5; x <= 5; x++) if ((int)ex + x >= 0 && (int)ex + x < 32) facebuf[y * 32 + (int)ex + x] = 0;
        }
        // eyebrows: chunky, angled by mood
        int inner = expr == FACE_SAD ? 4 : expr == FACE_HAPPY ? 5 : 6;
        int outer = expr == FACE_SAD ? 7 : 5;
        if (side) fline(18, inner, 26, outer, 2, ink); else fline(5, outer, 13, inner, 2, ink);
        if (fem && expr != FACE_BLINK) {               // lashes
            int ox = side ? 25 : 6;
            fline(ox, 9, ox + (side ? 2 : -2), 7, 1, ink);
            fline(ox + (side ? -1 : 1), 8, ox + (side ? 0 : 0), 6, 1, ink);
        }
    }
    // mouth
    if (expr == FACE_HAPPY) {
        for (int x = -5; x <= 5; x++) { int y = 24 + (25 - x * x) / 10; fpix(16 + x, y, ink); fpix(16 + x, y - 1, ink); }
        fell(16, 26.5f, 3.2f, 1.6f, lip);
    } else if (expr == FACE_SAD) {
        for (int x = -4; x <= 4; x++) { int y = 27 - (16 - x * x) / 6; fpix(16 + x, y, ink); fpix(16 + x, y + 1, ink); }
    } else {
        fline(12, 25, 20, 25, 2, ink);
        fpix(20, 24, ink);                             // a little smirk
    }
    glGenTextures(1, &s_faceTex[fem][expr]);
    glBindTexture(GL_TEXTURE_2D, s_faceTex[fem][expr]);
    glTexImage2D(0, 0, GL_RGBA, TEXTURE_SIZE_32, TEXTURE_SIZE_32, 0, TEXGEN_TEXCOORD, (u8*)facebuf);
}

// brick wall + concrete for the Create-A-Sim stage
static void makeStageTex(int which) {
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            u16 c;
            if (which == 0) {
                int row = y / 4, off = (row & 1) ? 4 : 0;
                bool mortar = (y % 4 == 3) || ((x + off) % 8 == 7);
                int n = rnd(4);
                c = mortar ? C(19, 18, 16) : C(20 + n - (row % 2) * 2, 8 + n / 2, 6 + n / 2);
            } else {
                int n = rnd(3);
                c = (x == 15 || y == 15) ? C(12, 12, 13) : C(17 + n, 17 + n, 18 + n);
            }
            texbuf[y * 16 + x] = c;
        }
    glGenTextures(1, &s_stageTex[which]);
    glBindTexture(GL_TEXTURE_2D, s_stageTex[which]);
    glTexImage2D(0, 0, GL_RGBA, TEXTURE_SIZE_16, TEXTURE_SIZE_16, 0,
                 TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T, (u8*)texbuf);
}

void render_initSims(void) {
    for (int f = 0; f < 2; f++)
        for (int e = 0; e < FACE_COUNT; e++) makeFace(f, e);
    makeStageTex(0);
    makeStageTex(1);
}

static void mat(u16 col) {
    u16 c = col & 0x7FFF;
    u16 amb = RGB15(((c & 31) * 9) >> 4, (((c >> 5) & 31) * 9) >> 4, (((c >> 10) & 31) * 9) >> 4);
    REG_DIFAMB = c | BIT(15) | (amb << 16);
}
static void part(int id, u16 col) { mat(col); glCallList((u32*)g_charParts[id]); }

typedef struct { float legA[2], knee[2], armA[2], armZ[2], elbow[2], bob, nod, lean, turn; } Pose;

static void computePose(const Sim* s, bool walking, Pose* p) {
    memset(p, 0, sizeof(*p));
    float t = s->anim;
    for (int k = 0; k < 2; k++) { p->armZ[k] = 7; p->elbow[k] = -12; p->knee[k] = 3; }
    switch (s->pose) {
    case POSE_SIT:
        for (int k = 0; k < 2; k++) { p->legA[k] = -86; p->knee[k] = 84; p->armA[k] = -18; p->elbow[k] = -45; }
        p->lean = -4;
        break;
    case POSE_LIE:
        for (int k = 0; k < 2; k++) { p->armZ[k] = 4; p->elbow[k] = -5; }
        p->nod = -10;
        break;
    case POSE_DANCE: {
        float a = sinf(t * 0.22f), b = cosf(t * 0.22f);
        p->armA[0] = -140 + a * 30; p->armA[1] = -140 - a * 30;
        p->elbow[0] = p->elbow[1] = -35;
        p->armZ[0] = p->armZ[1] = 25;
        p->legA[0] = a * 12; p->legA[1] = -a * 12;
        p->knee[0] = 20 + fabsf(b) * 25; p->knee[1] = 20 + fabsf(a) * 25;
        p->bob = -fabsf(b) * 0.04f; p->turn = a * 20; p->nod = b * 10;
        break; }
    case POSE_TALK:
        p->armA[0] = -35 + sinf(t * 0.13f) * 22; p->elbow[0] = -70 + sinf(t * 0.21f) * 20; p->armZ[0] = 14;
        p->armA[1] = -10; p->elbow[1] = -25;
        p->nod = sinf(t * 0.17f) * 6; p->turn = sinf(t * 0.05f) * 8;
        break;
    case POSE_WAVE:
        p->armA[0] = -160; p->elbow[0] = -25; p->armZ[0] = 18 + sinf(t * 0.35f) * 18;
        p->nod = 4;
        break;
    case POSE_USE:
        for (int k = 0; k < 2; k++) { p->armA[k] = -45 + sinf(t * 0.25f + k * 2) * 8; p->elbow[k] = -50; }
        p->nod = 10; p->lean = 6;
        break;
    default:
        if (walking) {
            float w = sinf(t * 0.26f);
            p->legA[0] = w * 30; p->legA[1] = -w * 30;
            for (int k = 0; k < 2; k++) {
                p->knee[k] = 6 + (p->legA[k] > 0 ? p->legA[k] * 1.3f : 0);
                p->armA[k] = -p->legA[k] * 0.8f;
                p->elbow[k] = -20 - fabsf(p->legA[k]) * 0.4f;
            }
            p->bob = fabsf(cosf(t * 0.26f)) * 0.02f - 0.01f;
            p->turn = w * 6; p->lean = 4;
        } else {
            // street idle: weight shift, breathing, glancing around
            float b = sinf(t * 0.045f);
            p->legA[0] = -3; p->legA[1] = 4; p->knee[1] = 8;
            p->armA[0] = b * 3; p->armA[1] = -b * 3; p->armZ[0] = 8; p->armZ[1] = 9;
            p->bob = b * 0.004f;
            p->turn = sinf(t * 0.013f) * 18;
        }
    }
}

static void drawSimBody(const Sim* s, bool walking, int expr) {
    Pose P;
    computePose(s, walking, &P);
    const bool hoodie = s->top == 0;
    glTranslatef32(0, floattof32(P.bob), 0);
    // legs
    for (int k = 0; k < 2; k++) {
        float side = k ? -1 : 1;
        glPushMatrix();
        glTranslatef32(floattof32(0.056f * side), floattof32(0.40f), 0);
        if (P.legA[k]) glRotateX(P.legA[k]);
        part(CP_UPPER_LEG, s->pants);
        glTranslatef32(0, floattof32(-0.18f), 0);
        if (P.knee[k]) glRotateX(P.knee[k]);
        part(CP_LOWER_LEG, s->pants);
        glTranslatef32(0, floattof32(-0.165f), 0);
        float foot = -(P.legA[k] + P.knee[k]) * (s->pose == POSE_SIT ? 1.0f : 0.6f);
        if (foot) glRotateX(foot);
        part(CP_SHOE, s->shoes);
        part(CP_SOLE, RGB15(30, 30, 29));
        glPopMatrix(1);
    }
    part(CP_HIPS, s->pants);
    // upper body
    glPushMatrix();
    glTranslatef32(0, floattof32(0.36f), 0);
    if (P.lean) glRotateX(P.lean);
    if (P.turn) glRotateY(P.turn * 0.5f);
    glTranslatef32(0, floattof32(-0.36f), 0);
    part(hoodie ? CP_TORSO_HOODIE : CP_TORSO_TEE, s->shirt);
    for (int k = 0; k < 2; k++) {
        float side = k ? -1 : 1;
        glPushMatrix();
        glTranslatef32(floattof32(0.158f * side), floattof32(0.62f), 0);
        if (P.armZ[k]) glRotateZ(P.armZ[k] * side);
        if (P.armA[k]) glRotateX(P.armA[k]);
        part(hoodie ? CP_UARM_HOODIE : CP_UARM_TEE, s->shirt);
        glTranslatef32(0, floattof32(-0.15f), 0);
        if (P.elbow[k]) glRotateX(P.elbow[k]);
        part(hoodie ? CP_LARM_HOODIE : CP_LARM_TEE, hoodie ? s->shirt : s->skin);
        glTranslatef32(0, floattof32(-0.14f), 0);
        part(CP_HAND, s->skin);
        glPopMatrix(1);
    }
    // head
    glTranslatef32(0, floattof32(0.665f), 0);
    if (P.turn) glRotateY(P.turn * 0.6f);
    if (P.nod) glRotateX(P.nod);
    part(CP_HEAD, s->skin);
    part(CP_HAIR_0 + (s->hairStyle % CP_HAIR_STYLES), s->hair);
    // face decal (unlit, see-through texture), same polygon ID as the body
    u32 pf = s_bodyFmt;
    glPolyFmt(pf & ~(POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1));
    glBindTexture(GL_TEXTURE_2D, s_faceTex[s->female ? 1 : 0][expr]);
    glColor(RGB15(31, 31, 31));
    float z = 0.137f, x0 = -0.11f, x1 = 0.11f, y0 = 0.02f, y1 = 0.24f;
    glBegin(GL_QUADS);
    glTexCoord2t16(0, inttot16(32));              glVertex3v16(V(x0), V(y0), V(z));
    glTexCoord2t16(inttot16(32), inttot16(32));   glVertex3v16(V(x1), V(y0), V(z));
    glTexCoord2t16(inttot16(32), 0);              glVertex3v16(V(x1), V(y1), V(z));
    glTexCoord2t16(0, 0);                         glVertex3v16(V(x0), V(y1), V(z));
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glPolyFmt(pf);
    glPopMatrix(1);
}

static int faceFor(const Sim* s, int idx) {
    if (s->pose == POSE_LIE && s->qlen && (s->q[0].act == ACT_SLEEP || s->q[0].act == ACT_NAP)) return FACE_BLINK;
    int t = ((int)s->anim + idx * 97) % 230;
    if (t < 7) return FACE_BLINK;
    if (s->pose == POSE_DANCE || s->pose == POSE_WAVE) return FACE_HAPPY;
    float m = sim_mood(s);
    return m > 62 ? FACE_HAPPY : m < 35 ? FACE_SAD : FACE_NEUTRAL;
}

static void plumbob(float x, float y, float z, float mood, float spin) {
    u16 col = mood > 60 ? RGB15(6, 31, 8) : mood > 35 ? RGB15(31, 28, 4) : RGB15(31, 6, 4);
    u16 hi = mood > 60 ? RGB15(18, 31, 18) : mood > 35 ? RGB15(31, 31, 18) : RGB15(31, 18, 16);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPolyFmt(POLY_ALPHA(28) | POLY_CULL_NONE | POLY_ID(61));
    glPushMatrix();
    glTranslatef32(floattof32(x), floattof32(y), floattof32(z));
    glRotateY(spin);
    float r = 0.09f, h = 0.16f;
    v16 px[4] = { V(r), 0, V(-r), 0 }, pz[4] = { 0, V(r), 0, V(-r) };
    glBegin(GL_TRIANGLES);
    for (int k = 0; k < 4; k++) {
        int n = (k + 1) & 3;
        glColor((k & 1) ? col : hi);
        glVertex3v16(0, V(h), 0); glVertex3v16(px[k], 0, pz[k]); glVertex3v16(px[n], 0, pz[n]);
        glColor((k & 1) ? hi : col);
        glVertex3v16(0, V(-h), 0); glVertex3v16(px[n], 0, pz[n]); glVertex3v16(px[k], 0, pz[k]);
    }
    glEnd();
    glPopMatrix(1);
}

static void drawSim(int i, bool active, float spin) {
    const Sim* s = &W.sim[i];
    float H = simHeight();
    s_bodyFmt = POLY_ALPHA(31) | s_cull | POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_ID(55 + i);
    glPolyFmt(s_bodyFmt);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPushMatrix();
    float baseY = s->y;
    if (s->pose == POSE_SIT) baseY = s->y - 0.40f * H;
    if (s->pose == POSE_LIE) baseY = s->y + 0.08f * H;
    if (baseY < 0) baseY = 0;
    glTranslatef32(floattof32(s->x), floattof32(baseY), floattof32(s->z));
    glRotateY(s->face);
    if (s->pose == POSE_LIE) { glTranslatef32(0, 0, 0); glRotateX(-90); }
    glScalef32(floattof32(H), floattof32(H), floattof32(H));
    drawSimBody(s, s->state == SS_WALK, faceFor(s, i));
    glPopMatrix(1);
    // soft shadow
    if (s->pose != POSE_LIE) flatQuad(s->x - 0.2f, s->z - 0.2f, s->x + 0.2f, s->z + 0.2f, 0.012f, RGB15(2, 4, 2), 12, 50);
    if (active) {
        float top = (s->pose == POSE_LIE) ? baseY + 0.35f : baseY + H + 0.22f;
        plumbob(s->x, top + sinf(spin * 0.05f) * 0.03f, s->z, sim_mood(s), spin);
    }
}

// ---------------------------------------------------------------- main scene
void render_scene(int mode, int cx, int cz, int heldDef, int heldRot, int buildEdge, bool ghostOk) {
    static float spin = 0;
    spin += 3;
    setCamera(camX, 0.4f, camZ, camZoom, camPitch, camYaw);
    drawFloors();
    drawWalls(cx + 0.5f, cz + 0.5f);
    drawObjects();
    for (int i = 0; i < W.nsim; i++) drawSim(i, i == W.active && mode == MODE_LIVE, spin);

    // cursor / placement ghost
    if (mode == MODE_BUY && heldDef >= 0) {
        PlacedObj t = { (u8)heldDef, (s8)cx, (s8)cz, (u8)heldRot, 0 };
        int w, d;
        obj_footprint(&t, &w, &d);
        drawObject(heldDef, cx + w * 0.5f, cz + d * 0.5f, heldRot, 62, 16);
        flatQuad(cx, cz, cx + w, cz + d, 0.02f, ghostOk ? RGB15(8, 31, 10) : RGB15(31, 6, 4), 14, 63 - 1);
    } else if (mode == MODE_BUILD) {
        u16 col = RGB15(31, 28, 6);
        flatQuad(cx, cz, cx + 1, cz + 1, 0.02f, RGB15(31, 31, 31), 8, 51);
        if (buildEdge == 0) flatQuad(cx - 0.05f, cz - 0.08f, cx + 1.05f, cz + 0.08f, 0.04f, col, 24, 52);
        else                flatQuad(cx - 0.08f, cz - 0.05f, cx + 0.08f, cz + 1.05f, 0.04f, col, 24, 52);
    } else if (mode == MODE_LIVE) {
        int oi = world_objAt(cx, cz, true);
        if (oi >= 0) {
            int w, d;
            obj_footprint(&W.obj[oi], &w, &d);
            flatQuad(W.obj[oi].x, W.obj[oi].z, W.obj[oi].x + w, W.obj[oi].z + d, 0.02f, RGB15(31, 31, 10), 12, 51);
        } else {
            flatQuad(cx + 0.08f, cz + 0.08f, cx + 0.92f, cz + 0.92f, 0.02f, RGB15(31, 31, 31), 12, 51);
        }
    }
    g_lastPolys = POLY_RAM_COUNT;
    glFlush(0);
}

// Create-a-Sim preview: a street corner with a brick wall
void render_cas(const Sim* s, float spin) {
    setCamera(0, 0.5f, 0, 0.95f, 10, 0);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(0));
    glColor(RGB15(31, 31, 31));
    // pavement
    glBindTexture(GL_TEXTURE_2D, s_stageTex[1]);
    glBegin(GL_QUADS);
    glTexCoord2t16(0, 0);                       glVertex3v16(V(-2), 0, V(-1));
    glTexCoord2t16(0, inttot16(48));            glVertex3v16(V(-2), 0, V(2));
    glTexCoord2t16(inttot16(64), inttot16(48)); glVertex3v16(V(2), 0, V(2));
    glTexCoord2t16(inttot16(64), 0);            glVertex3v16(V(2), 0, V(-1));
    glEnd();
    // brick wall
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(1));
    glBindTexture(GL_TEXTURE_2D, s_stageTex[0]);
    glBegin(GL_QUADS);
    glTexCoord2t16(0, inttot16(40));            glVertex3v16(V(-2), 0, V(-1));
    glTexCoord2t16(inttot16(64), inttot16(40)); glVertex3v16(V(2), 0, V(-1));
    glTexCoord2t16(inttot16(64), 0);            glVertex3v16(V(2), V(1.6f), V(-1));
    glTexCoord2t16(0, 0);                       glVertex3v16(V(-2), V(1.6f), V(-1));
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    // a splash of paint on the wall
    static const u16 paint[3] = { RGB15(31, 8, 20), RGB15(6, 26, 31), RGB15(31, 27, 4) };
    for (int k = 0; k < 3; k++) {
        glPolyFmt(POLY_ALPHA(22) | POLY_CULL_NONE | POLY_ID(2 + k));
        glColor(paint[k]);
        float cx = -1.2f + k * 1.15f, cy = 0.55f + (k & 1) * 0.35f;
        glBegin(GL_TRIANGLES);
        for (int j = 0; j < 6; j++) {
            float a0 = j * 1.047f, a1 = (j + 1) * 1.047f, r0 = 0.22f + (j & 1) * 0.08f, r1 = 0.22f + ((j + 1) & 1) * 0.08f;
            glVertex3v16(V(cx), V(cy), V(-0.99f));
            glVertex3v16(V(cx + cosf(a0) * r0), V(cy + sinf(a0) * r0 * 0.7f), V(-0.99f));
            glVertex3v16(V(cx + cosf(a1) * r1), V(cy + sinf(a1) * r1 * 0.7f), V(-0.99f));
        }
        glEnd();
    }
    flatQuad(-0.28f, -0.22f, 0.28f, 0.22f, 0.005f, RGB15(3, 3, 4), 12, 50);

    Sim t = *s;
    t.pose = POSE_STAND;
    t.state = SS_IDLE;
    t.anim = spin * 4;
    float H = simHeight();
    s_bodyFmt = POLY_ALPHA(31) | s_cull | POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_ID(55);
    glPolyFmt(s_bodyFmt);
    glPushMatrix();
    glRotateY(sinf(spin * 0.02f) * 50);
    float k = 0.92f / H;
    glScalef32(floattof32(H * k), floattof32(H * k), floattof32(H * k));
    drawSimBody(&t, false, ((int)spin % 180) < 4 ? FACE_BLINK : FACE_HAPPY);
    glPopMatrix(1);
    g_lastPolys = POLY_RAM_COUNT;
    glFlush(0);
}
