// PocketLife - actions and the buy-mode catalog
#include "game.h"

const char* const g_needNames[NEED_COUNT] = { "Hunger", "Energy", "Bladder", "Hygiene", "Fun", "Social" };
const char* const g_skillNames[SKILL_COUNT] = { "Cooking", "Logic", "Charisma" };
const char* const g_catNames[CAT_COUNT] = { "Kitchen", "Bath", "Bedroom", "Living", "Fun", "Decor" };

//                      name             Hun  Ene  Bla  Hyg  Fun  Soc   min  end          use           pose         skill            $/h
const ActionDef g_actions[ACT_COUNT] = {
    [ACT_NONE]      = { "",              {0},                            0,  -1,          USE_NONE,     POSE_STAND,  -1,               0 },
    [ACT_SNACK]     = { "Grab a Snack",  { 60,   0,  -6,   0,   4,   0 }, 30, NEED_HUNGER, USE_FACE,     POSE_USE,    -1,               0 },
    [ACT_COOK]      = { "Cook Dinner",   { 85,   0,  -4,  -4,   8,   0 }, 60, NEED_HUNGER, USE_FACE,     POSE_USE,    SKILL_COOKING,  -10 },
    [ACT_SLEEP]     = { "Sleep",         {  2,  17,   4,   0,   0,   0 },540, NEED_ENERGY, USE_LIE,      POSE_LIE,    -1,               0 },
    [ACT_NAP]       = { "Nap",           {  0,  14,   2,   0,   0,   0 }, 90, NEED_ENERGY, USE_LIE,      POSE_LIE,    -1,               0 },
    [ACT_TOILET]    = { "Use Toilet",    {  0,   0, 260,  -6,   0,   0 }, 20, NEED_BLADDER,USE_SIT,      POSE_SIT,    -1,               0 },
    [ACT_SHOWER]    = { "Take Shower",   {  0,   2,   0, 200,   6,   0 }, 30, NEED_HYGIENE,USE_STAND_ON, POSE_USE,    -1,               0 },
    [ACT_BATH]      = { "Take a Bath",   {  0,   8,   0, 130,  16,   0 }, 50, NEED_HYGIENE,USE_LIE,      POSE_LIE,    -1,               0 },
    [ACT_WASH]      = { "Wash Up",       {  0,   0,   0,  80,   0,   0 }, 15, NEED_HYGIENE,USE_FACE,     POSE_USE,    -1,               0 },
    [ACT_SPEECH]    = { "Practice Speech",{ 0,  -4,   0,   0,   6,   4 }, 60, -1,          USE_FACE,     POSE_TALK,   SKILL_CHARISMA,   0 },
    [ACT_TV]        = { "Watch TV",      {  0,   2,   0,   0,  32,   0 }, 60, NEED_FUN,    USE_FACE,     POSE_STAND,  -1,               0 },
    [ACT_RELAX]     = { "Relax",         {  0,  10,   0,   0,  10,   0 }, 60, NEED_ENERGY, USE_SIT,      POSE_SIT,    -1,               0 },
    [ACT_WORK]      = { "Work From Home",{ -2,  -8,   0,  -2, -12,   0 },180, -1,          USE_FACE,     POSE_USE,    SKILL_LOGIC,      1 },
    [ACT_GAMES]     = { "Play Games",    {  0,  -2,   0,   0,  40,   0 }, 60, NEED_FUN,    USE_FACE,     POSE_USE,    SKILL_LOGIC,      0 },
    [ACT_CHATNET]   = { "Chat Online",   {  0,   0,   0,   0,  10,  30 }, 45, NEED_SOCIAL, USE_FACE,     POSE_USE,    -1,               0 },
    [ACT_READ]      = { "Read a Book",   {  0,   0,   0,   0,  20,   0 }, 60, NEED_FUN,    USE_FACE,     POSE_USE,    SKILL_COOKING,    0 },
    [ACT_DANCE]     = { "Dance",         {  0, -10,   0,  -6,  48,   4 }, 45, NEED_FUN,    USE_FACE,     POSE_DANCE,  -1,               0 },
    [ACT_WATER]     = { "Water Plant",   {  0,   0,   0,   0,  12,   0 }, 10, -1,          USE_FACE,     POSE_USE,    -1,               0 },
    [ACT_LAUNDRY]   = { "Do Laundry",    {  0,  -4,   0,  20,  -6,   0 }, 40, -1,          USE_FACE,     POSE_USE,    -1,               0 },
    [ACT_EAT_TABLE] = { "Sit & Eat",     { 50,   4,  -4,   0,  10,   0 }, 40, NEED_HUNGER, USE_SIT,      POSE_SIT,    -1,              -6 },
    [ACT_GOTO]      = { "Go Here",       {0},                            0,  -1,          USE_NONE,     POSE_STAND,  -1,               0 },
    [ACT_CHAT]      = { "Chat",          {  0,   0,   0,   0,  10,  55 }, 30, NEED_SOCIAL, USE_SOCIAL,   POSE_TALK,   SKILL_CHARISMA,   0 },
    [ACT_JOKE]      = { "Tell a Joke",   {  0,   0,   0,   0,  35,  35 }, 20, -1,          USE_SOCIAL,   POSE_WAVE,   SKILL_CHARISMA,   0 },
    [ACT_HUG]       = { "Hug",           {  0,   0,   0,   0,  15, 120 }, 10, -1,          USE_SOCIAL,   POSE_TALK,   -1,               0 },
};

const ObjDef g_objDefs[] = {
    // walls are handled by build mode, these entries just give them names/prices
    { "Wall",              MDL_WALL,        10, CAT_DECOR,   OF_NOBUY,    { 0 } },
    { "Window",            MDL_WALL_WINDOW, 40, CAT_DECOR,   OF_NOBUY,    { 0 } },
    { "Door",              MDL_WALL_DOOR,   60, CAT_DECOR,   OF_NOBUY,    { 0 } },
    // kitchen
    { "Fridge",            MDL_FRIDGE,     450, CAT_KITCHEN, 0,           { ACT_SNACK } },
    { "Stove",             MDL_STOVE,      380, CAT_KITCHEN, 0,           { ACT_COOK } },
    { "Counter",           MDL_COUNTER,    140, CAT_KITCHEN, 0,           { 0 } },
    { "Kitchen Sink",      MDL_KSINK,      220, CAT_KITCHEN, 0,           { ACT_WASH } },
    { "Dining Table",      MDL_TABLE,      180, CAT_KITCHEN, 0,           { 0 } },
    { "Dining Chair",      MDL_CHAIR,       80, CAT_KITCHEN, 0,           { ACT_EAT_TABLE, ACT_RELAX } },
    // bath
    { "Toilet",            MDL_TOILET,     300, CAT_BATH,    0,           { ACT_TOILET } },
    { "Shower",            MDL_SHOWER,     620, CAT_BATH,    0,           { ACT_SHOWER } },
    { "Bathtub",           MDL_BATHTUB,    800, CAT_BATH,    0,           { ACT_BATH } },
    { "Bathroom Sink",     MDL_BSINK,      200, CAT_BATH,    0,           { ACT_WASH, ACT_SPEECH } },
    { "Washer",            MDL_WASHER,     350, CAT_BATH,    0,           { ACT_LAUNDRY } },
    // bedroom
    { "Single Bed",        MDL_BED_SINGLE, 350, CAT_BED,     0,           { ACT_SLEEP, ACT_NAP } },
    { "Double Bed",        MDL_BED_DOUBLE, 700, CAT_BED,     0,           { ACT_SLEEP, ACT_NAP } },
    { "Dresser",           MDL_DRESSER,    260, CAT_BED,     0,           { 0 } },
    { "Side Table",        MDL_SIDETABLE,   90, CAT_BED,     0,           { 0 } },
    // living
    { "Sofa",              MDL_SOFA,       520, CAT_LIVING,  0,           { ACT_RELAX, ACT_NAP } },
    { "Armchair",          MDL_ARMCHAIR,   260, CAT_LIVING,  0,           { ACT_RELAX } },
    { "Coffee Table",      MDL_COFFEETABLE,150, CAT_LIVING,  0,           { 0 } },
    { "Television",        MDL_TV,         600, CAT_LIVING,  0,           { ACT_TV } },
    { "Computer Desk",     MDL_COMPUTER,   900, CAT_FUN,     0,           { ACT_WORK, ACT_GAMES, ACT_CHATNET } },
    { "Bookcase",          MDL_BOOKCASE,   300, CAT_FUN,     0,           { ACT_READ } },
    { "Stereo",            MDL_RADIO,      250, CAT_FUN,     0,           { ACT_DANCE } },
    // decor
    { "Potted Plant",      MDL_PLANT,       60, CAT_DECOR,   0,           { ACT_WATER } },
    { "Floor Lamp",        MDL_LAMP,       120, CAT_DECOR,   0,           { 0 } },
    { "Rug",               MDL_RUG,        110, CAT_DECOR,   OF_WALKABLE, { 0 } },
    { "Trash Can",         MDL_TRASH,       30, CAT_DECOR,   0,           { 0 } },
    { "Coat Rack",         MDL_COATRACK,    70, CAT_DECOR,   0,           { 0 } },
};
const int g_objDefCount = sizeof(g_objDefs) / sizeof(g_objDefs[0]);
