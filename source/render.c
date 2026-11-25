// PocketLife - 3D rendering on the top screen (top-down <-> 2.5D isometric)
#include "game.h"
#include <math.h>
#include <string.h>

int g_lastPolys;
static int s_cull = POLY_CULL_BACK;
static int s_tex[FL_COUNT];

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
extern float simHeight(void);

static void limb(float px, float py, float ang, float angZ, float x0, float x1, float len, float t, u16 col, u16 endCol) {
    glPushMatrix();
    glTranslatef32(floattof32(px), floattof32(py), 0);
    if (ang) glRotateX(ang);
    if (angZ) glRotateZ(angZ);
    box(x0, -len, -t, x1, 0, t, col);
    if (endCol) box(x0 - 0.005f, -len - 0.04f, -t - 0.005f, x1 + 0.005f, -len + 0.02f, t + 0.01f, endCol);
    glPopMatrix(1);
}

static void drawSimBody(const Sim* s, bool walking) {
    float t = s->anim;
    float legA = 0, legB = 0, armA = 0, armB = 0, armZA = 0, armZB = 0, bob = 0;
    switch (s->pose) {
    case POSE_SIT: legA = legB = -88; armA = armB = -25; break;
    case POSE_LIE: armA = armB = -8; break;
    case POSE_DANCE:
        armA = -150 + sinf(t * 0.2f) * 30; armB = -150 - sinf(t * 0.2f) * 30;
        legA = sinf(t * 0.2f) * 15; legB = -legA; bob = fabsf(sinf(t * 0.2f)) * 0.04f; break;
    case POSE_TALK: armA = -30 + sinf(t * 0.12f) * 25; armZA = 10; break;
    case POSE_WAVE: armA = -170 + sinf(t * 0.3f) * 15; break;
    case POSE_USE: armA = armB = -55 + sinf(t * 0.25f) * 8; break;
    default:
        if (walking) { legA = sinf(t * 0.28f) * 30; legB = -legA; armA = -legA * 0.8f; armB = legA * 0.8f; }
        else { armA = armB = sinf(t * 0.05f) * 2; }
    }
    u16 dark = RGB15(3, 3, 4);
    u16 shoe = RGB15(6, 5, 5);
    glTranslatef32(0, floattof32(bob), 0);
    // legs
    limb(0.065f, 0.46f, legA, 0, -0.045f, 0.045f, 0.42f, 0.05f, s->pants, shoe);
    limb(-0.065f, 0.46f, legB, 0, -0.045f, 0.045f, 0.42f, 0.05f, s->pants, shoe);
    // hips + torso
    box(-0.13f, 0.40f, -0.07f, 0.13f, 0.50f, 0.07f, s->pants);
    box(-0.15f, 0.50f, -0.08f, 0.15f, 0.79f, 0.08f, s->shirt);
    // arms (sleeve + hand)
    limb(0.19f, 0.77f, armA, armZA, -0.04f, 0.04f, 0.27f, 0.04f, s->shirt, s->skin);
    limb(-0.19f, 0.77f, armB, -armZB, -0.04f, 0.04f, 0.27f, 0.04f, s->shirt, s->skin);
    // head
    box(-0.03f, 0.78f, -0.03f, 0.03f, 0.82f, 0.03f, s->skin);
    box(-0.11f, 0.82f, -0.10f, 0.11f, 1.02f, 0.10f, s->skin);
    // face
    box(0.03f, 0.90f, 0.10f, 0.06f, 0.94f, 0.11f, dark);
    box(-0.06f, 0.90f, 0.10f, -0.03f, 0.94f, 0.11f, dark);
    box(-0.03f, 0.85f, 0.10f, 0.03f, 0.865f, 0.105f, RGB15(20, 8, 8));
    // hair
    box(-0.12f, 0.99f, -0.11f, 0.12f, 1.06f, 0.11f, s->hair);
    box(-0.12f, s->female ? 0.66f : 0.88f, -0.12f, 0.12f, 1.0f, -0.07f, s->hair);
    if (s->female) {
        box(-0.125f, 0.80f, -0.10f, -0.105f, 1.0f, 0.04f, s->hair);
        box(0.105f, 0.80f, -0.10f, 0.125f, 1.0f, 0.04f, s->hair);
    }
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
    glPolyFmt(POLY_ALPHA(31) | s_cull | POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_ID(55 + i));
    glBindTexture(GL_TEXTURE_2D, 0);
    glPushMatrix();
    float baseY = s->y;
    if (s->pose == POSE_SIT) baseY = s->y - 0.44f * H;
    if (s->pose == POSE_LIE) baseY = s->y + 0.08f * H;
    if (baseY < 0) baseY = 0;
    glTranslatef32(floattof32(s->x), floattof32(baseY), floattof32(s->z));
    glRotateY(s->face);
    if (s->pose == POSE_LIE) { glTranslatef32(0, 0, 0); glRotateX(-90); }
    glScalef32(floattof32(H), floattof32(H), floattof32(H));
    drawSimBody(s, s->state == SS_WALK);
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

// Create-a-Sim preview
void render_cas(const Sim* s, float spin) {
    setCamera(0, 0.42f, 0, 0.9f, 12, 0);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(0));
    texQuad(-2, -2, 2, 2, 0, FL_CARPET);
    glBindTexture(GL_TEXTURE_2D, 0);
    Sim t = *s;
    t.x = 0; t.z = 0; t.y = 0; t.face = spin; t.pose = POSE_STAND;
    float H = simHeight();
    glPolyFmt(POLY_ALPHA(31) | s_cull | POLY_FORMAT_LIGHT0 | POLY_FORMAT_LIGHT1 | POLY_ID(55));
    glPushMatrix();
    glRotateY(spin);
    // fill the frame regardless of the kit's scale
    float k = 0.85f / H;
    glScalef32(floattof32(H * k), floattof32(H * k), floattof32(H * k));
    t.anim += spin;
    drawSimBody(&t, false);
    glPopMatrix(1);
    g_lastPolys = POLY_RAM_COUNT;
    glFlush(0);
}
