/*
    3D Endless Car Racing Game - OpenGL + GLUT + C
    ------------------------------------------------
    University project version.

    Project idea:
      This is an endless arcade racing game. The player drives a sports car
      on a 3D road, avoids enemy traffic, earns score and distance, and sees
      scenery change automatically over time.

    Code organization:
      1. Constants and structures define the game data.
      2. Utility functions handle math, colors, text, and simple shapes.
      3. Lighting and sky functions create the day-night cycle.
      4. Drawing functions render cars, road, buildings, trees, and scenery.
      5. Gameplay functions update movement, traffic, score, and collision.
      6. GLUT callback functions connect the game to keyboard and display.

    Important note:
      The game uses simple OpenGL primitive shapes such as cubes, spheres,
      cones, torus wheels, and transparent quads. This keeps the project pure C,
      beginner-friendly, and easy to explain in a university viva.

    Build in Code::Blocks:
      Link libraries: freeglut, opengl32, glu32, winmm
      If your GLUT package is old, use glut32 instead of freeglut.

    Controls:
      Left / Right Arrow : Move car
      Up / Down Arrow    : Increase / decrease speed
      P                  : Pause / resume
      I                  : Instructions
      R                  : Restart after game over
      ESC                : Exit / back
*/

#include <GL/glut.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

/* Window size used by GLUT when the game starts. */
#define WIN_W 1000
#define WIN_H 700

/* Gameplay and object limits. Fixed-size arrays are simple and safe for C. */
#define NUM_LANES 3
#define MAX_ENEMIES 9
#define MAX_TREES 64
#define MAX_LIGHTS 34
#define MAX_BUILDINGS 28
#define MAX_BARRIERS 80

/* Road and spawn values. Z is the forward/backward direction in this game. */
#define ROAD_HALF_WIDTH 6.1f
#define ROAD_LENGTH 210.0f
#define PLAYER_Z 8.0f
#define SPAWN_Z -165.0f
#define PI 3.14159265f

/* Screen/state values for menu, play, pause, and game over. */
#define STATE_MENU 0
#define STATE_INSTRUCTIONS 1
#define STATE_PLAYING 2
#define STATE_PAUSED 3
#define STATE_GAME_OVER 4

/* Environment IDs. The game cycles through these scenes during play. */
#define ENV_CITY 0
#define ENV_RIVER 1
#define ENV_HILLS 2
#define ENV_VILLAGE 3
#define ENV_DESERT 4
#define ENV_FLOWER 5
#define ENV_COUNT 6

/* Each environment lasts 6.5 seconds; the final part blends into the next. */
#define ENV_SEGMENT_SECONDS 6.5f
#define ENV_BLEND_SECONDS 0.35f

/* Vehicle stores both visual data and gameplay data for player/enemy cars. */
typedef struct {
    float x, z;
    float width, depth, height;
    float r, g, b;
    int lane;
    int type;
    int active;
} Vehicle;

/* Reused by scenery objects such as trees, lamps, buildings, and barriers. */
typedef struct {
    float x, z, scale;
    int type;
} RoadObject;

/* Simple rectangular collision box on the road plane. */
typedef struct {
    float x, z, w, d;
} HitBox;

/* Current game state and keyboard state. */
static int gameState = STATE_MENU;
static int keyLeft = 0, keyRight = 0, keyUp = 0, keyDown = 0;
static int lastTime = 0;
static int highScore = 0;

/* Main game objects. */
static Vehicle player;
static Vehicle enemies[MAX_ENEMIES];
static RoadObject trees[MAX_TREES];
static RoadObject lights[MAX_LIGHTS];
static RoadObject buildings[MAX_BUILDINGS];
static RoadObject barriers[MAX_BARRIERS];

/* Core gameplay variables updated every frame. */
static float roadOffset = 0.0f;
static float playerTargetX = 0.0f;
static float gameSpeed = 26.0f;
static float baseSpeed = 26.0f;
static float score = 0.0f;
static float distanceCovered = 0.0f;
static float difficultyTimer = 0.0f;
static float trafficTimer = 0.0f;
static float dayNightTime = 0.15f;
static float cameraX = 0.0f;
static int enemyLimit = 4;

/* Convert lane number 0, 1, 2 into road X position. */
static float laneX(int lane) {
    return (lane - 1) * 3.55f;
}

/* Return a random float between a and b. */
static float frandRange(float a, float b) {
    return a + (float)rand() / (float)RAND_MAX * (b - a);
}

/* Keep a number inside a minimum and maximum range. */
static float clampf(float v, float a, float b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

/* Linear interpolation, useful for smooth color/value transitions. */
static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

/* Sine wave converted to the 0..1 range for smooth repeated animation. */
static float wave01(float x) {
    return (sinf(x * PI * 2.0f) + 1.0f) * 0.5f;
}

/* Smooth interpolation curve so environment changes do not feel harsh. */
static float smoothStep(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* Brightness of the world based on the day-night timer. */
static float daylightAmount(void) {
    return clampf(sinf(dayNightTime * PI * 2.0f) * 0.72f + 0.38f, 0.10f, 1.0f);
}

/* Opposite of daylight. Used for lamps, headlights, and glowing windows. */
static float nightAmount(void) {
    return 1.0f - daylightAmount();
}

/* Extra warm light near sunrise and sunset. */
static float sunsetAmount(void) {
    float evening = 1.0f - fabsf(dayNightTime - 0.72f) / 0.12f;
    float sunrise = 1.0f - fabsf(dayNightTime - 0.05f) / 0.10f;
    return clampf(evening > sunrise ? evening : sunrise, 0.0f, 1.0f);
}

/* Human-readable time name for the HUD. */
static const char *timeOfDayName(void) {
    if (dayNightTime < 0.16f) return "Sunrise";
    if (dayNightTime < 0.42f) return "Morning";
    if (dayNightTime < 0.62f) return "Afternoon";
    if (dayNightTime < 0.78f) return "Sunset";
    if (dayNightTime < 0.92f) return "Night";
    return "Late Night";
}

/* Select the active scenery zone from elapsed gameplay time. */
static int currentEnvironment(void) {
    int zone = (int)(difficultyTimer / ENV_SEGMENT_SECONDS) % ENV_COUNT;
    if (zone < 0) zone = 0;
    return zone;
}

/* The next zone is used for transition blending. */
static int nextEnvironment(void) {
    return (currentEnvironment() + 1) % ENV_COUNT;
}

/* Returns 0 normally and approaches 1 near the end of an environment segment. */
static float environmentBlend(void) {
    float pos = fmodf(difficultyTimer, ENV_SEGMENT_SECONDS);
    return smoothStep((pos - (ENV_SEGMENT_SECONDS - ENV_BLEND_SECONDS)) / ENV_BLEND_SECONDS);
}

/* Weight of each environment. Current scene fades out while next scene fades in. */
static float environmentWeight(int zone) {
    int current = currentEnvironment();
    int next = nextEnvironment();
    float blend = environmentBlend();
    if (zone == current) return 1.0f - blend;
    if (zone == next) return blend;
    return 0.0f;
}

/* Text shown in the HUD for the current/next environment. */
static const char *environmentName(void) {
    float blend = environmentBlend();
    int zone = blend > 0.5f ? nextEnvironment() : currentEnvironment();
    if (zone == ENV_CITY) return "City";
    if (zone == ENV_DESERT) return "Desert";
    if (zone == ENV_RIVER) return "River Area";
    if (zone == ENV_HILLS) return "Hilly Area";
    if (zone == ENV_VILLAGE) return "Village";
    if (zone == ENV_FLOWER) return "Flower Garden";
    return "City";
}

/* Small wrapper so color calls are easy to read. */
static void setColor3f(float r, float g, float b) {
    glColor3f(r, g, b);
}

/* Draw bitmap text in 2D overlay coordinates. */
static void drawText2D(float x, float y, const char *text, void *font) {
    glRasterPos2f(x, y);
    while (*text) {
        glutBitmapCharacter(font, *text++);
    }
}

/* Switch from 3D camera projection to 2D UI drawing. */
static void begin2D(void) {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, WIN_W, 0, WIN_H);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
}

/* Restore 3D drawing state after drawing HUD/menu panels. */
static void end2D(void) {
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

/* Center text horizontally by measuring its bitmap width. */
static void drawCenteredText(float y, const char *text, void *font) {
    int width = 0;
    const char *p = text;
    while (*p) width += glutBitmapWidth(font, *p++);
    drawText2D((WIN_W - width) * 0.5f, y, text, font);
}

/* Draw a colored 3D cube at position x/y/z with scale sx/sy/sz. */
static void cube(float x, float y, float z,
                 float sx, float sy, float sz,
                 float r, float g, float b) {
    glPushMatrix();
    setColor3f(r, g, b);
    glTranslatef(x, y, z);
    glScalef(sx, sy, sz);
    glutSolidCube(1.0);
    glPopMatrix();
}

/* Draw a transparent cube, mainly used for glow and soft visual effects. */
static void cubeAlpha(float x, float y, float z,
                      float sx, float sy, float sz,
                      float r, float g, float b, float a) {
    glPushMatrix();
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);
    glTranslatef(x, y, z);
    glScalef(sx, sy, sz);
    glutSolidCube(1.0);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

/* Draw a scaled sphere to create curved car parts and scenery shapes. */
static void sphereShape(float x, float y, float z,
                        float sx, float sy, float sz,
                        float r, float g, float b) {
    glPushMatrix();
    setColor3f(r, g, b);
    glTranslatef(x, y, z);
    glScalef(sx, sy, sz);
    glutSolidSphere(1.0, 18, 10);
    glPopMatrix();
}

/*
   Update OpenGL lights from current time of day.
   LIGHT0 works like sun/moon light.
   LIGHT1 works like the player's headlight during dark scenes.
*/
static void applyWorldLighting(void) {
    float day = daylightAmount();
    float night = nightAmount();
    float sunset = sunsetAmount();
    GLfloat lightPos[] = { -12.0f + 24.0f * wave01(dayNightTime + 0.1f),
                           14.0f * day + 3.0f,
                           -18.0f,
                           1.0f };
    GLfloat ambient[] = { 0.18f + 0.24f * day + 0.10f * sunset,
                          0.18f + 0.24f * day + 0.05f * sunset,
                          0.22f + 0.24f * day,
                          1.0f };
    GLfloat diffuse[] = { 0.28f + 0.70f * day + 0.35f * sunset,
                          0.30f + 0.65f * day + 0.14f * sunset,
                          0.42f + 0.50f * day - 0.18f * sunset,
                          1.0f };
    GLfloat headPos[] = { player.x, 1.0f, player.z - 1.2f, 1.0f };
    GLfloat headDiffuse[] = { 0.95f * night,
                              0.86f * night,
                              0.52f * night,
                              1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);

    glEnable(GL_LIGHT1);
    glLightfv(GL_LIGHT1, GL_POSITION, headPos);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, headDiffuse);
    glLightf(GL_LIGHT1, GL_CONSTANT_ATTENUATION, 1.0f);
    glLightf(GL_LIGHT1, GL_LINEAR_ATTENUATION, 0.035f);
    glDisable(GL_FOG);
}

/* Set background sky color from day, night, sunrise, and sunset values. */
static void setSkyColor(void) {
    float day = daylightAmount();
    float sunset = sunsetAmount();
    float r = lerpf(0.07f, 0.48f, day) + 0.30f * sunset;
    float g = lerpf(0.10f, 0.72f, day) + 0.10f * sunset;
    float b = lerpf(0.18f, 0.96f, day) - 0.22f * sunset;
    glClearColor(clampf(r, 0.02f, 0.95f),
                 clampf(g, 0.03f, 0.90f),
                 clampf(b, 0.08f, 1.0f),
                 1.0f);
}

/* Draw one rotating wheel/rim using torus rings and small spoke cubes. */
static void drawWheel(float x, float y, float z, float side) {
    int i;
    float spin = fmodf(roadOffset * 18.0f + z * 95.0f, 360.0f);
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
    setColor3f(0.03f, 0.03f, 0.035f);
    glutSolidTorus(0.075, 0.235, 16, 24);
    setColor3f(0.82f, 0.86f, 0.88f);
    glutSolidTorus(0.025, 0.122, 12, 18);
    glRotatef(spin, 0.0f, 0.0f, 1.0f);
    for (i = 0; i < 6; i++) {
        glPushMatrix();
        glRotatef(i * 60.0f, 0.0f, 0.0f, 1.0f);
        cube(0.0f, 0.0f, 0.0f, 0.035f, 0.20f, 0.035f, 0.78f, 0.80f, 0.78f);
        glPopMatrix();
    }
    sphereShape(0.0f, 0.0f, 0.0f, 0.08f, 0.08f, 0.035f, 0.12f, 0.13f, 0.14f);
    (void)side;
    glPopMatrix();
}

/*
   Render a complete car model.
   isPlayer = 1 draws the hero/player car with extra body parts, stripes,
   LED bars, spoiler, and more premium details.
   isPlayer = 0 draws enemy sports cars using several style variations.
*/
static void drawCarModel(const Vehicle *v, int isPlayer) {
    float x = v->x, z = v->z;
    float steerTilt = isPlayer ? (playerTargetX - player.x) * -5.0f : 0.0f;
    float night = nightAmount();
    float stripeR = (v->r + 0.75f > 1.0f) ? 0.08f : 0.95f;
    float stripeG = (v->g + 0.75f > 1.0f) ? 0.08f : 0.95f;
    float stripeB = (v->b + 0.75f > 1.0f) ? 0.08f : 0.95f;
    int style = v->type % 5;

    glPushMatrix();
    glTranslatef(x, 0.0f, z);
    glRotatef(steerTilt, 0.0f, 0.0f, 1.0f);

    /* Shadow */
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    setColor3f(0.0f, 0.0f, 0.0f);
    glColor4f(0.0f, 0.0f, 0.0f, 0.25f);
    glBegin(GL_QUADS);
    glVertex3f(-v->width * 0.72f, 0.015f, -v->depth * 0.58f);
    glVertex3f( v->width * 0.72f, 0.015f, -v->depth * 0.58f);
    glVertex3f( v->width * 0.72f, 0.015f,  v->depth * 0.58f);
    glVertex3f(-v->width * 0.72f, 0.015f,  v->depth * 0.58f);
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);

    if (isPlayer) {
        /* Boxy modern racing car: angular body, squared cabin, wing, decals, and LED bars. */
        cube(0.0f, 0.24f, 0.02f, v->width * 1.12f, 0.36f, v->depth * 0.86f, v->r, v->g, v->b);
        cube(0.0f, 0.50f, -v->depth * 0.12f, v->width * 0.86f, 0.34f, v->depth * 0.52f,
             v->r * 0.82f, v->g * 0.82f, v->b * 0.82f);
        cube(0.0f, 0.76f, -v->depth * 0.06f, v->width * 0.64f, 0.28f, v->depth * 0.34f,
             0.05f, 0.12f, 0.18f);

        /* Squared nose, rear block, side skirts and diffuser. */
        cube(0.0f, 0.22f, -v->depth * 0.48f, v->width * 0.92f, 0.20f, v->depth * 0.22f,
             v->r * 1.05f, v->g * 1.05f, v->b * 1.05f);
        cube(0.0f, 0.24f, v->depth * 0.44f, v->width * 1.02f, 0.28f, v->depth * 0.22f,
             v->r * 0.68f, v->g * 0.68f, v->b * 0.68f);
        cube(-v->width * 0.57f, 0.23f, 0.0f, 0.08f, 0.18f, v->depth * 0.82f, 0.025f, 0.025f, 0.030f);
        cube( v->width * 0.57f, 0.23f, 0.0f, 0.08f, 0.18f, v->depth * 0.82f, 0.025f, 0.025f, 0.030f);
        cube(0.0f, 0.16f, v->depth * 0.58f, v->width * 0.86f, 0.12f, 0.12f, 0.018f, 0.018f, 0.022f);

        /* Racing stripes and angular glass panels. */
        cube(0.0f, 0.56f, -v->depth * 0.12f, 0.18f, 0.045f, v->depth * 0.78f, 1.0f, 0.92f, 0.08f);
        cube(-v->width * 0.24f, 0.565f, -v->depth * 0.12f, 0.045f, 0.045f, v->depth * 0.70f, 0.02f, 0.95f, 1.0f);
        cube( v->width * 0.24f, 0.565f, -v->depth * 0.12f, 0.045f, 0.045f, v->depth * 0.70f, 0.02f, 0.95f, 1.0f);
        cube(0.0f, 0.94f, -v->depth * 0.18f, v->width * 0.48f, 0.06f, 0.30f, 0.08f, 0.26f, 0.40f);
        cube(0.0f, 0.88f, v->depth * 0.10f, v->width * 0.46f, 0.05f, 0.24f, 0.06f, 0.20f, 0.34f);

        /* Rear wing with two bars. */
        cube(0.0f, 0.86f, v->depth * 0.56f, v->width * 1.10f, 0.07f, 0.16f, 0.018f, 0.018f, 0.022f);
        cube(-v->width * 0.42f, 0.62f, v->depth * 0.50f, 0.06f, 0.42f, 0.07f, 0.018f, 0.018f, 0.022f);
        cube( v->width * 0.42f, 0.62f, v->depth * 0.50f, 0.06f, 0.42f, 0.07f, 0.018f, 0.018f, 0.022f);

        glDisable(GL_LIGHTING);
        cube(0.0f, 0.42f, -v->depth * 0.49f, v->width * 0.90f, 0.045f, 0.040f, 0.35f, 1.0f, 1.0f);
        cube(0.0f, 0.36f,  v->depth * 0.50f, v->width * 0.78f, 0.045f, 0.040f, 1.0f, 0.04f, 0.03f);
        glEnable(GL_LIGHTING);

        drawWheel(-v->width * 0.50f, 0.23f, -v->depth * 0.34f, -1.0f);
        drawWheel( v->width * 0.50f, 0.23f, -v->depth * 0.34f,  1.0f);
        drawWheel(-v->width * 0.50f, 0.23f,  v->depth * 0.34f, -1.0f);
        drawWheel( v->width * 0.50f, 0.23f,  v->depth * 0.34f,  1.0f);

        glPopMatrix();
        return;
    }

    /* Low smooth sports body. Spheres are scaled to create curved hoods and fenders. */
    cube(0.0f, 0.32f, 0.05f, v->width * 1.04f, 0.36f, v->depth * 0.80f, v->r, v->g, v->b);
    sphereShape(0.0f, 0.42f, -v->depth * 0.18f, v->width * 0.54f, 0.25f, v->depth * 0.43f,
                v->r * 1.05f, v->g * 1.05f, v->b * 1.05f);
    sphereShape(-v->width * 0.37f, 0.33f, -v->depth * 0.23f, 0.25f, 0.17f, 0.52f, v->r, v->g, v->b);
    sphereShape( v->width * 0.37f, 0.33f, -v->depth * 0.23f, 0.25f, 0.17f, 0.52f, v->r, v->g, v->b);

    if (style == 0) {
        /* Supercar: low cockpit and dark side intakes. */
        cube(0.0f, 0.70f, -v->depth * 0.06f, v->width * 0.55f, 0.36f, v->depth * 0.33f,
             v->r * 0.82f, v->g * 0.82f, v->b * 0.82f);
        cube(-v->width * 0.54f, 0.36f, 0.02f, 0.05f, 0.18f, v->depth * 0.34f, 0.04f, 0.04f, 0.05f);
        cube( v->width * 0.54f, 0.36f, 0.02f, 0.05f, 0.18f, v->depth * 0.34f, 0.04f, 0.04f, 0.05f);
    } else if (style == 1) {
        /* Muscle car: longer hood, squared cabin, bright stripe. */
        cube(0.0f, 0.66f, 0.18f, v->width * 0.70f, 0.40f, v->depth * 0.34f,
             v->r * 0.78f, v->g * 0.78f, v->b * 0.78f);
        cube(0.0f, 0.54f, -v->depth * 0.15f, 0.16f, 0.05f, v->depth * 0.72f, stripeR, stripeG, stripeB);
    } else if (style == 2) {
        /* Hypercar: glass canopy and sharp center spine. */
        sphereShape(0.0f, 0.73f, -v->depth * 0.02f, v->width * 0.34f, 0.26f, v->depth * 0.28f,
                    0.05f, 0.12f, 0.18f);
        cube(0.0f, 0.60f, -v->depth * 0.24f, 0.10f, 0.08f, v->depth * 0.45f, stripeR, stripeG, stripeB);
    } else if (style == 3) {
        /* Racing car: stripe, wing and lower nose. */
        cube(0.0f, 0.66f, 0.00f, v->width * 0.58f, 0.34f, v->depth * 0.30f,
             v->r * 0.86f, v->g * 0.86f, v->b * 0.86f);
        cube(0.0f, 0.55f, -v->depth * 0.10f, 0.18f, 0.06f, v->depth * 0.86f, stripeR, stripeG, stripeB);
        cube(0.0f, 0.78f, v->depth * 0.52f, v->width * 0.90f, 0.08f, 0.16f, 0.04f, 0.04f, 0.045f);
        cube(-v->width * 0.35f, 0.62f, v->depth * 0.48f, 0.06f, 0.30f, 0.08f, 0.04f, 0.04f, 0.045f);
        cube( v->width * 0.35f, 0.62f, v->depth * 0.48f, 0.06f, 0.30f, 0.08f, 0.04f, 0.04f, 0.045f);
    } else {
        /* Roadster: small open cockpit and rear deck. */
        cube(0.0f, 0.58f, 0.08f, v->width * 0.45f, 0.20f, v->depth * 0.22f, 0.04f, 0.04f, 0.05f);
        cube(0.0f, 0.50f, v->depth * 0.31f, v->width * 0.62f, 0.12f, v->depth * 0.18f,
             v->r * 0.72f, v->g * 0.72f, v->b * 0.72f);
    }

    if (isPlayer) {
        /* Hero supercar layer: sharper aero, premium wing, glossy highlights, and racing decals. */
        cube(0.0f, 0.24f, -v->depth * 0.50f, v->width * 0.72f, 0.08f, 0.18f, 0.02f, 0.025f, 0.03f);
        cube(0.0f, 0.50f, -v->depth * 0.24f, 0.16f, 0.04f, v->depth * 0.82f, 0.00f, 0.95f, 1.0f);
        cube(-v->width * 0.23f, 0.515f, -v->depth * 0.23f, 0.045f, 0.045f, v->depth * 0.72f, 1.0f, 0.95f, 0.12f);
        cube( v->width * 0.23f, 0.515f, -v->depth * 0.23f, 0.045f, 0.045f, v->depth * 0.72f, 1.0f, 0.95f, 0.12f);
        cube(0.0f, 0.88f, v->depth * 0.54f, v->width * 1.08f, 0.07f, 0.18f, 0.02f, 0.02f, 0.025f);
        cube(-v->width * 0.42f, 0.66f, v->depth * 0.49f, 0.06f, 0.42f, 0.08f, 0.02f, 0.02f, 0.025f);
        cube( v->width * 0.42f, 0.66f, v->depth * 0.49f, 0.06f, 0.42f, 0.08f, 0.02f, 0.02f, 0.025f);
        cube(-v->width * 0.56f, 0.43f, -v->depth * 0.12f, 0.07f, 0.12f, 0.54f, 0.02f, 0.95f, 1.0f);
        cube( v->width * 0.56f, 0.43f, -v->depth * 0.12f, 0.07f, 0.12f, 0.54f, 0.02f, 0.95f, 1.0f);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_QUADS);
        glColor4f(1.0f, 1.0f, 1.0f, 0.20f);
        glVertex3f(-v->width * 0.42f, 0.61f, -v->depth * 0.42f);
        glVertex3f( v->width * 0.04f, 0.63f, -v->depth * 0.48f);
        glColor4f(1.0f, 1.0f, 1.0f, 0.03f);
        glVertex3f( v->width * 0.30f, 0.50f,  v->depth * 0.32f);
        glVertex3f(-v->width * 0.24f, 0.48f,  v->depth * 0.28f);
        glEnd();
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }

    /* Glass, headlights and taillights. */
    cube(0.0f, 0.83f, -v->depth * 0.18f, v->width * 0.42f, 0.07f, 0.20f, 0.08f, 0.28f, 0.42f);
    cube(0.0f, 0.77f,  v->depth * 0.10f, v->width * 0.42f, 0.06f, 0.18f, 0.05f, 0.20f, 0.34f);

    glDisable(GL_LIGHTING);
    cube(-v->width * 0.24f, 0.40f, -v->depth * 0.43f, 0.24f, 0.075f, 0.045f,
         1.0f, 0.95f, 0.58f);
    cube( v->width * 0.24f, 0.40f, -v->depth * 0.43f, 0.24f, 0.075f, 0.045f,
         1.0f, 0.95f, 0.58f);
    cube(-v->width * 0.27f, 0.36f,  v->depth * 0.43f, 0.22f, 0.075f, 0.045f,
         1.0f, 0.05f, 0.035f);
    cube( v->width * 0.27f, 0.36f,  v->depth * 0.43f, 0.22f, 0.075f, 0.045f,
         1.0f, 0.05f, 0.035f);
    if (isPlayer) {
        cube(0.0f, 0.43f, -v->depth * 0.45f, v->width * 0.86f, 0.035f, 0.035f,
             0.35f, 1.0f, 1.0f);
        cube(0.0f, 0.39f,  v->depth * 0.45f, v->width * 0.76f, 0.035f, 0.035f,
             1.0f, 0.05f, 0.04f);
        cube(0.0f, 0.18f, -v->depth * 0.56f, v->width * 0.94f, 0.045f, 0.08f,
             0.02f, 0.02f, 0.025f);
        cube(0.0f, 0.22f,  v->depth * 0.55f, v->width * 0.82f, 0.06f, 0.12f,
             0.02f, 0.02f, 0.025f);
    }
    glEnable(GL_LIGHTING);

    drawWheel(-v->width * 0.48f, 0.23f, -v->depth * 0.34f, -1.0f);
    drawWheel( v->width * 0.48f, 0.23f, -v->depth * 0.34f,  1.0f);
    drawWheel(-v->width * 0.48f, 0.23f,  v->depth * 0.34f, -1.0f);
    drawWheel( v->width * 0.48f, 0.23f,  v->depth * 0.34f,  1.0f);

    glPopMatrix();
}

/*
   Draw the road, ground, lane divider marks, road texture bands, and side lines.
   roadOffset makes lane dividers move toward the player, which creates speed.
*/
static void drawRoad(void) {
    float zStart = -ROAD_LENGTH + fmodf(roadOffset, 8.0f);
    float day = daylightAmount();
    float night = nightAmount();
    float desert = environmentWeight(ENV_DESERT);
    float river = environmentWeight(ENV_RIVER);
    float hills = environmentWeight(ENV_HILLS);
    float village = environmentWeight(ENV_VILLAGE);
    float flower = environmentWeight(ENV_FLOWER);
    int i;

    /* Ground */
    glDisable(GL_LIGHTING);
    glBegin(GL_QUADS);
    setColor3f(0.02f + 0.06f * day + 0.48f * desert + 0.08f * river + 0.05f * hills + 0.08f * flower,
               0.13f + 0.28f * day + 0.20f * desert + 0.18f * river + 0.16f * hills + 0.10f * village + 0.24f * flower,
               0.06f + 0.10f * day + 0.04f * desert + 0.16f * river + 0.05f * hills + 0.05f * flower);
    glVertex3f(-80.0f, 0.0f,  35.0f);
    glVertex3f( 80.0f, 0.0f,  35.0f);
    setColor3f(0.01f + 0.04f * day + 0.40f * desert + 0.06f * river + 0.04f * hills + 0.05f * flower,
               0.09f + 0.21f * day + 0.16f * desert + 0.14f * river + 0.13f * hills + 0.08f * village + 0.20f * flower,
               0.05f + 0.08f * day + 0.04f * desert + 0.14f * river + 0.05f * hills + 0.04f * flower);
    glVertex3f( 80.0f, 0.0f, -ROAD_LENGTH);
    glVertex3f(-80.0f, 0.0f, -ROAD_LENGTH);
    glEnd();

    /* Asphalt */
    glBegin(GL_QUADS);
    setColor3f(0.060f + 0.11f * day + 0.015f * desert,
               0.064f + 0.10f * day + 0.012f * desert,
               0.075f + 0.11f * day);
    glVertex3f(-ROAD_HALF_WIDTH, 0.02f,  30.0f);
    glVertex3f( ROAD_HALF_WIDTH, 0.02f,  30.0f);
    setColor3f(0.032f + 0.065f * day + 0.010f * desert,
               0.034f + 0.065f * day + 0.008f * desert,
               0.045f + 0.075f * day);
    glVertex3f( ROAD_HALF_WIDTH, 0.02f, -ROAD_LENGTH);
    glVertex3f(-ROAD_HALF_WIDTH, 0.02f, -ROAD_LENGTH);
    glEnd();

    /* Subtle asphalt texture bands. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (i = 0; i < 42; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.62f, 5.0f) + i * 5.0f;
        if (z > 30.0f) continue;
        glBegin(GL_QUADS);
        glColor4f(0.0f, 0.0f, 0.0f, 0.05f + 0.04f * night);
        glVertex3f(-ROAD_HALF_WIDTH + 0.35f, 0.031f, z);
        glVertex3f( ROAD_HALF_WIDTH - 0.35f, 0.031f, z);
        glVertex3f( ROAD_HALF_WIDTH - 0.35f, 0.031f, z + 0.18f);
        glVertex3f(-ROAD_HALF_WIDTH + 0.35f, 0.031f, z + 0.18f);
        glEnd();
    }
    glDisable(GL_BLEND);

    /* Soft shoulder strips make the widened road easier to read. */
    cubeAlpha(-ROAD_HALF_WIDTH - 0.35f, 0.035f, -ROAD_LENGTH * 0.42f,
              0.38f, 0.03f, ROAD_LENGTH + 35.0f,
              0.16f, 0.17f, 0.18f, 0.22f);
    cubeAlpha( ROAD_HALF_WIDTH + 0.35f, 0.035f, -ROAD_LENGTH * 0.42f,
              0.38f, 0.03f, ROAD_LENGTH + 35.0f,
              0.16f, 0.17f, 0.18f, 0.22f);

    /* Road edge lines */
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    setColor3f(0.92f, 0.92f, 0.84f);
    glVertex3f(-ROAD_HALF_WIDTH + 0.25f, 0.04f, 30.0f);
    glVertex3f(-ROAD_HALF_WIDTH + 0.25f, 0.04f, -ROAD_LENGTH);
    glVertex3f( ROAD_HALF_WIDTH - 0.25f, 0.04f, 30.0f);
    glVertex3f( ROAD_HALF_WIDTH - 0.25f, 0.04f, -ROAD_LENGTH);
    glEnd();

    /* Moving lane dividers */
    for (i = 0; i < 36; i++) {
        float z = zStart + i * 8.0f;
        float x1 = laneX(0) + 1.62f;
        float x2 = laneX(1) + 1.62f;
        if (z > 28.0f) continue;
        glBegin(GL_QUADS);
        setColor3f(0.88f + 0.12f * night, 0.88f + 0.10f * night, 0.78f + 0.10f * night);
        glVertex3f(x1 - 0.05f, 0.045f, z);
        glVertex3f(x1 + 0.05f, 0.045f, z);
        glVertex3f(x1 + 0.05f, 0.045f, z + 3.2f);
        glVertex3f(x1 - 0.05f, 0.045f, z + 3.2f);
        glVertex3f(x2 - 0.05f, 0.045f, z);
        glVertex3f(x2 + 0.05f, 0.045f, z);
        glVertex3f(x2 + 0.05f, 0.045f, z + 3.2f);
        glVertex3f(x2 - 0.05f, 0.045f, z + 3.2f);
        glEnd();
    }

    glEnable(GL_LIGHTING);
}

/* Draw a roadside tree using a trunk cube and cone leaves. */
static void drawTree(float x, float z, float s) {
    float day = daylightAmount();
    cube(x, 0.45f * s, z, 0.22f * s, 0.90f * s, 0.22f * s, 0.30f, 0.16f, 0.07f);
    glPushMatrix();
    glTranslatef(x, 1.25f * s, z);
    setColor3f(0.03f + 0.05f * day, 0.18f + 0.26f * day, 0.08f + 0.08f * day);
    glutSolidCone(0.75f * s, 1.55f * s, 12, 3);
    glTranslatef(0.0f, 0.55f * s, 0.0f);
    setColor3f(0.05f + 0.07f * day, 0.25f + 0.35f * day, 0.11f + 0.12f * day);
    glutSolidCone(0.58f * s, 1.25f * s, 12, 3);
    glPopMatrix();
}

/* Draw tall street light poles; glow appears stronger at night. */
static void drawStreetLight(float x, float z) {
    float armDir = (x < 0.0f) ? 1.0f : -1.0f;
    float lampX = x + armDir * 0.75f;
    cube(x, 1.75f, z, 0.10f, 3.50f, 0.10f, 0.32f, 0.33f, 0.33f);
    cube(x + armDir * 0.38f, 3.45f, z, 0.80f, 0.08f, 0.08f, 0.32f, 0.33f, 0.33f);
    cube(lampX, 3.32f, z, 0.34f, 0.16f, 0.26f, 0.82f, 0.90f, 1.0f);
}

/* Draw a small decorative neon sign for city scenery. */
static void drawNeonSign(float x, float y, float z, float w, int type) {
    float night = nightAmount();
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (type == 0) glColor4f(0.0f, 0.85f, 1.0f, 0.35f + 0.45f * night);
    if (type == 1) glColor4f(1.0f, 0.12f, 0.65f, 0.35f + 0.45f * night);
    if (type == 2) glColor4f(1.0f, 0.75f, 0.10f, 0.35f + 0.45f * night);
    if (type == 3) glColor4f(0.25f, 1.0f, 0.32f, 0.35f + 0.45f * night);
    glBegin(GL_QUADS);
    glVertex3f(x - w * 0.5f, y - 0.18f, z);
    glVertex3f(x + w * 0.5f, y - 0.18f, z);
    glVertex3f(x + w * 0.5f, y + 0.18f, z);
    glVertex3f(x - w * 0.5f, y + 0.18f, z);
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* Draw modern city buildings with windows that glow at night. */
static void drawBuilding(float x, float z, float w, float h, float d,
                         float r, float g, float b, int type) {
    int row, col;
    float night = nightAmount();
    int rows = (int)(h / 0.75f);
    int cols = (type == 2) ? 4 : 3;
    float frontZ = z - d * 0.51f;
    float backZ = z + d * 0.51f;
    float side = (x < 0.0f) ? -1.0f : 1.0f;

    if (type == 0) {
        /* Tall glass office tower. */
        cube(x, h * 0.5f, z, w, h, d, 0.03f, 0.10f, 0.20f);
        cube(x, h * 0.5f, frontZ - 0.03f, w * 0.86f, h * 0.96f, 0.035f, 0.02f, 0.38f, 0.58f);
        cube(x - side * w * 0.48f, h * 0.50f, z, 0.08f, h, d * 1.02f, 0.00f, 0.75f, 1.0f);
        cube(x, h + 0.12f, z, w * 0.96f, 0.24f, d * 0.90f, 0.01f, 0.04f, 0.10f);
        cube(x, h + 0.50f, z, 0.12f, 0.78f, 0.12f, 0.0f, 0.85f, 1.0f);
    } else if (type == 1) {
        /* Hotel with roof sign. */
        cube(x, h * 0.5f, z, w, h, d, 0.48f, 0.23f, 0.18f);
        cube(x - side * w * 0.30f, h * 0.52f, frontZ - 0.035f, w * 0.18f, h * 0.86f, 0.035f, 0.83f, 0.62f, 0.30f);
        cube(x + side * w * 0.30f, h * 0.52f, frontZ - 0.035f, w * 0.18f, h * 0.86f, 0.035f, 0.83f, 0.62f, 0.30f);
        cube(x, h * 0.12f, frontZ - 0.05f, w * 0.92f, h * 0.08f, 0.09f, 0.18f, 0.07f, 0.05f);
        cube(x, h + 0.22f, z, w * 0.98f, 0.34f, d * 0.50f, 0.80f, 0.16f, 0.16f);
        drawNeonSign(x, h + 0.22f, frontZ - 0.04f, w * 0.70f, 1);
    } else if (type == 2) {
        /* Shopping mall podium. */
        cube(x, h * 0.32f, z, w * 1.45f, h * 0.64f, d * 1.25f, 0.72f, 0.62f, 0.42f);
        cube(x, h * 0.82f, z, w * 0.96f, h * 0.48f, d * 0.82f, 0.06f, 0.34f, 0.48f);
        cube(x, h * 0.18f, frontZ - 0.12f, w * 1.18f, h * 0.18f, 0.10f, 0.10f, 0.18f, 0.24f);
        cube(x - side * w * 0.57f, h * 0.41f, frontZ - 0.08f, 0.10f, h * 0.50f, 0.08f, 1.0f, 0.55f, 0.08f);
        cube(x + side * w * 0.57f, h * 0.41f, frontZ - 0.08f, 0.10f, h * 0.50f, 0.08f, 0.0f, 0.82f, 1.0f);
        drawNeonSign(x, h * 0.65f, frontZ - 0.25f, w * 1.05f, 2);
    } else if (type == 3) {
        /* Warm apartment block with balconies. */
        cube(x, h * 0.5f, z, w, h, d, 0.70f, 0.38f, 0.24f);
        cube(x, h * 0.5f, frontZ - 0.045f, w * 0.30f, h * 0.92f, 0.045f, 0.95f, 0.72f, 0.36f);
        cube(x, h + 0.12f, z, w * 0.74f, 0.26f, d * 0.82f, 0.22f, 0.08f, 0.06f);
        for (row = 1; row < rows; row += 2) {
            cube(x + side * w * 0.56f, row * 0.72f, z, 0.18f, 0.07f, d * 0.75f,
                 0.96f, 0.86f, 0.62f);
        }
    } else {
        /* Hyper-modern stacked tower. */
        cube(x, h * 0.46f, z, w * 0.95f, h * 0.92f, d, 0.04f, 0.08f, 0.18f);
        cube(x + side * 0.35f, h * 0.78f, z, w * 0.70f, h * 0.44f, d * 0.84f, 0.02f, 0.30f, 0.48f);
        cube(x - side * w * 0.42f, h * 0.52f, frontZ - 0.035f, 0.08f, h * 0.82f, 0.035f, 0.0f, 0.90f, 1.0f);
        cube(x + side * w * 0.18f, h + 0.08f, z, w * 0.62f, 0.18f, d * 0.76f, 0.82f, 0.88f, 0.92f);
        drawNeonSign(x, h * 0.35f, frontZ - 0.04f, w * 0.70f, 0);
    }

    if (rows > 14) rows = 14;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (row = 0; row < rows; row++) {
        for (col = 0; col < cols; col++) {
            float wx = x - w * 0.34f + col * (w * 0.34f);
            float y1 = 0.56f + row * 0.68f;
            float y2 = y1 + 0.42f;
            float lit = (((row + col + type) % 3) == 0) ? 1.0f : 0.28f;
            float gr = 0.12f + 0.78f * night * lit;
            float gg = 0.42f + 0.38f * night * lit;
            float gb = 0.64f - 0.44f * night * lit;

            glBegin(GL_QUADS);
            glColor4f(gr, gg, gb, 0.78f);
            glVertex3f(wx - 0.24f, y1, frontZ - 0.070f);
            glVertex3f(wx + 0.24f, y1, frontZ - 0.070f);
            glVertex3f(wx + 0.24f, y2, frontZ - 0.070f);
            glVertex3f(wx - 0.24f, y2, frontZ - 0.070f);

            glColor4f(gr * 0.85f, gg * 0.90f, gb, 0.66f);
            glVertex3f(wx + 0.24f, y1, backZ + 0.070f);
            glVertex3f(wx - 0.24f, y1, backZ + 0.070f);
            glVertex3f(wx - 0.24f, y2, backZ + 0.070f);
            glVertex3f(wx + 0.24f, y2, backZ + 0.070f);
            glEnd();

            glBegin(GL_LINES);
            glColor4f(0.82f, 0.95f, 1.0f, 0.34f);
            glVertex3f(wx - 0.21f, y2 - 0.03f, frontZ - 0.074f);
            glVertex3f(wx + 0.21f, y2 - 0.03f, frontZ - 0.074f);
            glVertex3f(wx - 0.21f, y1 + 0.03f, backZ + 0.074f);
            glVertex3f(wx + 0.21f, y1 + 0.03f, backZ + 0.074f);
            glEnd();
        }
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    if (night > 0.12f) {
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (row = 0; row < rows; row++) {
            for (col = 0; col < cols; col++) {
                float lit = (((row + col + type) % 3) == 0) ? 1.0f : 0.35f;
                float wx = x - w * 0.34f + col * (w * 0.34f);
                glBegin(GL_QUADS);
                glColor4f(1.0f, 0.76f, 0.22f, 0.36f * night * lit);
                glVertex3f(wx - 0.26f, 0.55f + row * 0.68f, frontZ - 0.082f);
                glVertex3f(wx + 0.26f, 0.55f + row * 0.68f, frontZ - 0.082f);
                glVertex3f(wx + 0.26f, 1.00f + row * 0.68f, frontZ - 0.082f);
                glVertex3f(wx - 0.26f, 1.00f + row * 0.68f, frontZ - 0.082f);
                glVertex3f(wx + 0.26f, 0.55f + row * 0.68f, backZ + 0.082f);
                glVertex3f(wx - 0.26f, 0.55f + row * 0.68f, backZ + 0.082f);
                glVertex3f(wx - 0.26f, 1.00f + row * 0.68f, backZ + 0.082f);
                glVertex3f(wx + 0.26f, 1.00f + row * 0.68f, backZ + 0.082f);
                glEnd();
            }
        }
        glBegin(GL_QUADS);
        glColor4f(0.0f, 0.70f, 1.0f, 0.20f + 0.30f * night);
        glVertex3f(x - w * 0.51f, 0.35f, frontZ - 0.04f);
        glVertex3f(x - w * 0.47f, 0.35f, frontZ - 0.04f);
        glVertex3f(x - w * 0.47f, h * 0.96f, frontZ - 0.04f);
        glVertex3f(x - w * 0.51f, h * 0.96f, frontZ - 0.04f);
        glVertex3f(x + w * 0.47f, 0.35f, frontZ - 0.04f);
        glVertex3f(x + w * 0.51f, 0.35f, frontZ - 0.04f);
        glVertex3f(x + w * 0.51f, h * 0.96f, frontZ - 0.04f);
        glVertex3f(x + w * 0.47f, h * 0.96f, frontZ - 0.04f);
        glEnd();
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }
}

/* Draw road barrier blocks along the road side. */
static void drawBarrier(float x, float z) {
    cube(x, 0.22f, z, 0.35f, 0.44f, 1.25f, 0.95f, 0.95f, 0.90f);
    cube(x, 0.34f, z + 0.20f, 0.37f, 0.18f, 0.38f, 0.9f, 0.08f, 0.06f);
}

/* Draw simple city traffic lights as decorative environment objects. */
static void drawTrafficLight(float x, float z) {
    float night = nightAmount();
    cube(x, 1.05f, z, 0.08f, 2.10f, 0.08f, 0.18f, 0.18f, 0.18f);
    cube(x, 2.18f, z, 0.35f, 0.72f, 0.18f, 0.04f, 0.04f, 0.045f);
    glDisable(GL_LIGHTING);
    sphereShape(x, 2.38f, z - 0.10f, 0.08f, 0.08f, 0.035f, 1.0f, 0.05f, 0.03f);
    sphereShape(x, 2.18f, z - 0.10f, 0.08f, 0.08f, 0.035f, 1.0f, 0.72f, 0.08f);
    sphereShape(x, 1.98f, z - 0.10f, 0.08f, 0.08f, 0.035f, 0.05f, 1.0f, 0.15f + 0.40f * night);
    glEnable(GL_LIGHTING);
}

/* Draw small billboards placed away from the road to avoid blocking vision. */
static void drawBillboard(float x, float z, int type) {
    float night = nightAmount();
    cube(x - 0.40f, 0.75f, z, 0.06f, 1.50f, 0.06f, 0.22f, 0.22f, 0.23f);
    cube(x + 0.40f, 0.75f, z, 0.06f, 1.50f, 0.06f, 0.22f, 0.22f, 0.23f);
    cube(x, 1.62f, z, 1.30f, 0.52f, 0.09f, 0.06f, 0.07f, 0.09f);
    drawNeonSign(x, 1.62f, z - 0.06f, 1.05f, type % 4);
    if (night > 0.2f) {
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.2f, 0.8f, 1.0f, 0.12f * night);
        glBegin(GL_QUADS);
        glVertex3f(x - 0.74f, 1.24f, z - 0.10f);
        glVertex3f(x + 0.74f, 1.24f, z - 0.10f);
        glVertex3f(x + 0.86f, 2.04f, z - 0.10f);
        glVertex3f(x - 0.86f, 2.04f, z - 0.10f);
        glEnd();
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }
}

/* Draw different village/countryside houses. */
static void drawVillageHouse(float x, float z, float s, int type) {
    type %= 5;
    if (type == 0) {
        /* Warm brick village home. */
        cube(x, 0.45f * s, z, 1.42f * s, 0.90f * s, 1.08f * s, 0.86f, 0.52f, 0.28f);
        glPushMatrix();
        glTranslatef(x, 1.10f * s, z);
        glRotatef(45.0f, 0.0f, 1.0f, 0.0f);
        setColor3f(0.62f, 0.10f, 0.05f);
        glutSolidCone(1.08f * s, 0.70f * s, 4, 1);
        glPopMatrix();
        cube(x - 0.32f * s, 0.56f * s, z - 0.57f * s, 0.24f * s, 0.26f * s, 0.04f, 0.05f, 0.22f, 0.34f);
        cube(x + 0.32f * s, 0.38f * s, z - 0.58f * s, 0.32f * s, 0.52f * s, 0.04f, 0.22f, 0.10f, 0.04f);
    } else if (type == 1) {
        /* Green tin-roof country house. */
        cube(x, 0.42f * s, z, 1.60f * s, 0.84f * s, 0.98f * s, 0.58f, 0.78f, 0.50f);
        cube(x, 0.92f * s, z, 1.82f * s, 0.20f * s, 1.20f * s, 0.08f, 0.44f, 0.36f);
        cube(x - 0.52f * s, 0.35f * s, z - 0.52f * s, 0.24f * s, 0.48f * s, 0.04f, 0.16f, 0.08f, 0.04f);
        cube(x + 0.22f * s, 0.55f * s, z - 0.52f * s, 0.28f * s, 0.24f * s, 0.04f, 0.04f, 0.24f, 0.34f);
        cube(x + 0.58f * s, 0.55f * s, z - 0.52f * s, 0.28f * s, 0.24f * s, 0.04f, 0.04f, 0.24f, 0.34f);
    } else if (type == 2) {
        /* Two-storey painted rural house. */
        cube(x, 0.62f * s, z, 1.30f * s, 1.24f * s, 1.02f * s, 0.86f, 0.78f, 0.52f);
        cube(x, 1.30f * s, z, 1.46f * s, 0.24f * s, 1.14f * s, 0.28f, 0.20f, 0.12f);
        cube(x, 0.70f * s, z - 0.56f * s, 1.06f * s, 0.08f * s, 0.04f, 0.70f, 0.30f, 0.12f);
        cube(x - 0.32f * s, 0.86f * s, z - 0.56f * s, 0.22f * s, 0.24f * s, 0.04f, 0.06f, 0.26f, 0.42f);
        cube(x + 0.32f * s, 0.86f * s, z - 0.56f * s, 0.22f * s, 0.24f * s, 0.04f, 0.06f, 0.26f, 0.42f);
        cube(x, 0.30f * s, z - 0.57f * s, 0.28f * s, 0.46f * s, 0.04f, 0.20f, 0.10f, 0.04f);
    } else if (type == 3) {
        /* Small roadside shop. */
        cube(x, 0.42f * s, z, 1.70f * s, 0.84f * s, 0.98f * s, 0.24f, 0.62f, 0.72f);
        cube(x, 0.92f * s, z - 0.05f * s, 1.92f * s, 0.18f * s, 1.12f * s, 0.92f, 0.20f, 0.12f);
        cube(x, 0.70f * s, z - 0.56f * s, 1.32f * s, 0.26f * s, 0.05f, 0.95f, 0.78f, 0.18f);
        cube(x - 0.48f * s, 0.32f * s, z - 0.58f * s, 0.34f * s, 0.54f * s, 0.04f, 0.18f, 0.08f, 0.04f);
        cube(x + 0.28f * s, 0.42f * s, z - 0.58f * s, 0.62f * s, 0.22f * s, 0.04f, 0.05f, 0.20f, 0.28f);
    } else {
        /* Raised bamboo-style house. */
        cube(x, 0.66f * s, z, 1.46f * s, 0.76f * s, 1.04f * s, 0.68f, 0.42f, 0.24f);
        cube(x, 1.10f * s, z, 1.70f * s, 0.18f * s, 1.20f * s, 0.36f, 0.16f, 0.08f);
        cube(x - 0.55f * s, 0.18f * s, z - 0.35f * s, 0.10f * s, 0.36f * s, 0.10f * s, 0.24f, 0.12f, 0.05f);
        cube(x + 0.55f * s, 0.18f * s, z - 0.35f * s, 0.10f * s, 0.36f * s, 0.10f * s, 0.24f, 0.12f, 0.05f);
        cube(x - 0.55f * s, 0.18f * s, z + 0.35f * s, 0.10f * s, 0.36f * s, 0.10f * s, 0.24f, 0.12f, 0.05f);
        cube(x + 0.55f * s, 0.18f * s, z + 0.35f * s, 0.10f * s, 0.36f * s, 0.10f * s, 0.24f, 0.12f, 0.05f);
        cube(x - 0.30f * s, 0.72f * s, z - 0.56f * s, 0.22f * s, 0.22f * s, 0.04f, 0.04f, 0.22f, 0.32f);
        cube(x + 0.34f * s, 0.54f * s, z - 0.56f * s, 0.30f * s, 0.44f * s, 0.04f, 0.14f, 0.07f, 0.03f);
    }
}

/* Draw a crop/farm patch beside village roads. */
static void drawFarmPatch(float x, float z, float w, float d) {
    int i;
    glDisable(GL_LIGHTING);
    glBegin(GL_QUADS);
    setColor3f(0.13f, 0.42f, 0.10f);
    glVertex3f(x - w, 0.018f, z);
    glVertex3f(x + w, 0.018f, z);
    setColor3f(0.08f, 0.30f, 0.08f);
    glVertex3f(x + w, 0.018f, z - d);
    glVertex3f(x - w, 0.018f, z - d);
    glEnd();
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    setColor3f(0.22f, 0.56f, 0.16f);
    for (i = 0; i < 8; i++) {
        float rowX = x - w + i * (w * 2.0f / 7.0f);
        glVertex3f(rowX, 0.025f, z);
        glVertex3f(rowX + 0.8f, 0.025f, z - d);
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

/* Draw river water for village scenery. */
static void drawVillageRiver(float strength) {
    int i;
    if (strength < 0.04f) return;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (i = 0; i < 3; i++) {
        float z = -35.0f - i * 70.0f + fmodf(roadOffset * 0.28f, 70.0f);
        glBegin(GL_QUADS);
        glColor4f(0.04f, 0.24f, 0.48f, 0.58f * strength);
        glVertex3f(17.0f, 0.018f, z);
        glVertex3f(48.0f, 0.018f, z - 5.0f);
        glColor4f(0.08f, 0.45f, 0.70f, 0.60f * strength);
        glVertex3f(50.0f, 0.018f, z - 27.0f);
        glVertex3f(18.0f, 0.018f, z - 22.0f);
        glEnd();
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

#if 0
/* Legacy tunnel helper kept for future reuse; final cycle currently skips tunnel. */
static void drawTunnelRib(float z, float alpha, int portal) {
    float wallR = portal ? 0.34f : 0.18f;
    float wallG = portal ? 0.36f : 0.19f;
    float wallB = portal ? 0.38f : 0.22f;
    float beamDepth = portal ? 0.62f : 0.22f;

    cubeAlpha(-7.48f, 2.72f, z, 0.46f, 5.38f, beamDepth, wallR, wallG, wallB, alpha);
    cubeAlpha( 7.48f, 2.72f, z, 0.46f, 5.38f, beamDepth, wallR, wallG, wallB, alpha);
    cubeAlpha( 0.00f, 5.43f, z, 15.35f, 0.48f, beamDepth, wallR, wallG, wallB, alpha);
    cubeAlpha(-6.28f, 4.78f, z, 2.25f, 0.34f, beamDepth, wallR * 1.10f, wallG * 1.10f, wallB * 1.10f, alpha);
    cubeAlpha( 6.28f, 4.78f, z, 2.25f, 0.34f, beamDepth, wallR * 1.10f, wallG * 1.10f, wallB * 1.10f, alpha);
}

/* Draw one tunnel entrance/exit face for the legacy tunnel scene. */
static void drawTunnelPortalFace(float z, float alpha, int exitPortal) {
    if (alpha < 0.03f) return;

    drawTunnelRib(z, alpha, 1);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    if (exitPortal) {
        glColor4f(0.40f, 0.78f, 0.36f, 0.16f * alpha);
        glVertex3f(-5.95f, 0.08f, z - 0.05f);
        glVertex3f( 5.95f, 0.08f, z - 0.05f);
        glColor4f(0.70f, 0.94f, 0.82f, 0.28f * alpha);
        glVertex3f( 5.20f, 4.85f, z - 0.05f);
        glVertex3f(-5.20f, 4.85f, z - 0.05f);
    } else {
        glColor4f(0.005f, 0.008f, 0.014f, 0.72f * alpha);
        glVertex3f(-5.95f, 0.08f, z - 0.05f);
        glVertex3f( 5.95f, 0.08f, z - 0.05f);
        glColor4f(0.005f, 0.008f, 0.014f, 0.88f * alpha);
        glVertex3f( 5.20f, 4.85f, z - 0.05f);
        glVertex3f(-5.20f, 4.85f, z - 0.05f);
    }
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* Full tunnel scene renderer; kept unused after replacing tunnel with outdoor zones. */
static void drawTunnelScene(float strength) {
    int i;
    int current = currentEnvironment();
    int next = nextEnvironment();
    float blend = environmentBlend();
    float entryAlpha = (next == ENV_TUNNEL) ? blend : 0.0f;
    float exitAlpha = (current == ENV_TUNNEL && next == ENV_VILLAGE) ? blend : 0.0f;
    float interiorAlpha = clampf((strength - 0.24f) / 0.76f, 0.0f, 1.0f);
    float mouthAlpha = clampf((strength + entryAlpha * 0.85f) * 1.30f, 0.0f, 1.0f);
    float ribOffset = fmodf(roadOffset * 0.62f, 8.0f);
    if (strength < 0.04f && entryAlpha < 0.06f && exitAlpha < 0.06f) return;

    if (entryAlpha > 0.04f && strength < 0.55f) {
        drawTunnelPortalFace(-58.0f + 36.0f * entryAlpha, entryAlpha, 0);
    }

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    /* Main concrete side walls. */
    glColor4f(0.075f, 0.085f, 0.105f, 0.95f * interiorAlpha);
    glVertex3f(-7.65f, 0.02f, 34.0f);
    glVertex3f(-7.65f, 5.45f, 34.0f);
    glVertex3f(-7.65f, 5.45f, -ROAD_LENGTH);
    glVertex3f(-7.65f, 0.02f, -ROAD_LENGTH);

    glVertex3f(7.65f, 0.02f, 34.0f);
    glVertex3f(7.65f, 5.45f, 34.0f);
    glVertex3f(7.65f, 5.45f, -ROAD_LENGTH);
    glVertex3f(7.65f, 0.02f, -ROAD_LENGTH);

    /* Ceiling with a slightly darker rear gradient. */
    glColor4f(0.055f, 0.060f, 0.075f, 0.96f * interiorAlpha);
    glVertex3f(-7.65f, 5.45f, 34.0f);
    glVertex3f( 7.65f, 5.45f, 34.0f);
    glColor4f(0.030f, 0.034f, 0.045f, 0.98f * interiorAlpha);
    glVertex3f( 7.65f, 5.45f, -ROAD_LENGTH);
    glVertex3f(-7.65f, 5.45f, -ROAD_LENGTH);

    /* Dark tunnel mouth feel around the horizon. */
    glColor4f(0.005f, 0.008f, 0.014f, 0.30f * interiorAlpha);
    glVertex3f(-7.2f, 0.05f, -46.0f);
    glVertex3f( 7.2f, 0.05f, -46.0f);
    glColor4f(0.005f, 0.008f, 0.014f, 0.72f * interiorAlpha);
    glVertex3f( 6.3f, 4.85f, -ROAD_LENGTH);
    glVertex3f(-6.3f, 4.85f, -ROAD_LENGTH);
    glEnd();

    /* Tunnel road surface, curbs, wall panels, and ceiling service lines. */
    glBegin(GL_QUADS);
    glColor4f(0.020f, 0.023f, 0.030f, 0.50f * interiorAlpha);
    glVertex3f(-5.85f, 0.060f, 34.0f);
    glVertex3f( 5.85f, 0.060f, 34.0f);
    glColor4f(0.012f, 0.014f, 0.020f, 0.72f * interiorAlpha);
    glVertex3f( 5.85f, 0.060f, -ROAD_LENGTH);
    glVertex3f(-5.85f, 0.060f, -ROAD_LENGTH);

    glColor4f(0.18f, 0.19f, 0.20f, 0.72f * interiorAlpha);
    glVertex3f(-7.08f, 0.12f, 34.0f);
    glVertex3f(-6.28f, 0.12f, 34.0f);
    glVertex3f(-6.28f, 0.12f, -ROAD_LENGTH);
    glVertex3f(-7.08f, 0.12f, -ROAD_LENGTH);
    glVertex3f( 6.28f, 0.12f, 34.0f);
    glVertex3f( 7.08f, 0.12f, 34.0f);
    glVertex3f( 7.08f, 0.12f, -ROAD_LENGTH);
    glVertex3f( 6.28f, 0.12f, -ROAD_LENGTH);

    glColor4f(0.020f, 0.030f, 0.045f, 0.46f * interiorAlpha);
    glVertex3f(-0.30f, 5.37f, 34.0f);
    glVertex3f( 0.30f, 5.37f, 34.0f);
    glVertex3f( 0.30f, 5.37f, -ROAD_LENGTH);
    glVertex3f(-0.30f, 5.37f, -ROAD_LENGTH);
    glColor4f(0.06f, 0.08f, 0.10f, 0.36f * interiorAlpha);
    glVertex3f(-4.75f, 5.34f, 34.0f);
    glVertex3f(-4.42f, 5.34f, 34.0f);
    glVertex3f(-4.42f, 5.34f, -ROAD_LENGTH);
    glVertex3f(-4.75f, 5.34f, -ROAD_LENGTH);
    glVertex3f( 4.42f, 5.34f, 34.0f);
    glVertex3f( 4.75f, 5.34f, 34.0f);
    glVertex3f( 4.75f, 5.34f, -ROAD_LENGTH);
    glVertex3f( 4.42f, 5.34f, -ROAD_LENGTH);
    glEnd();

    glLineWidth(1.0f);
    glBegin(GL_LINES);
    glColor4f(0.50f, 0.54f, 0.58f, 0.22f * interiorAlpha);
    glVertex3f(-7.56f, 2.52f, 34.0f);
    glVertex3f(-7.56f, 2.52f, -ROAD_LENGTH);
    glVertex3f(-7.56f, 3.78f, 34.0f);
    glVertex3f(-7.56f, 3.78f, -ROAD_LENGTH);
    glVertex3f( 7.56f, 2.52f, 34.0f);
    glVertex3f( 7.56f, 2.52f, -ROAD_LENGTH);
    glVertex3f( 7.56f, 3.78f, 34.0f);
    glVertex3f( 7.56f, 3.78f, -ROAD_LENGTH);
    glVertex3f(-4.65f, 5.32f, 34.0f);
    glVertex3f(-4.65f, 5.32f, -ROAD_LENGTH);
    glVertex3f( 4.65f, 5.32f, 34.0f);
    glVertex3f( 4.65f, 5.32f, -ROAD_LENGTH);
    glEnd();

    for (i = 0; i < 36; i++) {
        float z = -ROAD_LENGTH + ribOffset + i * 6.0f;
        if (z > 32.0f) continue;
        glBegin(GL_LINES);
        glColor4f(0.58f, 0.61f, 0.64f, 0.24f * interiorAlpha);
        glVertex3f(-7.555f, 0.62f, z);
        glVertex3f(-7.555f, 5.12f, z);
        glVertex3f( 7.555f, 0.62f, z);
        glVertex3f( 7.555f, 5.12f, z);
        glColor4f(0.38f, 0.42f, 0.46f, 0.16f * interiorAlpha);
        glVertex3f(-5.75f, 0.071f, z);
        glVertex3f( 5.75f, 0.071f, z);
        glEnd();
    }

    for (i = 0; i < 22; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.38f, 16.0f) + i * 16.0f;
        if (z > 30.0f) continue;
        glBegin(GL_QUADS);
        glColor4f(0.015f, 0.020f, 0.026f, 0.58f * interiorAlpha);
        glVertex3f(-7.59f, 2.92f, z);
        glVertex3f(-7.59f, 3.42f, z);
        glVertex3f(-7.59f, 3.42f, z + 1.05f);
        glVertex3f(-7.59f, 2.92f, z + 1.05f);
        glVertex3f( 7.59f, 2.92f, z);
        glVertex3f( 7.59f, 3.42f, z);
        glVertex3f( 7.59f, 3.42f, z + 1.05f);
        glVertex3f( 7.59f, 2.92f, z + 1.05f);

        glColor4f(1.0f, 0.84f, 0.40f, 0.12f * interiorAlpha);
        glVertex3f(-4.4f, 0.066f, z + 0.10f);
        glVertex3f( 4.4f, 0.066f, z + 0.10f);
        glVertex3f( 4.4f, 0.066f, z + 1.65f);
        glVertex3f(-4.4f, 0.066f, z + 1.65f);
        glEnd();
    }

    /* Wall lane guides and lower service panels. */
    for (i = 0; i < 34; i++) {
        float z = -ROAD_LENGTH + ribOffset + i * 8.0f;
        if (z > 32.0f) continue;
        glBegin(GL_QUADS);
        glColor4f(0.08f, 0.45f, 0.78f, 0.26f * interiorAlpha);
        glVertex3f(-7.58f, 1.15f, z);
        glVertex3f(-7.58f, 1.42f, z);
        glVertex3f(-7.58f, 1.42f, z + 3.4f);
        glVertex3f(-7.58f, 1.15f, z + 3.4f);
        glVertex3f( 7.58f, 1.15f, z);
        glVertex3f( 7.58f, 1.42f, z);
        glVertex3f( 7.58f, 1.42f, z + 3.4f);
        glVertex3f( 7.58f, 1.15f, z + 3.4f);

        glColor4f(0.95f, 0.76f, 0.28f, 0.38f * interiorAlpha);
        glVertex3f(-7.57f, 0.42f, z);
        glVertex3f(-7.57f, 0.56f, z);
        glVertex3f(-7.57f, 0.56f, z + 2.1f);
        glVertex3f(-7.57f, 0.42f, z + 2.1f);
        glVertex3f( 7.57f, 0.42f, z);
        glVertex3f( 7.57f, 0.56f, z);
        glVertex3f( 7.57f, 0.56f, z + 2.1f);
        glVertex3f( 7.57f, 0.42f, z + 2.1f);
        glEnd();
    }

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);

    drawTunnelRib(0.0f, 0.78f * mouthAlpha, 1);
    drawTunnelRib(-46.0f, 0.55f * interiorAlpha, 1);
    for (i = 0; i < 30; i++) {
        float z = -ROAD_LENGTH + ribOffset + i * 8.0f;
        if (z > 30.0f) continue;
        drawTunnelRib(z, 0.28f * interiorAlpha, 0);
    }

    if (exitAlpha > 0.08f) {
        drawTunnelPortalFace(-70.0f + 48.0f * exitAlpha, exitAlpha, 1);
    }

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (i = 0; i < 34; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.82f, 7.0f) + i * 7.0f;
        if (z > 30.0f) continue;
        glBegin(GL_QUADS);
        glColor4f(0.20f, 0.22f, 0.24f, 0.68f * interiorAlpha);
        glVertex3f(-2.20f, 5.20f, z - 0.10f);
        glVertex3f( 2.20f, 5.20f, z - 0.10f);
        glVertex3f( 2.20f, 5.20f, z + 1.20f);
        glVertex3f(-2.20f, 5.20f, z + 1.20f);
        glColor4f(1.0f, 0.90f, 0.62f, 0.86f * interiorAlpha);
        glVertex3f(-1.55f, 5.18f, z);
        glVertex3f( 1.55f, 5.18f, z);
        glVertex3f( 1.55f, 5.18f, z + 0.92f);
        glVertex3f(-1.55f, 5.18f, z + 0.92f);
        glColor4f(1.0f, 0.78f, 0.34f, 0.22f * interiorAlpha);
        glVertex3f(-3.35f, 5.13f, z - 0.25f);
        glVertex3f( 3.35f, 5.13f, z - 0.25f);
        glVertex3f( 3.35f, 5.13f, z + 1.40f);
        glVertex3f(-3.35f, 5.13f, z + 1.40f);
        glColor4f(1.0f, 0.82f, 0.40f, 0.16f * interiorAlpha);
        glVertex3f(-2.85f, 0.068f, z - 0.10f);
        glVertex3f( 2.85f, 0.068f, z - 0.10f);
        glVertex3f( 3.55f, 0.068f, z + 2.10f);
        glVertex3f(-3.55f, 0.068f, z + 2.10f);
        glColor4f(1.0f, 0.18f, 0.12f, 0.54f * interiorAlpha);
        glVertex3f(-7.565f, 1.72f, z + 1.20f);
        glVertex3f(-7.565f, 1.98f, z + 1.20f);
        glVertex3f(-7.565f, 1.98f, z + 1.48f);
        glVertex3f(-7.565f, 1.72f, z + 1.48f);
        glColor4f(0.24f, 0.84f, 1.0f, 0.44f * interiorAlpha);
        glVertex3f( 7.565f, 1.72f, z + 1.20f);
        glVertex3f( 7.565f, 1.98f, z + 1.20f);
        glVertex3f( 7.565f, 1.98f, z + 1.48f);
        glVertex3f( 7.565f, 1.72f, z + 1.48f);
        glEnd();
    }

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}
#endif

/* Draw one cactus for desert scenery. */
static void drawCactus(float x, float z, float s) {
    cube(x, 0.55f * s, z, 0.20f * s, 1.10f * s, 0.20f * s, 0.06f, 0.36f, 0.18f);
    cube(x - 0.30f * s, 0.75f * s, z, 0.16f * s, 0.55f * s, 0.16f * s, 0.06f, 0.34f, 0.16f);
    cube(x + 0.30f * s, 0.88f * s, z, 0.16f * s, 0.48f * s, 0.16f * s, 0.07f, 0.39f, 0.18f);
    cube(x - 0.18f * s, 0.78f * s, z, 0.34f * s, 0.14f * s, 0.14f * s, 0.06f, 0.34f, 0.16f);
    cube(x + 0.18f * s, 1.05f * s, z, 0.34f * s, 0.14f * s, 0.14f * s, 0.07f, 0.39f, 0.18f);
}

/* Draw one palm tree; currently kept as reusable scenery code. */
static void drawPalmTree(float x, float z, float s) {
    int i;
    cube(x, 0.78f * s, z, 0.20f * s, 1.55f * s, 0.20f * s, 0.48f, 0.27f, 0.10f);
    glPushMatrix();
    glTranslatef(x, 1.62f * s, z);
    for (i = 0; i < 6; i++) {
        glPushMatrix();
        glRotatef(i * 60.0f, 0.0f, 1.0f, 0.0f);
        glRotatef(24.0f, 1.0f, 0.0f, 0.0f);
        cube(0.0f, 0.0f, 0.42f * s, 0.16f * s, 0.08f * s, 1.05f * s, 0.05f, 0.42f, 0.16f);
        glPopMatrix();
    }
    glPopMatrix();
}

/* Legacy beach umbrella object, no longer used in the active environment cycle. */
static void drawBeachUmbrella(float x, float z, float s, int colorType) {
    float r = colorType ? 0.95f : 0.10f;
    float g = colorType ? 0.18f : 0.52f;
    float b = colorType ? 0.12f : 0.95f;
    cube(x, 0.36f * s, z, 0.06f * s, 0.72f * s, 0.06f * s, 0.72f, 0.56f, 0.36f);
    glPushMatrix();
    glTranslatef(x, 0.86f * s, z);
    glRotatef(45.0f, 0.0f, 1.0f, 0.0f);
    setColor3f(r, g, b);
    glutSolidCone(0.62f * s, 0.34f * s, 12, 2);
    glPopMatrix();
    cube(x + 0.72f * s, 0.11f * s, z + 0.20f * s, 0.88f * s, 0.06f * s, 0.36f * s,
         0.90f, 0.78f, 0.50f);
}

/* Legacy beach hut object, no longer used in the active environment cycle. */
static void drawBeachHut(float x, float z, float s, int type) {
    float r = type ? 0.18f : 0.92f;
    float g = type ? 0.52f : 0.58f;
    float b = type ? 0.68f : 0.18f;
    cube(x, 0.42f * s, z, 1.35f * s, 0.84f * s, 0.96f * s, r, g, b);
    glPushMatrix();
    glTranslatef(x, 1.02f * s, z);
    glRotatef(45.0f, 0.0f, 1.0f, 0.0f);
    setColor3f(0.78f, 0.34f, 0.10f);
    glutSolidCone(1.04f * s, 0.48f * s, 4, 1);
    glPopMatrix();
    cube(x - 0.35f * s, 0.46f * s, z - 0.50f * s, 0.24f * s, 0.24f * s, 0.04f, 0.04f, 0.24f, 0.34f);
    cube(x + 0.28f * s, 0.30f * s, z - 0.51f * s, 0.30f * s, 0.44f * s, 0.04f, 0.18f, 0.08f, 0.04f);
}

/* Draw the desert environment using sand color, cactuses, and dune shapes. */
static void drawDesertScene(float strength) {
    int i;
    if (strength < 0.04f) return;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (i = 0; i < 5; i++) {
        float z = -18.0f - i * 42.0f + fmodf(roadOffset * 0.22f, 42.0f);
        glBegin(GL_TRIANGLES);
        glColor4f(0.70f, 0.43f, 0.16f, 0.58f * strength);
        glVertex3f(-58.0f, 0.025f, z - 26.0f);
        glColor4f(0.96f, 0.70f, 0.30f, 0.66f * strength);
        glVertex3f(-34.0f, 2.9f, z - 37.0f);
        glColor4f(0.62f, 0.35f, 0.12f, 0.52f * strength);
        glVertex3f(-11.0f, 0.025f, z - 26.0f);

        glColor4f(0.82f, 0.52f, 0.22f, 0.56f * strength);
        glVertex3f(12.0f, 0.025f, z - 22.0f);
        glColor4f(1.0f, 0.76f, 0.35f, 0.66f * strength);
        glVertex3f(36.0f, 2.5f, z - 34.0f);
        glColor4f(0.62f, 0.35f, 0.12f, 0.50f * strength);
        glVertex3f(60.0f, 0.025f, z - 22.0f);
        glEnd();
    }
    for (i = 0; i < 18; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.32f, 18.0f) + i * 18.0f;
        float x = (i % 2 == 0) ? -12.0f - (i % 4) * 3.2f : 12.0f + (i % 4) * 3.2f;
        drawCactus(x, z, 0.75f + 0.08f * (i % 3));
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* Draw the final flower garden environment with colorful flower rows. */
static void drawFlowerGardenScene(float strength) {
    int i, j;
    if (strength < 0.04f) return;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    /* Rich garden grass beside both sides of the road. */
    glColor4f(0.06f, 0.34f, 0.10f, 0.68f * strength);
    glVertex3f(-80.0f, 0.026f, 34.0f);
    glVertex3f(-6.7f, 0.026f, 34.0f);
    glColor4f(0.10f, 0.50f, 0.16f, 0.74f * strength);
    glVertex3f(-7.4f, 0.026f, -ROAD_LENGTH);
    glVertex3f(-80.0f, 0.026f, -ROAD_LENGTH);

    glColor4f(0.06f, 0.34f, 0.10f, 0.68f * strength);
    glVertex3f(6.7f, 0.026f, 34.0f);
    glVertex3f(80.0f, 0.026f, 34.0f);
    glColor4f(0.10f, 0.50f, 0.16f, 0.74f * strength);
    glVertex3f(80.0f, 0.026f, -ROAD_LENGTH);
    glVertex3f(7.4f, 0.026f, -ROAD_LENGTH);
    glEnd();

    for (i = 0; i < 34; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.30f, 7.0f) + i * 7.0f;
        if (z > 32.0f) continue;
        for (j = 0; j < 4; j++) {
            float sideX = 10.0f + j * 4.2f;
            float rr = (j == 0) ? 0.95f : (j == 1) ? 1.0f : (j == 2) ? 0.25f : 0.95f;
            float gg = (j == 0) ? 0.18f : (j == 1) ? 0.78f : (j == 2) ? 0.55f : 0.35f;
            float bb = (j == 0) ? 0.28f : (j == 1) ? 0.12f : (j == 2) ? 1.0f : 0.85f;

            glBegin(GL_QUADS);
            glColor4f(rr, gg, bb, 0.78f * strength);
            glVertex3f(-sideX - 0.38f, 0.055f, z);
            glVertex3f(-sideX + 0.38f, 0.055f, z + 0.20f);
            glVertex3f(-sideX + 0.28f, 0.055f, z + 0.90f);
            glVertex3f(-sideX - 0.48f, 0.055f, z + 0.70f);
            glVertex3f(sideX - 0.38f, 0.055f, z);
            glVertex3f(sideX + 0.38f, 0.055f, z + 0.20f);
            glVertex3f(sideX + 0.28f, 0.055f, z + 0.90f);
            glVertex3f(sideX - 0.48f, 0.055f, z + 0.70f);
            glEnd();
        }
    }

    /* Low white garden fences following the road. */
    for (i = 0; i < 26; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.45f, 9.0f) + i * 9.0f;
        if (z > 32.0f) continue;
        cubeAlpha(-8.2f, 0.34f, z, 0.12f, 0.58f, 0.12f, 0.95f, 0.94f, 0.86f, 0.76f * strength);
        cubeAlpha( 8.2f, 0.34f, z, 0.12f, 0.58f, 0.12f, 0.95f, 0.94f, 0.86f, 0.76f * strength);
        cubeAlpha(-8.2f, 0.56f, z + 2.2f, 0.14f, 0.10f, 4.8f, 0.95f, 0.94f, 0.86f, 0.56f * strength);
        cubeAlpha( 8.2f, 0.56f, z + 2.2f, 0.14f, 0.10f, 4.8f, 0.95f, 0.94f, 0.86f, 0.56f * strength);
    }

    for (i = 0; i < 10; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.24f, 24.0f) + i * 24.0f;
        if (z > 30.0f) continue;
        drawTree(-17.0f - (i % 3) * 3.0f, z, 0.58f + 0.08f * (i % 2));
        drawTree( 17.0f + (i % 3) * 3.0f, z + 5.0f, 0.58f + 0.08f * (i % 2));
        cubeAlpha(-12.0f, 1.25f, z + 2.2f, 0.18f, 1.80f, 0.18f, 0.62f, 0.38f, 0.18f, 0.72f * strength);
        cubeAlpha( 12.0f, 1.25f, z + 2.2f, 0.18f, 1.80f, 0.18f, 0.62f, 0.38f, 0.18f, 0.72f * strength);
        cubeAlpha( 0.0f, 2.10f, z + 2.2f, 24.0f, 0.16f, 0.18f, 0.62f, 0.38f, 0.18f, 0.58f * strength);
    }

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* Legacy beach environment; kept unused because beach was replaced by river. */
static void drawBeachScene(float strength) {
    int i;
    if (strength < 0.04f) return;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Sand shoulder beside the road. */
    glBegin(GL_QUADS);
    glColor4f(0.88f, 0.72f, 0.42f, 0.76f * strength);
    glVertex3f(-18.0f, 0.020f, 34.0f);
    glVertex3f(-6.7f, 0.020f, 34.0f);
    glColor4f(0.96f, 0.82f, 0.52f, 0.82f * strength);
    glVertex3f(-7.2f, 0.020f, -ROAD_LENGTH);
    glVertex3f(-19.0f, 0.020f, -ROAD_LENGTH);

    glColor4f(0.90f, 0.72f, 0.40f, 0.56f * strength);
    glVertex3f(7.0f, 0.020f, 34.0f);
    glVertex3f(24.0f, 0.020f, 34.0f);
    glColor4f(0.98f, 0.83f, 0.50f, 0.62f * strength);
    glVertex3f(25.0f, 0.020f, -ROAD_LENGTH);
    glVertex3f(7.4f, 0.020f, -ROAD_LENGTH);
    glEnd();

    /* Deep ocean with color bands for depth. */
    glBegin(GL_QUADS);
    glColor4f(0.01f, 0.18f, 0.42f, 0.82f * strength);
    glVertex3f(-80.0f, 0.018f, 34.0f);
    glVertex3f(-28.0f, 0.018f, 34.0f);
    glColor4f(0.03f, 0.40f, 0.66f, 0.86f * strength);
    glVertex3f(-30.0f, 0.018f, -ROAD_LENGTH);
    glVertex3f(-80.0f, 0.018f, -ROAD_LENGTH);

    glColor4f(0.05f, 0.54f, 0.72f, 0.72f * strength);
    glVertex3f(-30.0f, 0.019f, 34.0f);
    glVertex3f(-15.0f, 0.019f, 34.0f);
    glColor4f(0.08f, 0.68f, 0.82f, 0.78f * strength);
    glVertex3f(-16.5f, 0.019f, -ROAD_LENGTH);
    glVertex3f(-31.0f, 0.019f, -ROAD_LENGTH);
    glEnd();

    /* Curved foam shoreline. */
    for (i = 0; i < 32; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.48f, 9.0f) + i * 9.0f;
        float shore = -15.8f - sinf((z + roadOffset * 0.08f) * 0.09f) * 1.0f;
        if (z > 34.0f) continue;
        glBegin(GL_QUADS);
        glColor4f(0.92f, 0.98f, 1.0f, 0.58f * strength);
        glVertex3f(shore - 1.8f, 0.040f, z);
        glVertex3f(shore + 1.8f, 0.040f, z + 0.7f);
        glColor4f(0.92f, 0.98f, 1.0f, 0.08f * strength);
        glVertex3f(shore + 2.7f, 0.040f, z + 2.4f);
        glVertex3f(shore - 1.1f, 0.040f, z + 1.8f);
        glEnd();
    }

    /* Ocean wave rows. */
    for (i = 0; i < 26; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.42f, 8.0f) + i * 8.0f;
        float wave = sinf((z + roadOffset * 0.18f) * 0.12f) * 1.4f;
        glBegin(GL_LINES);
        glColor4f(0.78f, 0.94f, 1.0f, 0.34f * strength);
        glVertex3f(-76.0f, 0.041f, z);
        glVertex3f(-34.0f + wave, 0.041f, z + 1.7f);
        glColor4f(0.88f, 0.98f, 1.0f, 0.24f * strength);
        glVertex3f(-30.0f + wave * 0.5f, 0.042f, z + 2.3f);
        glVertex3f(-18.0f, 0.042f, z + 3.0f);
        glEnd();
    }

    /* Beach details. */
    for (i = 0; i < 8; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.24f, 26.0f) + i * 26.0f;
        if (z > 30.0f) continue;
        drawBeachUmbrella(12.0f + (i % 3) * 3.2f, z, 0.85f, i % 2);
    }

    for (i = 0; i < 7; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.20f, 30.0f) + i * 30.0f;
        if (z > 30.0f) continue;
        drawBeachHut(18.0f + (i % 2) * 3.2f, z + 4.0f, 0.72f, i % 2);
    }

    for (i = 0; i < 6; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.18f, 34.0f) + i * 34.0f;
        if (z > 28.0f) continue;
        glBegin(GL_TRIANGLES);
        glColor4f(0.55f, 0.16f, 0.08f, 0.70f * strength);
        glVertex3f(-46.0f, 0.055f, z);
        glVertex3f(-41.0f, 0.055f, z + 0.7f);
        glColor4f(0.84f, 0.35f, 0.12f, 0.75f * strength);
        glVertex3f(-43.2f, 0.52f, z + 1.7f);
        glEnd();
    }

    for (i = 0; i < 16; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.25f, 24.0f) + i * 24.0f;
        float x = (i % 2 == 0) ? 14.0f + (i % 3) * 3.0f : -10.0f - (i % 3) * 2.2f;
        drawPalmTree(x, z, 0.78f + 0.08f * (i % 3));
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* Draw the river-area environment with continuous blue water beside the road. */
static void drawRiverScene(float strength) {
    int i;
    if (strength < 0.04f) return;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    /* Wide blue river beside the highway. */
    glColor4f(0.02f, 0.20f, 0.42f, 0.72f * strength);
    glVertex3f(-80.0f, 0.018f, 34.0f);
    glVertex3f(-20.0f, 0.018f, 34.0f);
    glColor4f(0.06f, 0.44f, 0.66f, 0.78f * strength);
    glVertex3f(-23.5f, 0.018f, -ROAD_LENGTH);
    glVertex3f(-80.0f, 0.018f, -ROAD_LENGTH);

    glColor4f(0.08f, 0.36f, 0.20f, 0.54f * strength);
    glVertex3f(-21.0f, 0.030f, 34.0f);
    glVertex3f(-7.0f, 0.030f, 34.0f);
    glColor4f(0.05f, 0.28f, 0.13f, 0.56f * strength);
    glVertex3f(-7.5f, 0.030f, -ROAD_LENGTH);
    glVertex3f(-24.0f, 0.030f, -ROAD_LENGTH);

    glColor4f(0.07f, 0.34f, 0.18f, 0.42f * strength);
    glVertex3f(7.0f, 0.028f, 34.0f);
    glVertex3f(24.0f, 0.028f, 34.0f);
    glColor4f(0.05f, 0.26f, 0.14f, 0.48f * strength);
    glVertex3f(24.5f, 0.028f, -ROAD_LENGTH);
    glVertex3f(7.4f, 0.028f, -ROAD_LENGTH);
    glEnd();

    for (i = 0; i < 30; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.35f, 9.0f) + i * 9.0f;
        float bend = sinf((z + roadOffset * 0.12f) * 0.10f) * 1.8f;
        if (z > 34.0f) continue;
        glBegin(GL_LINES);
        glColor4f(0.70f, 0.95f, 1.0f, 0.28f * strength);
        glVertex3f(-74.0f, 0.044f, z);
        glVertex3f(-31.0f + bend, 0.044f, z + 2.0f);
        glColor4f(0.55f, 0.82f, 0.95f, 0.18f * strength);
        glVertex3f(-28.0f + bend * 0.5f, 0.044f, z + 2.8f);
        glVertex3f(-18.0f, 0.044f, z + 3.6f);
        glEnd();
    }

    for (i = 0; i < 8; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.22f, 28.0f) + i * 28.0f;
        if (z > 30.0f) continue;
        cubeAlpha(-14.5f, 0.30f, z, 0.22f, 0.60f, 0.22f, 0.34f, 0.22f, 0.10f, 0.70f * strength);
        cubeAlpha(-12.0f, 0.30f, z + 0.7f, 0.22f, 0.60f, 0.22f, 0.34f, 0.22f, 0.10f, 0.70f * strength);
        cubeAlpha(-13.25f, 0.78f, z + 0.35f, 3.4f, 0.16f, 0.24f, 0.48f, 0.30f, 0.12f, 0.78f * strength);
    }

    for (i = 0; i < 18; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.28f, 18.0f) + i * 18.0f;
        float x = (i % 2 == 0) ? -13.0f - (i % 3) * 2.6f : 12.5f + (i % 3) * 2.8f;
        drawTree(x, z, 0.62f + 0.08f * (i % 3));
    }

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* Legacy snow environment; kept unused because snow was removed from final design. */
static void drawSnowScene(float strength) {
    int i;
    if (strength < 0.04f) return;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    glColor4f(0.86f, 0.93f, 0.98f, 0.78f * strength);
    glVertex3f(-80.0f, 0.030f, 34.0f);
    glVertex3f(-6.8f, 0.030f, 34.0f);
    glColor4f(0.76f, 0.88f, 0.96f, 0.82f * strength);
    glVertex3f(-7.4f, 0.030f, -ROAD_LENGTH);
    glVertex3f(-80.0f, 0.030f, -ROAD_LENGTH);

    glColor4f(0.88f, 0.95f, 1.0f, 0.76f * strength);
    glVertex3f(6.8f, 0.030f, 34.0f);
    glVertex3f(80.0f, 0.030f, 34.0f);
    glColor4f(0.78f, 0.90f, 0.98f, 0.82f * strength);
    glVertex3f(80.0f, 0.030f, -ROAD_LENGTH);
    glVertex3f(7.4f, 0.030f, -ROAD_LENGTH);

    /* Frozen blue lake/ice strip on the left. */
    glColor4f(0.52f, 0.82f, 0.96f, 0.48f * strength);
    glVertex3f(-62.0f, 0.042f, 34.0f);
    glVertex3f(-24.0f, 0.042f, 34.0f);
    glColor4f(0.78f, 0.95f, 1.0f, 0.52f * strength);
    glVertex3f(-27.0f, 0.042f, -ROAD_LENGTH);
    glVertex3f(-64.0f, 0.042f, -ROAD_LENGTH);
    glEnd();

    for (i = 0; i < 26; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.30f, 10.0f) + i * 10.0f;
        if (z > 32.0f) continue;
        glBegin(GL_LINES);
        glColor4f(0.92f, 0.98f, 1.0f, 0.42f * strength);
        glVertex3f(-60.0f, 0.055f, z);
        glVertex3f(-28.0f, 0.055f, z + 2.0f);
        glColor4f(0.72f, 0.88f, 1.0f, 0.28f * strength);
        glVertex3f(13.0f, 0.055f, z + 1.5f);
        glVertex3f(34.0f, 0.055f, z + 4.0f);
        glEnd();
    }

    for (i = 0; i < 18; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.28f, 16.0f) + i * 16.0f;
        float x = (i % 2 == 0) ? -15.0f - (i % 3) * 2.6f : 14.0f + (i % 3) * 2.8f;
        drawTree(x, z, 0.62f + 0.08f * (i % 3));
        sphereShape(x + 0.30f, 0.18f, z + 0.60f, 0.55f, 0.14f, 0.45f, 0.92f, 0.96f, 1.0f);
    }

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* Draw the hilly environment with layered green hills and trees. */
static void drawHillScene(float strength) {
    int i;
    if (strength < 0.04f) return;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (i = 0; i < 6; i++) {
        float z = -22.0f - i * 38.0f + fmodf(roadOffset * 0.20f, 38.0f);
        glBegin(GL_TRIANGLES);
        glColor4f(0.05f, 0.20f, 0.12f, 0.60f * strength);
        glVertex3f(-62.0f, 0.026f, z - 24.0f);
        glColor4f(0.22f, 0.48f, 0.24f, 0.72f * strength);
        glVertex3f(-34.0f, 4.8f, z - 40.0f);
        glColor4f(0.04f, 0.17f, 0.10f, 0.58f * strength);
        glVertex3f(-8.0f, 0.026f, z - 24.0f);

        glColor4f(0.07f, 0.23f, 0.12f, 0.62f * strength);
        glVertex3f(8.0f, 0.026f, z - 24.0f);
        glColor4f(0.30f, 0.58f, 0.28f, 0.72f * strength);
        glVertex3f(35.0f, 4.4f, z - 38.0f);
        glColor4f(0.05f, 0.17f, 0.09f, 0.58f * strength);
        glVertex3f(62.0f, 0.026f, z - 24.0f);
        glEnd();
    }
    for (i = 0; i < 18; i++) {
        float z = -ROAD_LENGTH + fmodf(roadOffset * 0.28f, 16.0f) + i * 16.0f;
        float x = (i % 2 == 0) ? -16.0f - (i % 3) * 2.5f : 16.0f + (i % 3) * 2.5f;
        drawTree(x, z, 0.55f + 0.08f * (i % 4));
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* Initialize arrays of reusable roadside objects before the game starts. */
static void initEnvironmentObjects(void) {
    int i;
    for (i = 0; i < MAX_TREES; i++) {
        trees[i].z = -10.0f - i * 6.0f;
        trees[i].x = (i % 2 == 0) ? frandRange(-18.0f, -9.0f) : frandRange(9.0f, 18.0f);
        trees[i].scale = frandRange(0.7f, 1.25f);
        trees[i].type = i % 3;
    }
    for (i = 0; i < MAX_LIGHTS; i++) {
        lights[i].z = -8.0f - i * 10.0f;
        lights[i].x = (i % 2 == 0) ? -7.0f : 7.0f;
    }
    for (i = 0; i < MAX_BUILDINGS; i++) {
        buildings[i].z = -20.0f - i * 13.0f;
        buildings[i].x = (i % 2 == 0) ? frandRange(-34.0f, -18.0f) : frandRange(18.0f, 34.0f);
        buildings[i].scale = frandRange(3.5f, 10.5f);
        buildings[i].type = i % 5;
    }
    for (i = 0; i < MAX_BARRIERS; i++) {
        barriers[i].z = -4.0f - i * 4.0f;
        barriers[i].x = (i % 2 == 0) ? -6.15f : 6.15f;
    }
}

/* Move scenery forward; when it passes the camera, reset it far ahead. */
static void updateLoopedObject(float *z, float dt, float resetBehind) {
    *z += gameSpeed * dt;
    if (*z > 34.0f) *z = resetBehind;
}

/* Draw all scenery systems and blend between active environments. */
static void drawEnvironment(float dt) {
    int i;
    float farReset;
    float day = daylightAmount();
    float night = nightAmount();
    float sunset = sunsetAmount();
    float cityW = environmentWeight(ENV_CITY);
    float desertW = environmentWeight(ENV_DESERT);
    float riverW = environmentWeight(ENV_RIVER);
    float hillsW = environmentWeight(ENV_HILLS);
    float villageW = environmentWeight(ENV_VILLAGE);
    float flowerW = environmentWeight(ENV_FLOWER);

    /* Distant skyline haze and river sections. */
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor4f(1.0f, 0.55f, 0.24f, 0.10f * sunset);
    glVertex3f(-80.0f, 0.02f, -42.0f);
    glVertex3f( 80.0f, 0.02f, -42.0f);
    glColor4f(0.10f, 0.20f, 0.38f, 0.00f);
    glVertex3f( 80.0f, 0.02f, -ROAD_LENGTH);
    glVertex3f(-80.0f, 0.02f, -ROAD_LENGTH);
    glEnd();
    for (i = 0; i < 3; i++) {
        float z = -40.0f - i * 65.0f + fmodf(roadOffset * 0.35f, 65.0f);
        glBegin(GL_QUADS);
        glColor4f(0.03f + 0.05f * day, 0.18f + 0.20f * day, 0.34f + 0.30f * day,
                  0.62f * (0.30f + villageW));
        glVertex3f(-44.0f, 0.012f, z);
        glVertex3f(-20.0f, 0.012f, z);
        glVertex3f(-22.0f, 0.012f, z - 28.0f);
        glVertex3f(-48.0f, 0.012f, z - 28.0f);
        glEnd();
        glBegin(GL_LINES);
        glColor4f(0.85f, 0.95f, 1.0f, 0.18f + 0.15f * night);
        glVertex3f(-43.0f, 0.03f, z - 3.0f);
        glVertex3f(-21.0f, 0.03f, z - 7.0f);
        glVertex3f(-45.0f, 0.03f, z - 15.0f);
        glVertex3f(-23.0f, 0.03f, z - 19.0f);
        glEnd();
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);

    for (i = 0; i < MAX_TREES; i++) {
        updateLoopedObject(&trees[i].z, dt, -ROAD_LENGTH + frandRange(-20.0f, 0.0f));
        if (villageW > 0.06f) {
            drawTree(trees[i].x, trees[i].z, trees[i].scale * (0.95f + 0.25f * villageW));
        } else if (flowerW > 0.08f) {
            drawTree(trees[i].x * 0.85f, trees[i].z, trees[i].scale * 0.62f);
        } else if (riverW > 0.08f) {
            drawTree(trees[i].x * 0.82f, trees[i].z, trees[i].scale * 0.74f);
        } else if (hillsW > 0.08f) {
            drawTree(trees[i].x * 0.92f, trees[i].z, trees[i].scale * (0.80f + 0.25f * hillsW));
        } else if (cityW > 0.08f) {
            drawTree(trees[i].x * 0.70f, trees[i].z, trees[i].scale * 0.68f);
        }
    }
    for (i = 0; i < MAX_LIGHTS; i++) {
        updateLoopedObject(&lights[i].z, dt, -ROAD_LENGTH);
        if (cityW > 0.06f) {
            drawStreetLight(lights[i].x, lights[i].z);
            if (i % 7 == 2 && cityW > 0.18f) drawTrafficLight(lights[i].x * 1.18f, lights[i].z + 2.4f);
        }
    }
    for (i = 0; i < MAX_BUILDINGS; i++) {
        farReset = -ROAD_LENGTH - frandRange(0.0f, 50.0f);
        updateLoopedObject(&buildings[i].z, dt, farReset);
        if (cityW > 0.05f) {
            drawBuilding(buildings[i].x, buildings[i].z,
                         2.2f + 0.30f * buildings[i].type,
                         buildings[i].scale,
                         2.0f + 0.25f * (buildings[i].type % 3),
                         0.35f + 0.12f * buildings[i].type,
                         0.34f + 0.08f * buildings[i].type,
                         0.42f + 0.05f * buildings[i].type,
                         buildings[i].type);
            if (i % 10 == 1) {
                drawBillboard(buildings[i].x * 0.95f, buildings[i].z + 7.0f, buildings[i].type);
            }
        }
        if (villageW > 0.05f && i % 2 == 0) {
            drawVillageHouse(buildings[i].x * 0.55f, buildings[i].z, 0.92f + 0.10f * (i % 3), buildings[i].type % 5);
        }
        if (villageW > 0.10f && i % 6 == 3) {
            drawFarmPatch(buildings[i].x * 0.58f, buildings[i].z + 3.5f, 3.4f, 7.5f);
        }
    }
    for (i = 0; i < MAX_BARRIERS; i++) {
        updateLoopedObject(&barriers[i].z, dt, -ROAD_LENGTH);
        drawBarrier(barriers[i].x, barriers[i].z);
    }

    drawDesertScene(desertW);
    drawFlowerGardenScene(flowerW);
    drawRiverScene(riverW);
    drawHillScene(hillsW);
    drawVillageRiver(villageW);
}

/* Randomize enemy car body size, color, and visual style. */
static void chooseEnemyStyle(Vehicle *e) {
    int style = rand() % 5;
    e->type = style;
    e->width = frandRange(1.30f, 1.62f);
    e->depth = frandRange(2.35f, 2.90f);
    e->height = 1.0f;
    if (style == 0) { e->r = 0.86f; e->g = 0.05f; e->b = 0.06f; }
    if (style == 1) { e->r = 0.08f; e->g = 0.18f; e->b = 0.82f; e->depth += 0.18f; }
    if (style == 2) { e->r = 0.95f; e->g = 0.68f; e->b = 0.05f; e->width += 0.10f; }
    if (style == 3) { e->r = 0.05f; e->g = 0.72f; e->b = 0.30f; }
    if (style == 4) { e->r = 0.78f; e->g = 0.08f; e->b = 0.86f; e->width -= 0.06f; }
}

/* Prevent enemy cars from spawning too close together in the same lane. */
static int laneIsSafe(int lane, float z) {
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].lane == lane) {
            if (fabsf(enemies[i].z - z) < 32.0f) return 0;
        }
    }
    return 1;
}

/* Spawn or respawn one enemy car ahead of the player. */
static void spawnEnemy(int i) {
    int attempts;
    for (attempts = 0; attempts < 8; attempts++) {
        int lane = rand() % NUM_LANES;
        float z = SPAWN_Z - frandRange(5.0f, 70.0f);
        if (laneIsSafe(lane, z)) {
            enemies[i].active = 1;
            enemies[i].lane = lane;
            enemies[i].x = laneX(lane);
            enemies[i].z = z;
            chooseEnemyStyle(&enemies[i]);
            return;
        }
    }
    enemies[i].active = 0;
}

/* Reset all gameplay values when starting or restarting the game. */
static void resetGame(void) {
    int i;
    player.x = laneX(1);
    player.z = PLAYER_Z;
    player.width = 1.42f;
    player.depth = 2.68f;
    player.height = 1.0f;
    player.r = 0.92f;
    player.g = 0.05f;
    player.b = 0.08f;
    player.lane = 1;
    player.type = 0;
    player.active = 1;
    playerTargetX = player.x;
    cameraX = player.x;

    roadOffset = 0.0f;
    dayNightTime = 0.15f;
    gameSpeed = 26.0f;
    baseSpeed = 26.0f;
    score = 0.0f;
    distanceCovered = 0.0f;
    difficultyTimer = 0.0f;
    trafficTimer = 0.0f;
    enemyLimit = 4;

    for (i = 0; i < MAX_ENEMIES; i++) enemies[i].active = 0;
    for (i = 0; i < enemyLimit; i++) spawnEnemy(i);
}

/* Build a fair collision hitbox from a vehicle's physical body size. */
static HitBox getHitBox(const Vehicle *v) {
    HitBox h;
    h.x = v->x;
    h.z = v->z;
    /* Half extents of the physical car body, kept smaller than the visual model
       so spoilers, lights, and decorative curves do not cause false crashes. */
    h.w = v->width * 0.34f;
    h.d = v->depth * 0.35f;
    return h;
}

/* Collision happens only when hitboxes overlap on both X and Z axes. */
static int collide(const Vehicle *a, const Vehicle *b) {
    HitBox x = getHitBox(a);
    HitBox y = getHitBox(b);
    float dx = fabsf(x.x - y.x);
    float dz = fabsf(x.z - y.z);
    if (!a->active || !b->active) return 0;
    return dx <= (x.w + y.w) && dz <= (x.d + y.d);
}

/*
   Main gameplay update.
   dt is frame time in seconds, so movement stays smooth on different PCs.
*/
static void updateGame(float dt) {
    int i;
    float steerSpeed = 9.4f;

    if (keyLeft)  playerTargetX -= steerSpeed * dt;
    if (keyRight) playerTargetX += steerSpeed * dt;
    if (playerTargetX < laneX(0)) playerTargetX = laneX(0);
    if (playerTargetX > laneX(2)) playerTargetX = laneX(2);

    player.x += (playerTargetX - player.x) * 8.5f * dt;
    cameraX += (player.x - cameraX) * 2.8f * dt;

    if (keyUp) baseSpeed += 18.0f * dt;
    if (keyDown) baseSpeed -= 18.0f * dt;
    if (baseSpeed < 18.0f) baseSpeed = 18.0f;
    if (baseSpeed > 72.0f) baseSpeed = 72.0f;

    difficultyTimer += dt;
    trafficTimer += dt;
    dayNightTime += dt / 55.0f;
    if (dayNightTime > 1.0f) dayNightTime -= 1.0f;
    gameSpeed = baseSpeed + difficultyTimer * 0.55f;
    if (gameSpeed > 95.0f) gameSpeed = 95.0f;

    enemyLimit = 4 + (int)(difficultyTimer / 24.0f);
    if (enemyLimit > MAX_ENEMIES) enemyLimit = MAX_ENEMIES;

    roadOffset += gameSpeed * dt;
    score += dt * (gameSpeed * 0.18f);
    distanceCovered += gameSpeed * dt * 0.20f;

    for (i = 0; i < enemyLimit; i++) {
        if (!enemies[i].active) {
            spawnEnemy(i);
            continue;
        }
        enemies[i].z += gameSpeed * dt;
        if (enemies[i].z > 30.0f) {
            score += 20.0f;
            spawnEnemy(i);
        }
        if (collide(&player, &enemies[i])) {
            gameState = STATE_GAME_OVER;
            if ((int)score > highScore) {
                highScore = (int)score;
                FILE *f = fopen("glut_highscore.dat", "w");
                if (f) { fprintf(f, "%d", highScore); fclose(f); }
            }
        }
    }
}

/* Draw subtle speed/headlight streaks on the road. */
static void drawHeadlightEffect(void) {
    float speedAlpha = clampf((gameSpeed - 32.0f) / 75.0f, 0.0f, 0.30f);
    int i;
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (i = 0; i < 12; i++) {
        float side = (i % 2 == 0) ? -1.0f : 1.0f;
        float x = side * (2.0f + (i % 5) * 0.75f);
        float z = player.z - 4.0f - i * 3.5f + fmodf(roadOffset * 0.22f, 5.0f);
        glBegin(GL_LINES);
        glColor4f(0.85f, 0.92f, 1.0f, speedAlpha);
        glVertex3f(x, 0.10f, z);
        glColor4f(0.85f, 0.92f, 1.0f, 0.0f);
        glVertex3f(x + side * 0.5f, 0.10f, z + 4.5f);
        glEnd();
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* Configure the 3D chase camera and perspective projection. */
static void setupCamera(void) {
    float speedPullback = clampf((gameSpeed - 26.0f) / 85.0f, 0.0f, 1.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(62.0 + speedPullback * 5.0, (double)WIN_W / (double)WIN_H, 0.1, 300.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(cameraX * 0.22f, 5.1f, 15.8f + speedPullback * 1.8f,
              player.x * 0.12f, 0.5f, -28.0f - speedPullback * 6.0f,
              0.0f, 1.0f, 0.0f);
}

/* Draw score, high score, speed, distance, time, and environment text. */
static void drawHUD(void) {
    char text[128];
    begin2D();
    glColor3f(1.0f, 1.0f, 1.0f);
    sprintf(text, "Score: %d", (int)score);
    drawText2D(18, WIN_H - 28, text, GLUT_BITMAP_HELVETICA_18);
    sprintf(text, "High Score: %d", highScore);
    drawText2D(18, WIN_H - 52, text, GLUT_BITMAP_HELVETICA_18);
    sprintf(text, "Speed Level: %.0f", gameSpeed);
    drawText2D(18, WIN_H - 76, text, GLUT_BITMAP_HELVETICA_18);
    sprintf(text, "Distance: %.0f m", distanceCovered);
    drawText2D(18, WIN_H - 100, text, GLUT_BITMAP_HELVETICA_18);
    sprintf(text, "Scene: %s", timeOfDayName());
    drawText2D(18, WIN_H - 124, text, GLUT_BITMAP_HELVETICA_18);
    sprintf(text, "Environment: %s", environmentName());
    drawText2D(18, WIN_H - 148, text, GLUT_BITMAP_HELVETICA_18);
    end2D();
}

/* Draw the complete 3D world for the current frame. */
static void drawScene3D(float dt) {
    int i;
    setupCamera();
    applyWorldLighting();

    setSkyColor();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    drawRoad();
    drawEnvironment(gameState == STATE_PLAYING ? dt : 0.0f);
    drawHeadlightEffect();

    drawCarModel(&player, 1);
    for (i = 0; i < enemyLimit; i++) {
        if (enemies[i].active) drawCarModel(&enemies[i], 0);
    }
}

/* Draw a translucent rectangle behind menu text. */
static void drawPanel(float x, float y, float w, float h) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.02f, 0.04f, 0.08f, 0.76f);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
    glDisable(GL_BLEND);
}

/* Draw the start menu screen. */
static void drawMenu(void) {
    begin2D();
    drawPanel(190, 160, 620, 360);
    glColor3f(1.0f, 0.86f, 0.18f);
    drawCenteredText(465, "3D CAR RACING GAME", GLUT_BITMAP_TIMES_ROMAN_24);
    glColor3f(0.95f, 0.95f, 0.95f);
    drawCenteredText(405, "Press ENTER to Start", GLUT_BITMAP_HELVETICA_18);
    drawCenteredText(372, "Press I for Instructions", GLUT_BITMAP_HELVETICA_18);
    drawCenteredText(339, "Press ESC to Exit", GLUT_BITMAP_HELVETICA_18);
    glColor3f(0.45f, 0.85f, 1.0f);
    drawCenteredText(245, "Pure C + OpenGL + GLUT", GLUT_BITMAP_HELVETICA_18);
    end2D();
}

/* Draw the instructions/help screen. */
static void drawInstructions(void) {
    begin2D();
    drawPanel(150, 110, 700, 480);
    glColor3f(1.0f, 0.86f, 0.18f);
    drawCenteredText(540, "INSTRUCTIONS", GLUT_BITMAP_TIMES_ROMAN_24);
    glColor3f(0.95f, 0.95f, 0.95f);
    drawText2D(240, 480, "Left / Right Arrow : Move car", GLUT_BITMAP_HELVETICA_18);
    drawText2D(240, 445, "Up / Down Arrow    : Increase / decrease speed", GLUT_BITMAP_HELVETICA_18);
    drawText2D(240, 410, "P                  : Pause / resume", GLUT_BITMAP_HELVETICA_18);
    drawText2D(240, 375, "Avoid traffic. Score and distance increase while driving.", GLUT_BITMAP_HELVETICA_18);
    drawText2D(240, 340, "Collision uses realistic 3D bounding boxes.", GLUT_BITMAP_HELVETICA_18);
    drawText2D(240, 275, "Press ENTER to Start", GLUT_BITMAP_HELVETICA_18);
    drawText2D(240, 240, "Press ESC to return to Menu", GLUT_BITMAP_HELVETICA_18);
    end2D();
}

/* Draw the pause overlay. */
static void drawPause(void) {
    begin2D();
    drawPanel(270, 260, 460, 160);
    glColor3f(1.0f, 0.86f, 0.18f);
    drawCenteredText(365, "PAUSED", GLUT_BITMAP_TIMES_ROMAN_24);
    glColor3f(1.0f, 1.0f, 1.0f);
    drawCenteredText(320, "Press P to Resume", GLUT_BITMAP_HELVETICA_18);
    end2D();
}

/* Draw the game over screen with final score and restart option. */
static void drawGameOver(void) {
    char text[128];
    begin2D();
    drawPanel(220, 170, 560, 340);
    glColor3f(1.0f, 0.20f, 0.15f);
    drawCenteredText(455, "GAME OVER", GLUT_BITMAP_TIMES_ROMAN_24);
    glColor3f(1.0f, 1.0f, 1.0f);
    sprintf(text, "Final Score: %d", (int)score);
    drawCenteredText(390, text, GLUT_BITMAP_HELVETICA_18);
    sprintf(text, "High Score: %d", highScore);
    drawCenteredText(360, text, GLUT_BITMAP_HELVETICA_18);
    sprintf(text, "Distance Covered: %.0f m", distanceCovered);
    drawCenteredText(330, text, GLUT_BITMAP_HELVETICA_18);
    drawCenteredText(270, "Press R to Restart", GLUT_BITMAP_HELVETICA_18);
    drawCenteredText(240, "Press ESC to Exit", GLUT_BITMAP_HELVETICA_18);
    end2D();
}

/* GLUT display callback: update game, render 3D scene, then render UI. */
static void display(void) {
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = (now - lastTime) / 1000.0f;
    if (dt > 0.05f) dt = 0.05f;
    lastTime = now;

    if (gameState == STATE_PLAYING) updateGame(dt);

    drawScene3D(dt);
    drawHUD();

    if (gameState == STATE_MENU) drawMenu();
    if (gameState == STATE_INSTRUCTIONS) drawInstructions();
    if (gameState == STATE_PAUSED) drawPause();
    if (gameState == STATE_GAME_OVER) drawGameOver();

    glutSwapBuffers();
}

/* GLUT timer callback: request a new frame roughly every 16 ms. */
static void timer(int value) {
    (void)value;
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

/* Handle normal keyboard keys such as Enter, P, I, R, and Esc. */
static void normalKey(unsigned char key, int x, int y) {
    (void)x; (void)y;
    if (key == 27) {
        if (gameState == STATE_INSTRUCTIONS) gameState = STATE_MENU;
        else exit(0);
    }
    if (key == 13) {
        if (gameState == STATE_MENU || gameState == STATE_INSTRUCTIONS) {
            resetGame();
            gameState = STATE_PLAYING;
        }
    }
    if (key == 'i' || key == 'I') {
        if (gameState == STATE_MENU) gameState = STATE_INSTRUCTIONS;
    }
    if (key == 'p' || key == 'P') {
        if (gameState == STATE_PLAYING) gameState = STATE_PAUSED;
        else if (gameState == STATE_PAUSED) gameState = STATE_PLAYING;
    }
    if (key == 'r' || key == 'R') {
        if (gameState == STATE_GAME_OVER) {
            resetGame();
            gameState = STATE_PLAYING;
        }
    }
}

/* Handle arrow-key press events. */
static void specialDown(int key, int x, int y) {
    (void)x; (void)y;
    if (key == GLUT_KEY_LEFT) keyLeft = 1;
    if (key == GLUT_KEY_RIGHT) keyRight = 1;
    if (key == GLUT_KEY_UP) keyUp = 1;
    if (key == GLUT_KEY_DOWN) keyDown = 1;
}

/* Handle arrow-key release events. */
static void specialUp(int key, int x, int y) {
    (void)x; (void)y;
    if (key == GLUT_KEY_LEFT) keyLeft = 0;
    if (key == GLUT_KEY_RIGHT) keyRight = 0;
    if (key == GLUT_KEY_UP) keyUp = 0;
    if (key == GLUT_KEY_DOWN) keyDown = 0;
}

/* Update OpenGL viewport if the window size changes. */
static void reshape(int w, int h) {
    glViewport(0, 0, w, h);
}

/* Enable depth testing, lighting, color material, and smooth shading. */
static void initOpenGL(void) {
    GLfloat lightPos[] = { 0.0f, 12.0f, 10.0f, 1.0f };
    GLfloat lightAmb[] = { 0.35f, 0.35f, 0.35f, 1.0f };
    GLfloat lightDif[] = { 0.85f, 0.85f, 0.82f, 1.0f };

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDif);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);
}

/* Read saved high score from disk if the file exists. */
static void loadHighScore(void) {
    FILE *f = fopen("glut_highscore.dat", "r");
    if (f) {
        fscanf(f, "%d", &highScore);
        fclose(f);
    }
}

/* Program entry point: initialize GLUT, register callbacks, and start loop. */
int main(int argc, char **argv) {
    srand((unsigned int)time(NULL));
    loadHighScore();

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("3D Car Racing Game - C OpenGL GLUT");

    initOpenGL();
    initEnvironmentObjects();
    resetGame();
    gameState = STATE_MENU;
    lastTime = glutGet(GLUT_ELAPSED_TIME);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(normalKey);
    glutSpecialFunc(specialDown);
    glutSpecialUpFunc(specialUp);
    glutTimerFunc(16, timer, 0);

    glutMainLoop();
    return 0;
}
