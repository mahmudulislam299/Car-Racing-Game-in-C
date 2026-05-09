/*
 =============================================================
   TURBO ROAD  -  SDL2 Car Racing Game in C
   Build: gcc car_game.c -o car_game -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf -lglu32 -lopengl32
 =============================================================
*/

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SDL_RenderClear(renderer) glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT)
#define SDL_RenderPresent(renderer) SDL_GL_SwapWindow(win)
#define SDL_DestroyRenderer(renderer) ((void)0)
#define SDL_RenderSetViewport(renderer, rect) do { \
    const SDL_Rect *_vp_rect=(const SDL_Rect *)(rect); \
    if(_vp_rect) glViewport(_vp_rect->x, WIN_H-_vp_rect->y-_vp_rect->h, _vp_rect->w, _vp_rect->h); \
    else glViewport(0,0,WIN_W,WIN_H); \
} while(0)

/* ─── Window & timing ─── */
#define WIN_W       900
#define WIN_H       650
#define FPS         60
#define FRAME_MS    (1000 / FPS)

/* ─── Road layout (wider road, narrow grass strips) ─── */
#define ROAD_LEFT    250               /* left edge of road  */
#define ROAD_RIGHT   650               /* right edge of road */
#define ROAD_W       (ROAD_RIGHT - ROAD_LEFT)   /* 400 px total road */
#define NUM_LANES    3
#define LANE_W       (ROAD_W / NUM_LANES)       /* 133 px per lane  */
#define HORIZON_Y    178
#define ROAD_NEAR_W  500
#define ROAD_FAR_W    72
#define ROAD_CURVE_SEG_LEN    30.0f
#define ROAD_CURVE_WORLD_RATE  0.165f
#define DAY_NIGHT_CYCLE_FRAMES 7200

/* ─── Car dimensions (thinner than before) ─── */
#define CAR_W        32    /* body width  – slim fit in lane */
#define CAR_H        62    /* body height                   */
#define WHEEL_EXT     6    /* wheels protrude this far sideways */
/* Full visual span = CAR_W + WHEEL_EXT*2 = 44px  (lane is 180px → lots of room) */

/* ─── Game limits ─── */
#define MAX_ENEMIES    5
#define MAX_LIVES      3
#define MAX_PARTICLES  90
#define MAX_SPEED_LINES 28

/* ─── Speed ─── */
#define INITIAL_SPEED    2
#define MAX_SPEED        9
#define SPEED_UP_EVERY  15
#define PLAYER_SPEED     7
#define INVINCIBLE_FRM  140   /* ~2.3 s */

/* ─── Minimum gap before a new enemy may enter the same lane ─── */
#define MIN_LANE_GAP  (CAR_H + 90)

/* ══════════════════════════════════════════════
   TYPES
   ══════════════════════════════════════════════ */
typedef struct { Uint8 r, g, b, a; } Color;

/* Axis-aligned bounding box */
typedef struct { int x1, y1, x2, y2; } AABB;

typedef struct {
    float x, y;
    int   speed;
    Color body;
    int   active;
    int   lane;
    int   vehicle;
} Car;

typedef struct {
    float x, y, vx, vy;
    int   life;
    Color col;
} Particle;

typedef struct {
    float x, y;
    int len, speed;
    Color col;
} SpeedLine;

/* ══════════════════════════════════════════════
   GLOBALS
   ══════════════════════════════════════════════ */
static SDL_Window   *win     = NULL;
static SDL_Renderer *ren     = NULL;
static SDL_GLContext glCtx   = NULL;
static TTF_Font     *font    = NULL;
static TTF_Font     *bigFont = NULL;

static Car      player;
static Car      enemies[MAX_ENEMIES];
static Particle sparks[MAX_PARTICLES];
static SpeedLine speedLines[MAX_SPEED_LINES];

static int   score     = 0;
static int   lives     = MAX_LIVES;
static int   gameSpeed = INITIAL_SPEED;
static float roadOff   = 0.0f;
static int   invFrames = 0;   /* countdown – player is untouchable while > 0 */
static int   running   = 1;
static int   frameNo   = 0;
static int   shakeFrames = 0;
static int   hudPulse  = 0;

static Color enemyCols[6] = {
    {220,  0,  0,255},   /* red     */
    {200,  0,200,255},   /* magenta */
    {255,120,  0,255},   /* orange  */
    {255, 80,180,255},   /* pink    */
    {100,180,  0,255},   /* lime    */
    {  0,160,220,255},   /* sky blue*/
};

static unsigned int roadHash(unsigned int x){
    x^=x>>16;
    x*=0x7feb352dU;
    x^=x>>15;
    x*=0x846ca68bU;
    x^=x>>16;
    return x;
}
static float catmull1D(float p0,float p1,float p2,float p3,float u){
    float u2=u*u, u3=u2*u;
    return 0.5f*((2.0f*p1)+(-p0+p2)*u
               +(2.0f*p0-5.0f*p1+4.0f*p2-p3)*u2
               +(-p0+3.0f*p1-3.0f*p2+p3)*u3);
}
static float roadCurveKey(int seg){
    if(seg<2) return 0.0f;
    int block=seg/2;
    unsigned int h=roadHash((unsigned int)(block+113));
    unsigned int mode=h%12U;
    float amp=1.20f + ((float)((h>>8)&255)/255.0f)*1.75f;

    if(mode<4U) return 0.0f;          /* long, calm straight sections */
    if(mode<7U) return amp;           /* right bend */
    if(mode<10U) return -amp;         /* left bend */

    /* S-turns: two neighbouring blocks lean opposite ways. */
    return (block&1) ? amp*0.86f : -amp*0.86f;
}
static float playerLanePos(void){
    return ((player.x-(float)ROAD_LEFT)/(float)ROAD_W)*6.0f-3.0f;
}
static float carLanePos(Car *c){
    return ((c->x-(float)ROAD_LEFT)/(float)ROAD_W)*6.0f-3.0f;
}
static float carDepth(Car *c){
    return -5.8f - ((float)WIN_H-c->y)*0.078f;
}
static float roadTAtZ(float z){
    float t=(-z-2.6f)/122.4f;
    if(t<0.0f) t=0.0f;
    if(t>1.0f) t=1.0f;
    return t;
}
static float roadCurveAtZ(float z){
    float t=roadTAtZ(z);
    float d=-z + roadOff*ROAD_CURVE_WORLD_RATE;
    int seg=(int)(d/ROAD_CURVE_SEG_LEN);
    float u=(d-(float)seg*ROAD_CURVE_SEG_LEN)/ROAD_CURVE_SEG_LEN;
    float curve=catmull1D(roadCurveKey(seg-1),roadCurveKey(seg),
                          roadCurveKey(seg+1),roadCurveKey(seg+2),u);
    if(curve>-0.16f && curve<0.16f) curve=0.0f;
    if(curve>4.05f) curve=4.05f;
    if(curve<-4.05f) curve=-4.05f;
    return curve*t;
}
static float roadLean(void){
    float nearCurve=roadCurveAtZ(-10.0f);
    float midCurve=roadCurveAtZ(-34.0f);
    float farCurve=roadCurveAtZ(-86.0f);
    float lean=((midCurve-nearCurve)*1.9f)+((farCurve-midCurve)*1.4f);
    if(lean>9.0f) lean=9.0f;
    if(lean<-9.0f) lean=-9.0f;
    return lean;
}
static float roadHalfAtZ(float z){
    float t=roadTAtZ(z);
    return 5.15f - 2.15f*t;
}
static float roadBankAtZ(float z){
    float front=roadCurveAtZ(z-10.0f);
    float back =roadCurveAtZ(z+8.0f);
    float bank=(front-back)*0.105f;
    if(bank>0.34f) bank=0.34f;
    if(bank<-0.34f) bank=-0.34f;
    return bank;
}
static float roadSurfaceY(float z,float lateral){
    float h=roadHalfAtZ(z);
    float side=lateral/(h>0.05f?h:0.05f);
    if(side>1.25f) side=1.25f;
    if(side<-1.25f) side=-1.25f;
    return side*roadBankAtZ(z);
}

/* ══════════════════════════════════════════════
   DRAW PRIMITIVES
   ══════════════════════════════════════════════ */
static void use2D(void){
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0,WIN_W,WIN_H,0.0,-1.0,1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}
static void use3D(void){
    float aspect=(float)WIN_W/(float)WIN_H;
    float camLook=roadCurveAtZ(-24.0f)*0.095f + roadCurveAtZ(-70.0f)*0.045f;
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-aspect*0.064,aspect*0.064,-0.030,0.116,0.1,150.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(roadLean()*1.18f,0.0f,0.0f,1.0f);
    glRotatef(18.5f,1.0f,0.0f,0.0f);
    glTranslatef(-playerLanePos()*0.16f-camLook,-2.18f,0.42f);
}
static void setCol(SDL_Renderer *r, Color c){
    (void)r;
    glColor4ub(c.r,c.g,c.b,c.a);
}
static void fillR(int x,int y,int w,int h,Color c){
    if(w<=0||h<=0) return;
    use2D(); setCol(ren,c);
    glBegin(GL_QUADS);
    glVertex2i(x,y); glVertex2i(x+w,y); glVertex2i(x+w,y+h); glVertex2i(x,y+h);
    glEnd();
}
static void outR(int x,int y,int w,int h,Color c){
    if(w<=0||h<=0) return;
    use2D(); setCol(ren,c);
    glBegin(GL_LINE_LOOP);
    glVertex2i(x,y); glVertex2i(x+w,y); glVertex2i(x+w,y+h); glVertex2i(x,y+h);
    glEnd();
}
static void blendR(int x,int y,int w,int h,Color c){
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    fillR(x,y,w,h,c);
    glDisable(GL_BLEND);
}
static Color shade(Color c,float k){
    Color o={(Uint8)(c.r*k>255?255:c.r*k),
             (Uint8)(c.g*k>255?255:c.g*k),
             (Uint8)(c.b*k>255?255:c.b*k),c.a};
    return o;
}
static void line(int x1,int y1,int x2,int y2,Color c){
    use2D(); setCol(ren,c);
    glBegin(GL_LINES);
    glVertex2i(x1,y1); glVertex2i(x2,y2);
    glEnd();
}
static void tri2D(int x1,int y1,int x2,int y2,int x3,int y3,Color c){
    use2D(); setCol(ren,c);
    glBegin(GL_TRIANGLES);
    glVertex2i(x1,y1); glVertex2i(x2,y2); glVertex2i(x3,y3);
    glEnd();
}
static void circle2D(int cx,int cy,int r,Color c){
    static const int px[17]={100,92,71,38,0,-38,-71,-92,-100,-92,-71,-38,0,38,71,92,100};
    static const int py[17]={0,38,71,92,100,92,71,38,0,-38,-71,-92,-100,-92,-71,-38,0};
    use2D(); setCol(ren,c);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2i(cx,cy);
    for(int i=0;i<17;i++) glVertex2i(cx+(px[i]*r)/100,cy+(py[i]*r)/100);
    glEnd();
}
static void blendCircle2D(int cx,int cy,int r,Color c){
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    circle2D(cx,cy,r,c);
    glDisable(GL_BLEND);
}
static void glowR(int x,int y,int w,int h,Color c);
static void renderText(const char *t,int x,int y,Color c,TTF_Font *f);
static int dayNightAmount(void);
static Color mixColor(Color day,Color night,int t);
static Color litWindowColor(Color day){
    return mixColor(day,(Color){255,205,88,255},dayNightAmount());
}
static void drawHeart2D(int x,int y,int s,Color c){
    Color shine={255,170,180,190};
    glowR(x+2,y+2,s*2,s*2,(Color){255,35,70,85});
    circle2D(x+s/2,y+s/2,s/2,c);
    circle2D(x+s+s/2,y+s/2,s/2,c);
    tri2D(x,y+s/2,x+s*2,y+s/2,x+s,y+s*2,c);
    circle2D(x+s/2-1,y+s/2-1,s/5,shine);
}
static void drawHouse2D(int x,int y,int w,int h,Color wall,Color roof){
    Color win=litWindowColor((Color){124,178,205,255});
    Color win2=litWindowColor((Color){108,168,198,255});
    Color shine=litWindowColor((Color){235,245,250,150});
    blendR(x+3,y+h,w,5,(Color){0,0,0,35});
    fillR(x+3,y+3,w,h,shade(wall,0.72f));
    fillR(x,y,w,h,wall);
    fillR(x+w-5,y+3,5,h-3,shade(wall,0.78f));
    fillR(x,y+h-4,w,4,shade(wall,0.82f));
    fillR(x+2,y+2,w-4,3,shade(wall,1.16f));
    tri2D(x-6,y,x+w/2,y-h/2,x+w+6,y,roof);
    line(x-4,y,x+w/2,y-h/2,(Color){235,210,165,210});
    line(x+w/2,y-h/2,x+w+4,y,(Color){85,54,44,210});
    line(x+3,y+3,x+w-3,y+3,shade(wall,1.25f));
    fillR(x+w/2-4,y+h-13,8,13,(Color){92,62,42,255});
    fillR(x+w/2-3,y+h-12,2,12,(Color){150,105,65,255});
    fillR(x+8,y+8,8,7,win);
    fillR(x+w-17,y+8,8,7,win);
    line(x+8,y+11,x+16,y+11,shine);
    line(x+w-17,y+11,x+w-9,y+11,shine);
    if(w>50){
        fillR(x+w/2-18,y+h-16,8,6,win2);
        line(x+w/2-18,y+h-13,x+w/2-10,y+h-13,shine);
    }
    fillR(x+w/2-7,y+h,14,2,(Color){128,114,92,255});
    fillR(x+w/2-10,y+h+2,20,2,(Color){100,94,82,230});
    fillR(x+w-10,y-h/2+4,5,12,(Color){116,76,50,255});
    blendR(x+w-8,y-h/2,18,7,(Color){235,238,232,50});
}
static void drawShop2D(int x,int y,int w,int h,Color wall){
    Color lamp=litWindowColor((Color){244,230,178,255});
    Color win=litWindowColor((Color){92,154,188,255});
    Color shine=litWindowColor((Color){238,248,250,150});
    blendR(x+3,y+h,w,5,(Color){0,0,0,35});
    fillR(x+3,y+3,w,h,shade(wall,0.72f));
    fillR(x,y,w,h,wall);
    fillR(x,y+h-5,w,5,shade(wall,0.75f));
    fillR(x,y+7,w,6,(Color){178,46,48,255});
    fillR(x+5,y+7,8,6,lamp);
    fillR(x+21,y+7,8,6,lamp);
    fillR(x+37,y+7,8,6,lamp);
    fillR(x+w-15,y+7,8,6,lamp);
    fillR(x+w/2-5,y+h-15,10,15,(Color){70,78,82,255});
    fillR(x+7,y+18,13,10,win);
    fillR(x+w-21,y+18,13,10,win);
    line(x+9,y+20,x+18,y+20,shine);
    line(x+w-19,y+20,x+w-10,y+20,shine);
    fillR(x-3,y+h,w+6,3,(Color){118,108,96,255});
    fillR(x+6,y-9,w-12,7,(Color){60,65,70,255});
    renderText("PIT",x+12,y-10,(Color){255,235,95,255},font);
}
static void drawApartment2D(int x,int y,int w,int h,Color wall,Color roof){
    Color win=litWindowColor((Color){116,174,205,255});
    Color shine=litWindowColor((Color){236,246,250,145});
    blendR(x+4,y+h,w,6,(Color){0,0,0,34});
    fillR(x+4,y+4,w,h,shade(wall,0.68f));
    fillR(x,y,w,h,wall);
    fillR(x+w-6,y+3,6,h-3,shade(wall,0.72f));
    fillR(x,y-7,w,7,roof);
    fillR(x+5,y-13,w-10,6,shade(roof,1.12f));
    int rows=(h>44)?3:2;
    for(int row=0;row<rows;row++){
        for(int col=0;col<3;col++){
            int wx=x+7+col*(w-16)/3;
            int wy=y+8+row*((h-19)/rows);
            fillR(wx,wy,8,7,win);
            line(wx,wy+3,wx+8,wy+3,shine);
        }
        fillR(x+4,y+18+row*((h-19)/rows),w-10,2,shade(wall,0.82f));
    }
    fillR(x+w/2-5,y+h-15,10,15,(Color){64,70,72,255});
    fillR(x+w/2-3,y+h-12,2,10,(Color){110,120,124,255});
    fillR(x+3,y+h-3,w-6,3,shade(wall,0.78f));
}
static void drawOffice2D(int x,int y,int w,int h,Color wall,Color trim){
    Color win=litWindowColor((Color){102,164,196,255});
    Color shine=litWindowColor((Color){236,248,250,145});
    blendR(x+5,y+h,w,7,(Color){0,0,0,36});
    fillR(x+5,y+4,w,h,shade(wall,0.64f));
    fillR(x,y,w,h,wall);
    fillR(x+w-7,y+3,7,h-3,shade(wall,0.72f));
    fillR(x-2,y-8,w+4,8,trim);
    fillR(x+4,y-13,w-8,5,shade(trim,1.14f));
    for(int row=0;row<3;row++){
        for(int col=0;col<4;col++){
            int wx=x+7+col*((w-16)/4);
            int wy=y+8+row*((h-17)/3);
            fillR(wx,wy,7,7,win);
            line(wx,wy+2,wx+7,wy+2,shine);
        }
    }
    fillR(x+w/2-6,y+h-16,12,16,(Color){55,63,68,255});
    fillR(x+w/2-12,y+h-2,24,3,shade(trim,0.85f));
}
static void drawBarn2D(int x,int y,int w,int h){
    Color loft=litWindowColor((Color){235,210,145,255});
    blendR(x+4,y+h,w,5,(Color){0,0,0,34});
    fillR(x+3,y+3,w,h,(Color){112,49,40,255});
    fillR(x,y,w,h,(Color){166,72,54,255});
    tri2D(x-5,y,x+w/2,y-h/2,x+w+5,y,(Color){96,70,58,255});
    line(x-3,y,x+w/2,y-h/2,(Color){205,160,118,210});
    fillR(x+w/2-7,y+h-16,14,16,(Color){92,52,38,255});
    line(x+w/2-7,y+h-16,x+w/2+7,y+h,(Color){210,166,120,255});
    line(x+w/2+7,y+h-16,x+w/2-7,y+h,(Color){210,166,120,255});
    fillR(x+8,y+8,8,6,loft);
}
static void drawTree2D(int x,int y,int s){
    fillR(x-s/8,y+s/2,s/4,s,(Color){104,70,42,255});
    fillR(x-s/14,y+s/3,s/7,s,(Color){137,92,52,255});
    circle2D(x,y+s/3,s/2,(Color){31,105,54,255});
    circle2D(x-s/3,y+s/2,s/3,(Color){23,86,46,255});
    circle2D(x+s/3,y+s/2,s/3,(Color){44,130,66,255});
    circle2D(x,y+s/8,s/3,(Color){53,143,74,255});
}

static void drawObservatory2D(int x,int y,int s){
    Color wall={188,198,190,255}, side={128,148,152,255};
    Color dome={104,170,198,255}, glass={150,226,240,220};
    blendR(x-10,y+s+6,s+70,8,(Color){0,0,0,38});
    fillR(x,y+s/3,s+44,s,wall);
    fillR(x+s+28,y+s/3+5,16,s-5,side);
    fillR(x+6,y+s/3+7,s+28,4,shade(wall,1.18f));
    circle2D(x+s/2+22,y+s/3,34,dome);
    fillR(x-2,y+s/3,s+50,34,dome);
    blendR(x+10,y+s/3+5,s+22,10,glass);
    fillR(x+s/2+15,y+s/3-28,7,28,(Color){90,112,118,255});
    line(x+s/2+18,y+s/3-30,x+s/2+52,y+s/3-45,(Color){230,244,245,230});
    line(x+s/2+18,y+s/3-29,x+s/2+55,y+s/3-39,(Color){72,90,96,230});
    fillR(x+12,y+s/3+s-28,11,28,(Color){82,72,62,255});
    for(int i=0;i<3;i++){
        fillR(x+34+i*18,y+s/3+48,10,11,(Color){102,170,202,255});
        line(x+34+i*18,y+s/3+53,x+44+i*18,y+s/3+53,(Color){232,248,250,155});
    }
}

static void drawHillWindTurbine2D(int x,int y,int s,int phase){
    Color mast={218,224,214,255}, shadow={122,138,132,210}, blade={246,246,232,245};
    fillR(x-2,y-s,4,s,(Color){174,184,178,255});
    fillR(x-1,y-s,2,s,mast);
    circle2D(x,y-s,5,(Color){238,238,222,255});
    int p=(phase/18)%3;
    if(p==0){
        line(x,y-s,x,y-s-28,blade);
        line(x,y-s,x-24,y-s+15,blade);
        line(x,y-s,x+24,y-s+15,blade);
    }else if(p==1){
        line(x,y-s,x+25,y-s-16,blade);
        line(x,y-s,x+1,y-s+28,blade);
        line(x,y-s,x-26,y-s-12,blade);
    }else{
        line(x,y-s,x-25,y-s-17,blade);
        line(x,y-s,x+26,y-s-12,blade);
        line(x,y-s,x-2,y-s+28,blade);
    }
    line(x-4,y,x+8,y,shadow);
}

static void drawGreenhouse2D(int x,int y,int w,int h){
    Color frame={80,112,108,255}, glass={145,224,214,110};
    blendR(x+2,y+h,w,6,(Color){0,0,0,34});
    fillR(x,y+12,w,h-12,(Color){98,148,126,170});
    tri2D(x,y+12,x+w/2,y-8,x+w,y+12,glass);
    blendR(x+3,y+14,w-6,h-17,glass);
    line(x,y+12,x+w/2,y-8,frame);
    line(x+w/2,y-8,x+w,y+12,frame);
    line(x+w/2,y-8,x+w/2,y+h,frame);
    for(int i=1;i<4;i++){
        int px=x+(w*i)/4;
        line(px,y+7,px,y+h,frame);
    }
    fillR(x-4,y+h-3,w+8,5,(Color){78,88,70,255});
}

static void drawSolarArray2D(int x,int y,int count){
    for(int i=0;i<count;i++){
        int px=x+i*27;
        blendR(px,y+18,22,3,(Color){0,0,0,34});
        fillR(px,y,22,14,(Color){28,60,82,255});
        fillR(px+2,y+2,18,10,(Color){54,126,160,255});
        line(px+11,y,px+11,y+14,(Color){178,224,230,130});
        line(px,y+7,px+22,y+7,(Color){178,224,230,120});
        fillR(px+10,y+14,3,7,(Color){76,70,58,255});
    }
}

static void glowR(int x,int y,int w,int h,Color c){
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for(int i=3;i>0;i--){
        Color g=c; g.a=(Uint8)(c.a/(i+1));
        fillR(x-i*3,y-i*3,w+i*6,h+i*6,g);
    }
    glDisable(GL_BLEND);
}
static int pulse(int frame,int period,int amp){
    int p=frame%period;
    if(p>period/2) p=period-p;
    return (p*amp*2)/period;
}
static Uint8 mixByte(Uint8 a,Uint8 b,int t){
    if(t<0) t=0;
    if(t>1000) t=1000;
    return (Uint8)(((int)a*(1000-t)+(int)b*t)/1000);
}
static Color mixColor(Color day,Color night,int t){
    Color c={mixByte(day.r,night.r,t),mixByte(day.g,night.g,t),
             mixByte(day.b,night.b,t),mixByte(day.a,night.a,t)};
    return c;
}
static int dayNightAmount(void){
    int p=frameNo%DAY_NIGHT_CYCLE_FRAMES;
    if(p<2100) return 0;
    if(p<3300) return ((p-2100)*1000)/1200;
    if(p<5700) return 1000;
    return ((DAY_NIGHT_CYCLE_FRAMES-p)*1000)/1500;
}
static int roadWidthAt(int y){
    if(y<=HORIZON_Y) return ROAD_FAR_W;
    int dy=y-HORIZON_Y;
    int range=WIN_H-HORIZON_Y;
    int t=(dy*1000)/range;
    return ROAD_FAR_W + ((ROAD_NEAR_W-ROAD_FAR_W)*t*t)/1000000;
}
static int roadCenterAt(int y){
    int dy=y-HORIZON_Y;
    int curve=(dy*dy)/1050;
    return WIN_W/2 + curve;
}
static int roadLeftAt(int y){
    return roadCenterAt(y)-roadWidthAt(y)/2;
}
static int roadRightAt(int y){
    return roadCenterAt(y)+roadWidthAt(y)/2;
}
static int laneMarkerXAt(int y,int marker){
    int left=roadLeftAt(y), w=roadWidthAt(y);
    return left + (w*marker)/NUM_LANES;
}

/* ══════════════════════════════════════════════
   TEXT HELPERS
   ══════════════════════════════════════════════ */
static void renderText(const char *t,int x,int y,Color c,TTF_Font *f){
    if(!f) return;
    SDL_Color sc={c.r,c.g,c.b,c.a};
    SDL_Surface *s=TTF_RenderText_Blended(f,t,sc); if(!s) return;
    SDL_Surface *rgba=SDL_ConvertSurfaceFormat(s,SDL_PIXELFORMAT_ABGR8888,0);
    SDL_FreeSurface(s);
    if(!rgba) return;

    GLuint tex=0;
    glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,rgba->w,rgba->h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba->pixels);

    use2D();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glColor4ub(255,255,255,255);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2i(x,y);
    glTexCoord2f(1,0); glVertex2i(x+rgba->w,y);
    glTexCoord2f(1,1); glVertex2i(x+rgba->w,y+rgba->h);
    glTexCoord2f(0,1); glVertex2i(x,y+rgba->h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDeleteTextures(1,&tex);
    SDL_FreeSurface(rgba);
}
static void renderCentered(const char *t,int y,Color c,TTF_Font *f){
    if(!f) return; int w,h; TTF_SizeText(f,t,&w,&h);
    renderText(t,(WIN_W-w)/2,y,c,f);
}

/* ══════════════════════════════════════════════
   getCarAABB()
   Exact bounding box INCLUDING the side wheels.
   This is the ONLY box used for collision – if a
   wheel touches anything, it counts as a hit.
   ══════════════════════════════════════════════ */
static AABB getCarAABB(Car *c){
    return (AABB){
        (int)c->x - WHEEL_EXT,          /* leftmost wheel pixel  */
        (int)c->y + 8,                   /* top of body           */
        (int)c->x + CAR_W + WHEEL_EXT,  /* rightmost wheel pixel */
        (int)c->y + CAR_H - 4           /* bottom of body        */
    };
}

/* ══════════════════════════════════════════════
   aabbOverlap()
   Returns 1 when two AABBs intersect.
   A 1 px shrink prevents triggers on exact edges.
   ══════════════════════════════════════════════ */
static int aabbOverlap(AABB a,AABB b){
    return !(a.x2-1<b.x1+1 || a.x1+1>b.x2-1 ||
             a.y2-1<b.y1+1 || a.y1+1>b.y2-1);
}

/* ══════════════════════════════════════════════
   laneX() – left pixel edge of a lane's car slot
   Each car is centred inside its 180 px lane.
   ══════════════════════════════════════════════ */
static int laneX(int lane){
    return ROAD_LEFT + lane * LANE_W + (LANE_W - CAR_W) / 2;
}

/* ══════════════════════════════════════════════
   laneOccupied()
   True when any active enemy in 'lane' is still
   within MIN_LANE_GAP of the top of the screen,
   preventing cars from spawning on top of each other.
   ══════════════════════════════════════════════ */
static int laneOccupied(int lane){
    for(int i=0;i<MAX_ENEMIES;i++){
        if(enemies[i].active && enemies[i].lane==lane
           && enemies[i].y < (float)MIN_LANE_GAP)
            return 1;
    }
    return 0;
}

/* ══════════════════════════════════════════════
   spawnEnemy()
   Picks a free lane (random order) and places
   a new car above the screen.  Stays inactive
   if all lanes are blocked – retried next frame.
   ══════════════════════════════════════════════ */
static void spawnEnemy(int idx){
    int order[3]={0,1,2};
    for(int i=2;i>0;i--){           /* Fisher-Yates shuffle */
        int j=rand()%(i+1), tmp=order[i]; order[i]=order[j]; order[j]=tmp;
    }
    for(int t=0;t<3;t++){
        int lane=order[t];
        if(!laneOccupied(lane)){
            enemies[idx].x      =(float)laneX(lane);
            enemies[idx].y      =(float)(-CAR_H-10-rand()%80);
            enemies[idx].speed  = gameSpeed+rand()%2;
            enemies[idx].body   = enemyCols[rand()%6];
            enemies[idx].active = 1;
            enemies[idx].lane   = lane;
            enemies[idx].vehicle= 1+rand()%4;
            return;
        }
    }
    enemies[idx].active=0; /* all lanes busy – retry next frame */
}

/* ══════════════════════════════════════════════
   updateEnemies()
   Move each enemy down.  Respawn when off-screen.
   Award 1 point and optionally increase speed.
   ══════════════════════════════════════════════ */
static void updateEnemies(void){
    for(int i=0;i<MAX_ENEMIES;i++){
        if(!enemies[i].active){ spawnEnemy(i); continue; }
        enemies[i].y+=(float)enemies[i].speed;
        if(enemies[i].y > WIN_H+CAR_H){
            score++;
            if(score%SPEED_UP_EVERY==0 && gameSpeed<MAX_SPEED){
                gameSpeed++;
                hudPulse=24;
            }
            spawnEnemy(i);
        }
    }
}

/* ══════════════════════════════════════════════
   PARTICLES (explosion sparks on collision)
   ══════════════════════════════════════════════ */
static void spawnExplosion(int cx,int cy){
    Color cols[4]={{255,220,0,255},{255,80,0,255},{255,30,30,255},{255,255,255,255}};
    for(int i=0;i<MAX_PARTICLES;i++){
        sparks[i].x=(float)cx; sparks[i].y=(float)cy;
        sparks[i].vx=((rand()%240)-120)/26.0f;
        sparks[i].vy=((rand()%240)-120)/26.0f;
        sparks[i].life=20+rand()%20;
        sparks[i].col=cols[rand()%4];
    }
}
static void spawnExhaust(void){
    for(int n=0;n<3;n++){
        for(int i=0;i<MAX_PARTICLES;i++){
            if(sparks[i].life<=0){
                sparks[i].x=player.x+CAR_W/2-5+(float)(rand()%11);
                sparks[i].y=player.y+CAR_H-4+(float)(rand()%6);
                sparks[i].vx=((rand()%80)-40)/55.0f;
                sparks[i].vy=1.2f+(rand()%70)/45.0f;
                sparks[i].life=12+rand()%14;
                sparks[i].col=(Color){120,210,255,125};
                break;
            }
        }
    }
}
static void updateParticles(void){
    for(int i=0;i<MAX_PARTICLES;i++){
        if(sparks[i].life<=0) continue;
        sparks[i].x+=sparks[i].vx; sparks[i].y+=sparks[i].vy;
        sparks[i].vy+=0.20f; sparks[i].life--;
    }
}
static void drawParticles(void){
    for(int i=0;i<MAX_PARTICLES;i++){
        if(sparks[i].life<=0) continue;
        int sz=2+sparks[i].life/7;
        fillR((int)sparks[i].x,(int)sparks[i].y,sz,sz,sparks[i].col);
    }
}
static void resetSpeedLines(void){
    for(int i=0;i<MAX_SPEED_LINES;i++){
        int side=rand()%2;
        speedLines[i].x=(float)(side ? ROAD_RIGHT+18+rand()%140 : rand()%(ROAD_LEFT-18));
        speedLines[i].y=(float)(rand()%WIN_H);
        speedLines[i].len=18+rand()%45;
        speedLines[i].speed=7+rand()%9;
        speedLines[i].col=(Color){180,240,255,(Uint8)(60+rand()%90)};
    }
}
static void updateSpeedLines(void){
    for(int i=0;i<MAX_SPEED_LINES;i++){
        speedLines[i].y+=(float)(speedLines[i].speed+gameSpeed*2);
        if(speedLines[i].y>WIN_H+speedLines[i].len){
            int side=rand()%2;
            speedLines[i].x=(float)(side ? ROAD_RIGHT+18+rand()%140 : rand()%(ROAD_LEFT-18));
            speedLines[i].y=(float)(-rand()%160);
            speedLines[i].len=18+rand()%45+gameSpeed*3;
            speedLines[i].speed=7+rand()%9;
        }
    }
}
static void drawSpeedLines(void){
    if(gameSpeed<4) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for(int i=0;i<MAX_SPEED_LINES;i++){
        line((int)speedLines[i].x,(int)speedLines[i].y,
             (int)speedLines[i].x,(int)speedLines[i].y+speedLines[i].len,
             speedLines[i].col);
    }
    glDisable(GL_BLEND);
}

/* ══════════════════════════════════════════════
   drawBackground()
   Sky, wide grass strips, road shoulders, trees
   ══════════════════════════════════════════════ */
static void drawBackground(void){
    int cloudShift=(int)(roadOff*0.04f)%WIN_W;
    int hillShift=(int)(roadOff*0.015f)%WIN_W;
    int turnShift=(int)(roadCurveAtZ(-84.0f)*24.0f);
    int nearShift=(int)(roadCurveAtZ(-42.0f)*14.0f);
    int night=dayNightAmount();

    /* Gradual day/night sky */
    for(int y=0;y<WIN_H;y+=2){
        Uint8 dr=(Uint8)(76 + y/9);
        Uint8 dg=(Uint8)(145 + y/11);
        Uint8 db=(Uint8)(222 - y/38);
        Uint8 nr=(Uint8)(8 + y/70);
        Uint8 ng=(Uint8)(18 + y/62);
        Uint8 nb=(Uint8)(48 + y/25);
        Uint8 r=mixByte(dr,nr,night);
        Uint8 g=mixByte(dg,ng,night);
        Uint8 b=mixByte(db,nb,night);
        fillR(0,y,WIN_W,2,(Color){r,g,b,255});
    }
    for(int y=HORIZON_Y-34;y<HORIZON_Y+18;y+=2){
        Uint8 a=(Uint8)((60-((y-(HORIZON_Y-34))*40)/52)*(1000-night)/1000);
        blendR(0,y,WIN_W,2,(Color){255,214,150,a});
    }

    if(night<780){
        Uint8 a=(Uint8)(230*(1000-night)/1000);
        blendCircle2D(105+turnShift/5,72,38,(Color){255,221,135,(Uint8)(a/2)});
        blendCircle2D(105+turnShift/5,72,23,(Color){255,239,190,a});
        blendCircle2D(105+turnShift/5,72,12,(Color){255,250,216,a});
    }
    if(night>180){
        Uint8 a=(Uint8)((night-180)*255/820);
        blendCircle2D(WIN_W-130+turnShift/8,58,23,(Color){214,226,238,a});
        blendCircle2D(WIN_W-121+turnShift/8,51,19,(Color){20,30,58,(Uint8)(a>210?210:a)});
        for(int i=0;i<22;i++){
            int sx=(i*97+37)%WIN_W;
            int sy=22+(i*43)%118;
            Uint8 sa=(Uint8)((95+(i%4)*35)*(night-180)/820);
            blendR(sx,sy,2,2,(Color){230,238,255,sa});
        }
    }

    /* Very slow cloud banks with rounded tops and soft gray undersides */
    for(int i=0;i<4;i++){
        int cx=(i*230-cloudShift+turnShift/3+WIN_W*2)%WIN_W;
        int cy=34+(i%2)*34;
        Color cloud=mixColor((Color){255,255,255,100},(Color){72,84,112,70},night);
        Color cloudLow=mixColor((Color){190,205,216,42},(Color){36,46,72,46},night);
        blendR(cx+8,cy+16,118,10,cloudLow);
        blendCircle2D(cx+18,cy+11,17,cloud);
        blendCircle2D(cx+42,cy+4,23,cloud);
        blendCircle2D(cx+72,cy+8,20,cloud);
        blendCircle2D(cx+99,cy+14,15,cloud);
        blendR(cx+18,cy+19,98,8,cloudLow);
    }

    /* Layered mountain ridges: varied shapes, richer color, and soft base haze */
    Color farA=mixColor((Color){106,126,158,245},(Color){24,36,70,245},night);
    Color farB=mixColor((Color){83,112,148,245},(Color){18,30,62,245},night);
    Color snow=mixColor((Color){226,236,232,210},(Color){116,134,162,170},night);
    for(int x=-260-hillShift+turnShift;x<WIN_W+300;x+=210){
        int peak=62+((x+31)&24);
        tri2D(x,HORIZON_Y+30,x+112,peak,x+245,HORIZON_Y+30,farA);
        tri2D(x+72,HORIZON_Y+30,x+166,peak+18,x+302,HORIZON_Y+30,farB);
        tri2D(x+92,peak+18,x+112,peak,x+136,peak+20,snow);
        tri2D(x+148,peak+35,x+166,peak+18,x+188,peak+36,shade(snow,0.82f));
    }

    Color midA=mixColor((Color){64,132,116,255},(Color){14,54,64,255},night);
    Color midB=mixColor((Color){48,111,102,255},(Color){10,42,54,255},night);
    for(int x=-170-(hillShift/2)+turnShift;x<WIN_W+230;x+=180){
        int peak=102+((x+17)&18);
        tri2D(x,HORIZON_Y+52,x+98,peak,x+218,HORIZON_Y+52,midA);
        tri2D(x+74,HORIZON_Y+52,x+160,peak+14,x+286,HORIZON_Y+52,midB);
        line(x+98,peak,x+134,HORIZON_Y+50,(Color){146,188,168,88});
        line(x+160,peak+14,x+196,HORIZON_Y+50,(Color){34,74,70,120});
    }

    Color nearA=mixColor((Color){34,112,70,255},(Color){6,42,38,255},night);
    Color nearB=mixColor((Color){25,92,62,255},(Color){4,32,34,255},night);
    for(int x=-90-(hillShift/3)+nearShift/2;x<WIN_W+150;x+=150){
        tri2D(x,HORIZON_Y+66,x+78,HORIZON_Y+28+((x+9)&16),x+188,HORIZON_Y+66,nearA);
        tri2D(x+70,HORIZON_Y+66,x+138,HORIZON_Y+38+((x+21)&12),x+246,HORIZON_Y+66,nearB);
    }
    blendR(0,HORIZON_Y+42,WIN_W,34,mixColor((Color){185,220,184,44},(Color){22,42,58,48},night));

    /* Field with subtle bands, not scrolling clutter */
    fillR(0,HORIZON_Y+18,WIN_W,WIN_H-HORIZON_Y-18,
          mixColor((Color){31,128,59,255},(Color){8,54,42,255},night));
    blendR(0,HORIZON_Y+40,WIN_W,22,
           mixColor((Color){90,168,74,95},(Color){18,82,66,70},night));
    blendR(0,HORIZON_Y+82,WIN_W,18,
           mixColor((Color){21,101,48,80},(Color){4,38,34,80},night));

    /* Hillfront landmark: observatory park, turbines, greenhouse, and solar field */
    int landmarkX=70-(hillShift/5)+turnShift;
    blendR(landmarkX-28,HORIZON_Y+63,760,15,(Color){16,68,45,62});
    blendR(landmarkX+90,HORIZON_Y+23,128,32,(Color){255,240,180,28});
    drawHillWindTurbine2D(landmarkX+28, HORIZON_Y+65,72,frameNo+10);
    drawHillWindTurbine2D(landmarkX+650,HORIZON_Y+68,66,frameNo+34);
    drawObservatory2D(landmarkX+112,HORIZON_Y+14,58);
    drawGreenhouse2D(landmarkX+310,HORIZON_Y+43,92,42);
    drawSolarArray2D(landmarkX+430,HORIZON_Y+65,5);
    drawHouse2D(landmarkX+560,HORIZON_Y+52,46,28,(Color){190,178,136,255},(Color){80,88,86,255});
    drawShop2D(landmarkX+612,HORIZON_Y+47,58,32,(Color){188,164,126,255});
    fillR(landmarkX+82,HORIZON_Y+75,575,5,(Color){72,76,66,230});
    for(int i=0;i<7;i++){
        int px=landmarkX+102+i*82;
        fillR(px,HORIZON_Y+54,4,24,(Color){78,82,72,255});
        circle2D(px+2,HORIZON_Y+51,4,(Color){255,222,112,180});
    }
    tri2D(landmarkX+258,HORIZON_Y+38,landmarkX+258,HORIZON_Y+66,landmarkX+296,HORIZON_Y+52,(Color){96,220,190,255});
    fillR(landmarkX+255,HORIZON_Y+38,3,34,(Color){64,80,82,255});

    /* Fence line and tree clusters, slow enough not to distract */
    int fenceShift=(int)(roadOff*0.12f)%80;
    for(int x=-80-fenceShift+nearShift;x<WIN_W+80;x+=80){
        fillR(x,HORIZON_Y+86,54,3,(Color){196,176,128,230});
        fillR(x+8,HORIZON_Y+75,4,22,(Color){148,106,67,255});
        fillR(x+44,HORIZON_Y+75,4,22,(Color){148,106,67,255});
    }
    for(int i=0;i<7;i++){
        int tx=(i*118-((int)(roadOff*0.10f)%130)+nearShift+WIN_W)%WIN_W;
        int ty=HORIZON_Y+54+(i%2)*22;
        drawTree2D(tx,ty,34+(i%2)*6);
    }

    Color oldFlowers[3]={{240,214,90,255},{230,120,150,255},{196,224,235,255}};
    for(int i=0;i<14;i++){
        int fy=(int)(HORIZON_Y+72+i*37+roadOff*0.45f)%WIN_H;
        if(fy<HORIZON_Y) fy+=HORIZON_Y;
        int fx=(i%2)?(18+(i*37)%150):(530+(i*29)%145);
        fillR(fx,fy,3,3,oldFlowers[i%3]);
    }
    return;
#if 0
    /* Sky gradient */
    for(int y=0;y<WIN_H;y+=2){
        Uint8 r=(Uint8)(72+y/7), g=(Uint8)(150+y/9), b=(Uint8)(215+y/18);
        fillR(0,y,WIN_W,2,(Color){r,g,b,255});
    }

    Color hill1={60,128,105,255}, hill2={42,108,82,255};
    for(int x=-40;x<WIN_W;x+=80){
        int wave=(x+frameNo/2)%70; if(wave<0) wave+=70;
        int h=38+wave;
        fillR(x,118-h,80,h,hill1);
        fillR(x+20,135-h/2,70,h/2,hill2);
    }

    for(int i=0;i<5;i++){
        int cx=(i*165-(frameNo/3)%180+WIN_W)%WIN_W;
        int cy=22+i*17;
        blendR(cx,cy,58,10,(Color){255,255,255,95});
        blendR(cx+22,cy-7,42,10,(Color){255,255,255,75});
    }

    /* Grass strips (narrower now – road is wider) */
    Color gr={25,130,55,255};
    fillR(0,HORIZON_Y,WIN_W,WIN_H-HORIZON_Y,gr);

    for(int y=(int)(roadOff*0.55f)%42-42;y<WIN_H;y+=42){
        if(y>HORIZON_Y){
            fillR(0,y,WIN_W,5,(Color){37,154,64,255});
            fillR(0,y+18,WIN_W,3,(Color){18,110,45,255});
        }
    }

    for(int y=HORIZON_Y+(int)(roadOff*1.5f)%82-82;y<WIN_H;y+=82){
        if(y<HORIZON_Y+12) continue;
        int scale=2+((y-HORIZON_Y)*8)/(WIN_H-HORIZON_Y);
        int lx=roadLeftAt(y)-20-scale;
        int rx=roadRightAt(y)+12;
        fillR(lx,y,scale,scale*5,(Color){245,245,235,255});
        fillR(lx,y+scale*3,scale,scale*2,(Color){220,40,35,255});
        fillR(rx,y+38,scale,scale*5,(Color){245,245,235,255});
        fillR(rx,y+38+scale*3,scale,scale*2,(Color){220,40,35,255});
    }

    /* Scrolling trees (parallax – slower than road) */
    Color trunk={101,67,33,255},leaf={20,120,20,255},leafD={15,90,15,255};
    int baseY[4]={20,145,280,410};
    for(int i=0;i<4;i++){
        /* Left tree */
        int ty=(int)(baseY[i]+roadOff*0.4f)%WIN_H; if(ty<0)ty+=WIN_H;
        fillR(38,ty+28,10,26,trunk);
        fillR(20,ty+ 4,46,28,leaf);
        fillR(12,ty+14,62,18,leafD);
        fillR(20,ty+28,46,12,leafD);
        /* Right tree (staggered) */
        int ty2=(int)(baseY[i]+80+roadOff*0.4f)%WIN_H; if(ty2<0)ty2+=WIN_H;
        int tx=ROAD_RIGHT+22;
        fillR(tx,    ty2+28,10,26,trunk);
        fillR(tx-18, ty2+ 4,46,28,leaf);
        fillR(tx-26, ty2+14,62,18,leafD);
        fillR(tx-18, ty2+28,46,12,leafD);
    }

    Color flowers[3]={{255,225,80,255},{255,80,150,255},{190,235,255,255}};
    for(int i=0;i<18;i++){
        int fy=(int)(i*53+roadOff*0.75f)%WIN_H;
        int fx=(i%2)?(20+(i*29)%(ROAD_LEFT-42)):(ROAD_RIGHT+22+(i*31)%(WIN_W-ROAD_RIGHT-45));
        fillR(fx,fy,3,3,flowers[i%3]);
    }
#endif
}

/* ══════════════════════════════════════════════
   drawRoad()
   Road surface, yellow kerbs, scrolling dashes
   ══════════════════════════════════════════════ */
static void box3D(float x,float y,float z,float sx,float sy,float sz,Color c);
static void drawRoadsideWater3D(void);
static void drawRoadsideModels3D(void);
static void drawRoad(void){
    use3D();
    int night=dayNightAmount();

    /* Ground plane */
    Color groundNear=mixColor((Color){18,95,45,255},(Color){5,42,36,255},night);
    Color groundFar =mixColor((Color){8,55,35,255},(Color){3,24,30,255},night);
    glBegin(GL_QUADS);
    glColor3ub(groundNear.r,groundNear.g,groundNear.b);
    glVertex3f(-80.0f,-0.08f,-2.6f);
    glVertex3f( 80.0f,-0.08f,-2.6f);
    glColor3ub(groundFar.r,groundFar.g,groundFar.b);
    glVertex3f( 80.0f,-0.08f,-125.0f);
    glVertex3f(-80.0f,-0.08f,-125.0f);
    glEnd();

    drawRoadsideWater3D();
    drawRoadsideModels3D();

    /* Actual 3D asphalt made from curved slices */
    for(int s=0;s<50;s++){
        float z1=-2.6f-(float)s*2.45f;
        float z2=z1-2.45f;
        float c1=roadCurveAtZ(z1), c2=roadCurveAtZ(z2);
        float h1=roadHalfAtZ(z1), h2=roadHalfAtZ(z2);
        Uint8 shadeV=(Uint8)(62-(s>34?20:s/2));
        shadeV=mixByte(shadeV,(Uint8)(shadeV*58/100),night);
        glBegin(GL_QUADS);
        glColor3ub(shadeV,shadeV,shadeV+4);
        glVertex3f(c1-h1,roadSurfaceY(z1,-h1),z1);
        glVertex3f(c1+h1,roadSurfaceY(z1, h1),z1);
        glColor3ub(mixByte(30,16,night),mixByte(32,18,night),mixByte(36,26,night));
        glVertex3f(c2+h2,roadSurfaceY(z2, h2),z2);
        glVertex3f(c2-h2,roadSurfaceY(z2,-h2),z2);
        glEnd();
    }

    /* Subtle asphalt grain and tire paths */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for(int i=0;i<34;i++){
        float z=-5.0f-(float)((i*11+(int)(roadOff*0.22f))%118);
        float lane=((i*37)%100)/100.0f;
        float x=roadCurveAtZ(z)-3.55f+lane*7.1f;
        float y=roadSurfaceY(z,x-roadCurveAtZ(z))+0.052f;
        float w=0.10f+(float)(i%5)*0.025f;
        glBegin(GL_QUADS);
        glColor4ub(255,255,255,(GLubyte)(13+(i%4)*5));
        glVertex3f(x,y,z); glVertex3f(x+w,y,z);
        glVertex3f(x+w,y,z-0.42f); glVertex3f(x,y,z-0.42f);
        glEnd();
    }
    for(int side=-1;side<=1;side+=2){
        float hNear=roadHalfAtZ(-3.2f), hFar=roadHalfAtZ(-124.0f);
        glBegin(GL_QUADS);
        glColor4ub(0,0,0,45);
        glVertex3f(roadCurveAtZ(-3.2f)+side*hNear*0.33f,roadSurfaceY(-3.2f,side*hNear*0.33f)+0.053f,-3.2f);
        glVertex3f(roadCurveAtZ(-3.2f)+side*hNear*0.39f,roadSurfaceY(-3.2f,side*hNear*0.39f)+0.053f,-3.2f);
        glColor4ub(0,0,0,12);
        glVertex3f(roadCurveAtZ(-124.0f)+side*hFar*0.44f,roadSurfaceY(-124.0f,side*hFar*0.44f)+0.053f,-124.0f);
        glVertex3f(roadCurveAtZ(-124.0f)+side*hFar*0.37f,roadSurfaceY(-124.0f,side*hFar*0.37f)+0.053f,-124.0f);
        glEnd();
    }
    glDisable(GL_BLEND);

    /* Shoulders follow the same curve as the road */
    for(int s=0;s<50;s++){
        float z1=-2.6f-(float)s*2.45f, z2=z1-2.45f;
        float c1=roadCurveAtZ(z1), c2=roadCurveAtZ(z2);
        float h1=roadHalfAtZ(z1), h2=roadHalfAtZ(z2);
        glBegin(GL_QUADS);
        glColor3ub(115,105,88);
        glVertex3f(c1-h1-1.15f,roadSurfaceY(z1,-h1-1.15f)+0.01f,z1); glVertex3f(c1-h1,roadSurfaceY(z1,-h1)+0.01f,z1);
        glColor3ub(72,70,62);
        glVertex3f(c2-h2,roadSurfaceY(z2,-h2)+0.01f,z2); glVertex3f(c2-h2-1.0f,roadSurfaceY(z2,-h2-1.0f)+0.01f,z2);
        glColor3ub(115,105,88);
        glVertex3f(c1+h1,roadSurfaceY(z1,h1)+0.01f,z1); glVertex3f(c1+h1+1.15f,roadSurfaceY(z1,h1+1.15f)+0.01f,z1);
        glColor3ub(72,70,62);
        glVertex3f(c2+h2+1.0f,roadSurfaceY(z2,h2+1.0f)+0.01f,z2); glVertex3f(c2+h2,roadSurfaceY(z2,h2)+0.01f,z2);
        glEnd();
    }

    /* Road edge lines */
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glColor3ub(255,218,48);
    for(int s=0;s<50;s++){
        float z1=-2.6f-(float)s*2.45f, z2=z1-2.45f;
        float h1=roadHalfAtZ(z1)-0.25f, h2=roadHalfAtZ(z2)-0.25f;
        glVertex3f(roadCurveAtZ(z1)-h1,roadSurfaceY(z1,-h1)+0.045f,z1);
        glVertex3f(roadCurveAtZ(z2)-h2,roadSurfaceY(z2,-h2)+0.045f,z2);
        glVertex3f(roadCurveAtZ(z1)+h1,roadSurfaceY(z1, h1)+0.045f,z1);
        glVertex3f(roadCurveAtZ(z2)+h2,roadSurfaceY(z2, h2)+0.045f,z2);
    }
    glEnd();

    /* Low guardrail glints near the useful driving area */
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3ub(180,188,186);
    glVertex3f(roadCurveAtZ(-5.0f)-roadHalfAtZ(-5.0f)-0.80f,0.34f,-5.0f); glVertex3f(roadCurveAtZ(-54.0f)-roadHalfAtZ(-54.0f)-0.70f,0.23f,-54.0f);
    glVertex3f(roadCurveAtZ(-5.0f)+roadHalfAtZ(-5.0f)+0.80f,0.34f,-5.0f); glVertex3f(roadCurveAtZ(-54.0f)+roadHalfAtZ(-54.0f)+0.70f,0.23f,-54.0f);
    glEnd();
    for(float z=-7.0f;z>-54.0f;z-=8.0f){
        float h=roadHalfAtZ(z)+0.68f;
        box3D(roadCurveAtZ(z)-h,0.02f,z,0.08f,0.42f,0.08f,(Color){125,130,125,255});
        box3D(roadCurveAtZ(z)+h,0.02f,z,0.08f,0.42f,0.08f,(Color){125,130,125,255});
    }

    /* Lane divider dashes moving in world space */
    float off=(float)((int)(roadOff*0.10f)%12);
    for(float z=-4.0f+off;z>-122.0f;z-=12.0f){
        for(int lane=1;lane<NUM_LANES;lane++){
            float h=roadHalfAtZ(z)-0.35f, h2=roadHalfAtZ(z-5.2f)-0.35f;
            float lateral=-h+(2.0f*h/NUM_LANES)*lane;
            float lateral2=-h2+(2.0f*h2/NUM_LANES)*lane;
            float x=roadCurveAtZ(z)+lateral;
            float x2=roadCurveAtZ(z-5.2f)+lateral2;
            glBegin(GL_QUADS);
            glColor3ub(245,245,235);
            glVertex3f(x-0.045f,roadSurfaceY(z,lateral)+0.060f,z);
            glVertex3f(x+0.045f,roadSurfaceY(z,lateral)+0.060f,z);
            glVertex3f(x2+0.045f,roadSurfaceY(z-5.2f,lateral2)+0.060f,z-5.2f);
            glVertex3f(x2-0.045f,roadSurfaceY(z-5.2f,lateral2)+0.060f,z-5.2f);
            glEnd();
        }
    }

    /* Arcade-style rumble strips for clearer depth through turns */
    for(float z=-3.2f+off;z>-120.0f;z-=5.5f){
        Color c=((int)(-z/5.5f)%2)?(Color){228,50,44,255}:(Color){244,238,220,255};
        glColor3ub(c.r,c.g,c.b);
        glBegin(GL_QUADS);
        float c1=roadCurveAtZ(z), c2=roadCurveAtZ(z-3.2f);
        float h1=roadHalfAtZ(z), h2=roadHalfAtZ(z-3.2f);
        glVertex3f(c1-h1-0.62f,roadSurfaceY(z,-h1-0.62f)+0.06f,z); glVertex3f(c1-h1-0.10f,roadSurfaceY(z,-h1-0.10f)+0.06f,z);
        glVertex3f(c2-h2-0.10f,roadSurfaceY(z-3.2f,-h2-0.10f)+0.06f,z-3.2f); glVertex3f(c2-h2-0.62f,roadSurfaceY(z-3.2f,-h2-0.62f)+0.06f,z-3.2f);
        glVertex3f(c1+h1+0.10f,roadSurfaceY(z,h1+0.10f)+0.06f,z); glVertex3f(c1+h1+0.62f,roadSurfaceY(z,h1+0.62f)+0.06f,z);
        glVertex3f(c2+h2+0.62f,roadSurfaceY(z-3.2f,h2+0.62f)+0.06f,z-3.2f); glVertex3f(c2+h2+0.10f,roadSurfaceY(z-3.2f,h2+0.10f)+0.06f,z-3.2f);
        glEnd();
    }
}

/* ══════════════════════════════════════════════
   drawWheel()
   One wheel: black tyre → silver rim → spoke cross
   ══════════════════════════════════════════════ */
static void drawWheel(int wx,int wy,int ww,int wh){
    fillR(wx,   wy,   ww,  wh,  (Color){22,22,22,255});   /* tyre  */
    fillR(wx+2, wy+2, ww-4,wh-4,(Color){170,170,170,255});/* rim   */
    fillR(wx+ww/2-1,wy+1,3,wh-2,(Color){70,70,70,255});   /* spoke V */
    fillR(wx+1,wy+wh/2-1,ww-2,3,(Color){70,70,70,255});   /* spoke H */
    outR(wx,wy,ww,wh,(Color){0,0,0,255});
}

static void box3D(float x,float y,float z,float sx,float sy,float sz,Color c){
    Color top=shade(c,1.28f), side=shade(c,0.72f), dark=shade(c,0.48f);
    float x1=x-sx/2, x2=x+sx/2, y1=y, y2=y+sy, z1=z-sz/2, z2=z+sz/2;
    glBegin(GL_QUADS);
    glColor3ub(top.r,top.g,top.b);
    glVertex3f(x1,y2,z1); glVertex3f(x2,y2,z1); glVertex3f(x2,y2,z2); glVertex3f(x1,y2,z2);
    glColor3ub(c.r,c.g,c.b);
    glVertex3f(x1,y1,z2); glVertex3f(x2,y1,z2); glVertex3f(x2,y2,z2); glVertex3f(x1,y2,z2);
    glColor3ub(side.r,side.g,side.b);
    glVertex3f(x2,y1,z1); glVertex3f(x2,y1,z2); glVertex3f(x2,y2,z2); glVertex3f(x2,y2,z1);
    glVertex3f(x1,y1,z2); glVertex3f(x1,y1,z1); glVertex3f(x1,y2,z1); glVertex3f(x1,y2,z2);
    glColor3ub(dark.r,dark.g,dark.b);
    glVertex3f(x2,y1,z1); glVertex3f(x1,y1,z1); glVertex3f(x1,y2,z1); glVertex3f(x2,y2,z1);
    glVertex3f(x1,y1,z1); glVertex3f(x2,y1,z1); glVertex3f(x2,y1,z2); glVertex3f(x1,y1,z2);
    glEnd();
}

static void gableRoof3D(float x,float y,float z,float sx,float sy,float sz,Color c){
    Color top=shade(c,1.16f), side=shade(c,0.74f), dark=shade(c,0.54f);
    float xl=x-sx/2, xr=x+sx/2, zf=z-sz/2, zb=z+sz/2, yr=y+sy;
    glBegin(GL_TRIANGLES);
    glColor3ub(c.r,c.g,c.b);
    glVertex3f(xl,y,zf); glVertex3f(xr,y,zf); glVertex3f(x,yr,zf);
    glColor3ub(dark.r,dark.g,dark.b);
    glVertex3f(xr,y,zb); glVertex3f(xl,y,zb); glVertex3f(x,yr,zb);
    glEnd();
    glBegin(GL_QUADS);
    glColor3ub(top.r,top.g,top.b);
    glVertex3f(xl,y,zf); glVertex3f(x,yr,zf); glVertex3f(x,yr,zb); glVertex3f(xl,y,zb);
    glColor3ub(side.r,side.g,side.b);
    glVertex3f(x,yr,zf); glVertex3f(xr,y,zf); glVertex3f(xr,y,zb); glVertex3f(x,yr,zb);
    glEnd();
}

static void windowPane3D(float x,float y,float z,float side,float w,float h,Color glass){
    glBegin(GL_QUADS);
    glColor3ub(glass.r,glass.g,glass.b);
    glVertex3f(x,y,z-w/2); glVertex3f(x,y,z+w/2);
    glColor3ub(210,232,236);
    glVertex3f(x,y+h,z+w/2); glVertex3f(x,y+h,z-w/2);
    glEnd();
    box3D(x-side*0.012f,y+h*0.48f,z,0.025f,0.018f,w*1.14f,(Color){45,56,58,255});
}

static void drawHouseModel3D(float x,float z,float side,int style){
    Color walls[4]={{196,174,132,255},{188,198,172,255},{205,183,158,255},{172,186,190,255}};
    Color roofs[4]={{128,60,46,255},{82,86,90,255},{145,84,52,255},{108,70,58,255}};
    Color wall=walls[style%4], roof=roofs[(style+1)%4];
    float w=1.35f+0.16f*(style%3), d=1.28f+0.12f*((style+1)%3), h=0.82f+0.10f*(style%2);
    box3D(x,0.0f,z,w,h,d,wall);
    gableRoof3D(x,h,z,w+0.28f,0.45f,d+0.22f,roof);
    float faceX=x-side*(w/2+0.018f);
    windowPane3D(faceX,0.36f,z-0.32f,-side,0.28f,0.22f,(Color){105,170,202,255});
    windowPane3D(faceX,0.36f,z+0.32f,-side,0.28f,0.22f,(Color){105,170,202,255});
    box3D(faceX,0.0f,z,0.05f,0.42f,0.26f,(Color){82,58,42,255});
    box3D(x,0.0f,z+side*0.02f,w+0.12f,0.04f,d+0.18f,(Color){115,105,90,255});
}

static void drawShopModel3D(float x,float z,float side,int style){
    Color wall=(style%2)?(Color){184,160,118,255}:(Color){174,182,166,255};
    float w=1.65f, d=1.18f, h=0.78f;
    box3D(x,0.0f,z,w,h,d,wall);
    box3D(x,h,z,w+0.18f,0.16f,d+0.10f,(Color){58,64,68,255});
    box3D(x-side*(w/2+0.08f),0.55f,z,0.08f,0.18f,d+0.22f,(Color){188,42,44,255});
    for(int i=0;i<3;i++){
        windowPane3D(x-side*(w/2+0.035f),0.28f,z-0.36f+i*0.36f,-side,0.22f,0.20f,(Color){104,168,204,255});
    }
    box3D(x-side*(w/2+0.05f),0.0f,z,0.08f,0.42f,0.28f,(Color){55,65,70,255});
    box3D(x,0.0f,z,w+0.18f,0.04f,d+0.18f,(Color){92,86,74,255});
}

static void drawTowerModel3D(float x,float z,float side,int style){
    Color wall=(style%2)?(Color){164,174,176,255}:(Color){178,164,150,255};
    Color trim=(style%2)?(Color){75,90,98,255}:(Color){112,76,66,255};
    float w=1.18f+0.1f*(style%2), d=1.05f, h=1.42f+0.18f*(style%3);
    box3D(x,0.0f,z,w,h,d,wall);
    box3D(x,h,z,w+0.12f,0.12f,d+0.12f,trim);
    for(int row=0;row<3;row++){
        for(int col=0;col<2;col++){
            windowPane3D(x-side*(w/2+0.02f),0.34f+row*0.32f,z-0.25f+col*0.50f,-side,0.20f,0.18f,(Color){98,158,192,255});
        }
    }
    box3D(x-side*(w/2+0.04f),0.0f,z,0.06f,0.36f,0.24f,(Color){50,58,62,255});
}

static void drawWindmillModel3D(float x,float z,float side,int style){
    Color wall=(style%2)?(Color){202,190,158,255}:(Color){190,184,164,255};
    Color wood={118,82,52,255};
    Color sail={238,232,205,255};
    float faceX=x-side*0.34f;
    box3D(x,0.0f,z,0.55f,1.72f,0.55f,wall);
    gableRoof3D(x,1.72f,z,0.78f,0.38f,0.72f,(Color){122,62,46,255});
    box3D(faceX,0.38f,z,0.05f,0.34f,0.20f,(Color){90,64,44,255});
    box3D(faceX,0.86f,z,0.05f,0.24f,0.18f,(Color){104,158,190,255});

    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3ub(94,72,50);
    glVertex3f(x-0.20f,0.0f,z-0.22f); glVertex3f(x+0.20f,1.54f,z+0.22f);
    glVertex3f(x+0.20f,0.0f,z-0.22f); glVertex3f(x-0.20f,1.54f,z+0.22f);
    glEnd();

    glPushMatrix();
    glTranslatef(faceX-side*0.035f,1.32f,z);
    glRotatef((float)((frameNo*4+style*31)%360),side,0.0f,0.0f);
    glBegin(GL_TRIANGLES);
    glColor3ub(sail.r,sail.g,sail.b);
    glVertex3f(0.0f,0.00f,0.05f); glVertex3f(0.0f,0.12f,0.88f); glVertex3f(0.0f,-0.08f,0.22f);
    glVertex3f(0.0f,0.00f,-0.05f); glVertex3f(0.0f,-0.12f,-0.88f); glVertex3f(0.0f,0.08f,-0.22f);
    glVertex3f(0.0f,0.05f,0.00f); glVertex3f(0.0f,0.88f,-0.12f); glVertex3f(0.0f,0.22f,0.08f);
    glVertex3f(0.0f,-0.05f,0.00f); glVertex3f(0.0f,-0.88f,0.12f); glVertex3f(0.0f,-0.22f,-0.08f);
    glEnd();
    box3D(0.0f,-0.045f,-0.045f,0.08f,0.09f,0.09f,wood);
    glPopMatrix();
}

static void drawSiloModel3D(float x,float z,float side,int style){
    Color metal=(style%2)?(Color){178,188,188,255}:(Color){190,184,168,255};
    Color cap=(style%2)?(Color){96,108,112,255}:(Color){132,82,56,255};
    box3D(x,0.0f,z,0.72f,1.42f,0.72f,metal);
    box3D(x,0.10f,z,0.86f,0.06f,0.86f,shade(metal,0.72f));
    box3D(x,0.70f,z,0.82f,0.05f,0.82f,shade(metal,0.86f));
    gableRoof3D(x,1.42f,z,0.86f,0.34f,0.86f,cap);
    box3D(x-side*0.40f,0.0f,z,0.06f,1.32f,0.08f,(Color){78,82,76,255});
}

static void drawBillboardModel3D(float x,float z,float side,int style){
    Color face=(style%2)?(Color){38,68,82,255}:(Color){88,58,76,255};
    box3D(x-side*0.30f,0.0f,z-0.34f,0.08f,0.78f,0.08f,(Color){92,82,68,255});
    box3D(x-side*0.30f,0.0f,z+0.34f,0.08f,0.78f,0.08f,(Color){92,82,68,255});
    box3D(x,0.78f,z,0.14f,0.54f,1.28f,face);
    box3D(x-side*0.08f,1.22f,z,0.08f,0.06f,1.12f,(Color){255,218,88,255});
    box3D(x-side*0.08f,0.92f,z-0.28f,0.08f,0.08f,0.34f,(Color){116,232,255,255});
    box3D(x-side*0.08f,0.92f,z+0.28f,0.08f,0.08f,0.34f,(Color){255,110,85,255});
}

static void drawRoadsideWater3D(void){
    int night=dayNightAmount();
    Color bankNear=mixColor((Color){58,112,54,255},(Color){10,48,40,255},night);
    Color bankFar =mixColor((Color){28,78,46,255},(Color){5,30,32,255},night);
    Color waterNear=mixColor((Color){24,132,178,255},(Color){6,34,86,255},night);
    Color waterFar =mixColor((Color){7,78,132,255},(Color){3,18,56,255},night);

    for(int side=-1;side<=1;side+=2){
        /* A narrow grass lip beside the road, then uninterrupted water. */
        glBegin(GL_QUAD_STRIP);
        for(int s=0;s<=50;s++){
            float z=-2.6f-(float)s*2.45f;
            float c=roadCurveAtZ(z), h=roadHalfAtZ(z);
            float roadEdge=side*h;
            float waterEdge=side*(h+1.08f);
            Color bank=mixColor(bankNear,bankFar,(s*1000)/50);
            glColor3ub(bank.r,bank.g,bank.b);
            glVertex3f(c+roadEdge,roadSurfaceY(z,roadEdge)+0.022f,z);
            glVertex3f(c+waterEdge,roadSurfaceY(z,waterEdge)+0.020f,z);
        }
        glEnd();

        glBegin(GL_QUAD_STRIP);
        for(int s=0;s<=50;s++){
            float z=-2.6f-(float)s*2.45f;
            float c=roadCurveAtZ(z), h=roadHalfAtZ(z);
            float in=side*(h+1.10f);
            float out=side*78.0f;
            Color water=mixColor(waterNear,waterFar,(s*1000)/50);
            glColor3ub(water.r,water.g,water.b);
            glVertex3f(c+in,-0.050f,z);
            glVertex3f(out,-0.050f,z);
        }
        glEnd();

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

        glLineWidth(2.0f);
        glBegin(GL_LINE_STRIP);
        glColor4ub(218,248,240,82);
        for(int s=0;s<=50;s++){
            float z=-2.6f-(float)s*2.45f;
            float h=roadHalfAtZ(z);
            float lateral=side*(h+1.36f);
            float c=roadCurveAtZ(z);
            glVertex3f(c+lateral,-0.030f,z);
        }
        glEnd();

        glLineWidth(1.0f);
        for(int band=0;band<8;band++){
            glBegin(GL_LINE_STRIP);
            glColor4ub(176,232,242,(GLubyte)(42+band*5));
            for(int s=0;s<=50;s++){
                float z=-2.6f-(float)s*2.45f;
                float h=roadHalfAtZ(z);
                float wobble=(float)(((s+band*2)%7)-3)*0.035f;
                float lateral=side*(h+3.0f+(float)band*4.2f+wobble);
                float c=roadCurveAtZ(z);
                glVertex3f(c+lateral,-0.025f,z);
            }
            glEnd();
        }
        glDisable(GL_BLEND);
    }
}

static void drawStreetLight3D(float x,float y,float z,float side,int style){
    int night=dayNightAmount();
    float poleH=2.85f+0.12f*(style%2);
    float arm=0.92f;
    Color metal=(style%2)?(Color){112,122,124,255}:(Color){92,102,106,255};
    Color warm=mixColor((Color){88,82,64,255},(Color){255,224,118,255},night);
    box3D(x,y,z,0.10f,0.08f,0.18f,(Color){72,78,78,255});
    box3D(x,y,z,0.070f,poleH,0.070f,metal);
    box3D(x-side*arm*0.42f,y+poleH-0.04f,z,arm,0.065f,0.065f,metal);
    box3D(x-side*arm,y+poleH-0.16f,z,0.30f,0.16f,0.26f,(Color){50,56,58,255});
    box3D(x-side*arm,y+poleH-0.26f,z,0.22f,0.08f,0.18f,warm);
    if(night>250){
        Color bulb=mixColor((Color){150,124,70,255},(Color){255,246,160,255},night);
        box3D(x-side*arm,y+poleH-0.31f,z,0.15f,0.055f,0.12f,bulb);
    }
}

static void drawRoadsideModels3D(void){
    for(int i=0;i<22;i++){
        int step=(i*9+(int)(roadOff*0.58f))%124;
        float z=-4.5f-(float)step;
        int right=(i%2);
        float side=right?1.0f:-1.0f;
        float h=roadHalfAtZ(z);
        float lateral=side*(h+0.62f);
        float x=roadCurveAtZ(z)+lateral;
        float y=roadSurfaceY(z,lateral)+0.02f;
        drawStreetLight3D(x,y,z,side,i);
    }
}

static void drawWheelModel3D(float x,float y,float z,float side,float s){
    float cx=x+side*0.50f*s, w=0.20f*s, r=0.20f*s;
    GLUquadric *q=gluNewQuadric();
    if(!q) return;
    glPushMatrix();
    glTranslatef(cx-side*w/2,y,z);
    glRotatef(side>0.0f?90.0f:-90.0f,0.0f,1.0f,0.0f);
    glColor3ub(12,12,14);
    gluCylinder(q,r,r,w,24,1);
    gluDisk(q,r*0.58f,r,24,1);
    glTranslatef(0.0f,0.0f,w);
    gluDisk(q,r*0.58f,r,24,1);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(cx+side*(w/2+0.012f),y,z);
    glRotatef(90.0f,0.0f,1.0f,0.0f);
    glColor3ub(185,194,198);
    gluDisk(q,0.0f,r*0.52f,24,1);
    glPopMatrix();
    box3D(cx+side*0.012f,y-0.025f,z,0.025f,0.05f,r*1.08f,(Color){82,88,90,255});
    box3D(cx+side*0.012f,y-r*0.54f,z,0.025f,0.05f,r*1.08f,(Color){82,88,90,255});
    gluDeleteQuadric(q);
}

static void drawSportModel3D(float x,float z,float s,Color body,int isPlayer,float bob){
    Color side=shade(body,0.64f), dark=shade(body,0.42f), hi=shade(body,1.25f);
    float y0=0.10f+bob, y1=0.44f*s+bob;
    float zf=z-0.96f*s, zr=z+0.94f*s;
    float wf=0.72f*s, wr=0.98f*s, tf=0.48f*s, tr=0.72f*s;

    glBegin(GL_QUADS);
    glColor3ub(hi.r,hi.g,hi.b);
    glVertex3f(x-tf/2,y1,zf+0.16f*s); glVertex3f(x+tf/2,y1,zf+0.16f*s);
    glVertex3f(x+tr/2,y1,zr-0.14f*s); glVertex3f(x-tr/2,y1,zr-0.14f*s);
    glColor3ub(body.r,body.g,body.b);
    glVertex3f(x-wf/2,y0,zf); glVertex3f(x+wf/2,y0,zf);
    glVertex3f(x+tf/2,y1,zf+0.16f*s); glVertex3f(x-tf/2,y1,zf+0.16f*s);
    glColor3ub(side.r,side.g,side.b);
    glVertex3f(x+wf/2,y0,zf); glVertex3f(x+wr/2,y0,zr);
    glVertex3f(x+tr/2,y1,zr-0.14f*s); glVertex3f(x+tf/2,y1,zf+0.16f*s);
    glVertex3f(x-wr/2,y0,zr); glVertex3f(x-wf/2,y0,zf);
    glVertex3f(x-tf/2,y1,zf+0.16f*s); glVertex3f(x-tr/2,y1,zr-0.14f*s);
    glColor3ub(dark.r,dark.g,dark.b);
    glVertex3f(x+wr/2,y0,zr); glVertex3f(x-wr/2,y0,zr);
    glVertex3f(x-tr/2,y1,zr-0.14f*s); glVertex3f(x+tr/2,y1,zr-0.14f*s);
    glEnd();

    /* Sloped glass canopy */
    glBegin(GL_QUADS);
    glColor3ub(58,112,142);
    glVertex3f(x-0.34f*s,0.47f*s+bob,z-0.35f*s);
    glVertex3f(x+0.34f*s,0.47f*s+bob,z-0.35f*s);
    glColor3ub(30,70,100);
    glVertex3f(x+0.25f*s,0.78f*s+bob,z+0.28f*s);
    glVertex3f(x-0.25f*s,0.78f*s+bob,z+0.28f*s);
    glEnd();
    box3D(x,0.78f*s+bob,z+0.07f*s,0.42f*s,0.05f*s,0.54f*s,(Color){24,52,76,255});

    /* Aerodynamic pieces */
    box3D(x,0.24f*s+bob,zf-0.03f*s,0.38f*s,0.08f*s,0.05f*s,(Color){22,22,24,255});
    box3D(x,0.55f*s+bob,zr+0.06f*s,1.05f*s,0.08f*s,0.12f*s,shade(body,0.50f));
    box3D(x-0.58f*s,0.48f*s+bob,z-0.26f*s,0.11f*s,0.07f*s,0.16f*s,shade(body,0.78f));
    box3D(x+0.58f*s,0.48f*s+bob,z-0.26f*s,0.11f*s,0.07f*s,0.16f*s,shade(body,0.78f));
    box3D(x-0.58f*s,0.18f*s+bob,z,0.10f*s,0.14f*s,1.42f*s,shade(body,0.50f));
    box3D(x+0.58f*s,0.18f*s+bob,z,0.10f*s,0.14f*s,1.42f*s,shade(body,0.50f));
    box3D(x,0.46f*s+bob,z-0.50f*s,0.20f*s,0.035f*s,0.56f*s,shade(body,1.38f));
    box3D(x,0.25f*s+bob,zf-0.08f*s,0.32f*s,0.07f*s,0.035f*s,(Color){24,24,27,255});
    box3D(x-0.22f*s,0.20f*s+bob,zr+0.11f*s,0.16f*s,0.05f*s,0.045f*s,(Color){210,18,28,255});
    box3D(x+0.22f*s,0.20f*s+bob,zr+0.11f*s,0.16f*s,0.05f*s,0.045f*s,(Color){210,18,28,255});
    box3D(x-0.40f*s,0.34f*s+bob,z-0.18f*s,0.045f*s,0.18f*s,0.42f*s,(Color){34,80,108,255});
    box3D(x+0.40f*s,0.34f*s+bob,z-0.18f*s,0.045f*s,0.18f*s,0.42f*s,(Color){34,80,108,255});
    box3D(x-0.42f*s,0.13f*s+bob,z-0.48f*s,0.18f*s,0.12f*s,0.34f*s,shade(body,0.82f));
    box3D(x+0.42f*s,0.13f*s+bob,z-0.48f*s,0.18f*s,0.12f*s,0.34f*s,shade(body,0.82f));
    box3D(x-0.42f*s,0.13f*s+bob,z+0.48f*s,0.18f*s,0.12f*s,0.34f*s,shade(body,0.74f));
    box3D(x+0.42f*s,0.13f*s+bob,z+0.48f*s,0.18f*s,0.12f*s,0.34f*s,shade(body,0.74f));

    drawWheelModel3D(x,0.20f*s+bob,z-0.48f*s,-1.0f,s);
    drawWheelModel3D(x,0.20f*s+bob,z+0.48f*s,-1.0f,s);
    drawWheelModel3D(x,0.20f*s+bob,z-0.48f*s, 1.0f,s);
    drawWheelModel3D(x,0.20f*s+bob,z+0.48f*s, 1.0f,s);

    box3D(x-0.24f*s,0.31f*s+bob,zf-0.05f*s,0.18f*s,0.08f*s,0.04f*s,(Color){255,246,170,255});
    box3D(x+0.24f*s,0.31f*s+bob,zf-0.05f*s,0.18f*s,0.08f*s,0.04f*s,(Color){255,246,170,255});
    box3D(x-0.26f*s,0.27f*s+bob,zr+0.05f*s,0.18f*s,0.07f*s,0.04f*s,(Color){235,28,35,255});
    box3D(x+0.26f*s,0.27f*s+bob,zr+0.05f*s,0.18f*s,0.07f*s,0.04f*s,(Color){235,28,35,255});
    if(isPlayer) box3D(x,0.46f*s+bob,z-0.12f*s,0.10f*s,0.035f*s,1.25f*s,(Color){30,255,225,255});
}

static void drawCngModel3D(float x,float z,float s,Color body,float bob){
    Color green={24,138,78,255}, yellow={244,196,48,255};
    Color roof={28,42,38,255}, glass={86,178,198,255};
    box3D(x,0.11f*s+bob,z,0.86f*s,0.34f*s,1.36f*s,green);
    box3D(x,0.47f*s+bob,z-0.08f*s,0.72f*s,0.54f*s,0.90f*s,green);
    box3D(x,0.96f*s+bob,z-0.08f*s,0.82f*s,0.10f*s,0.98f*s,roof);
    box3D(x,0.62f*s+bob,z-0.58f*s,0.48f*s,0.22f*s,0.05f*s,glass);
    box3D(x-0.40f*s,0.49f*s+bob,z-0.06f*s,0.045f*s,0.28f*s,0.62f*s,(Color){18,84,56,255});
    box3D(x+0.40f*s,0.49f*s+bob,z-0.06f*s,0.045f*s,0.28f*s,0.62f*s,(Color){18,84,56,255});
    box3D(x,0.25f*s+bob,z-0.78f*s,0.50f*s,0.16f*s,0.16f*s,yellow);
    box3D(x,0.18f*s+bob,z+0.72f*s,0.58f*s,0.12f*s,0.12f*s,shade(green,0.65f));
    drawWheelModel3D(x,0.14f*s+bob,z-0.42f*s,-1.0f,s*0.88f);
    drawWheelModel3D(x,0.14f*s+bob,z-0.42f*s, 1.0f,s*0.88f);
    box3D(x,0.09f*s+bob,z+0.58f*s,0.22f*s,0.24f*s,0.30f*s,(Color){16,16,18,255});
    box3D(x,0.26f*s+bob,z-0.90f*s,0.13f*s,0.07f*s,0.04f*s,(Color){255,242,158,255});
}

static void drawRickshawModel3D(float x,float z,float s,Color body,float bob){
    Color frame={210,40,54,255}, canopy={38,160,86,255}, cloth={244,208,58,255};
    box3D(x,0.12f*s+bob,z+0.20f*s,0.92f*s,0.24f*s,0.78f*s,frame);
    box3D(x,0.44f*s+bob,z+0.20f*s,0.82f*s,0.50f*s,0.70f*s,cloth);
    box3D(x,0.92f*s+bob,z+0.20f*s,0.96f*s,0.10f*s,0.84f*s,canopy);
    box3D(x-0.48f*s,0.34f*s+bob,z+0.20f*s,0.055f*s,0.72f*s,0.055f*s,(Color){48,58,50,255});
    box3D(x+0.48f*s,0.34f*s+bob,z+0.20f*s,0.055f*s,0.72f*s,0.055f*s,(Color){48,58,50,255});
    box3D(x,0.22f*s+bob,z-0.42f*s,0.44f*s,0.10f*s,0.64f*s,(Color){110,70,42,255});
    box3D(x,0.20f*s+bob,z-0.86f*s,0.12f*s,0.08f*s,0.55f*s,(Color){48,58,50,255});
    drawWheelModel3D(x,0.10f*s+bob,z+0.48f*s,-1.0f,s*0.82f);
    drawWheelModel3D(x,0.10f*s+bob,z+0.48f*s, 1.0f,s*0.82f);
    box3D(x,0.08f*s+bob,z-0.98f*s,0.26f*s,0.28f*s,0.18f*s,(Color){16,16,18,255});
    box3D(x,0.56f*s+bob,z+0.58f*s,0.42f*s,0.22f*s,0.05f*s,(Color){72,170,205,255});
}

static void drawBusModel3D(float x,float z,float s,Color body,float bob){
    Color bus={238,190,44,255}, lower={38,146,88,255}, glass={70,150,188,255};
    box3D(x,0.12f*s+bob,z,1.16f*s,0.62f*s,1.72f*s,bus);
    box3D(x,0.16f*s+bob,z+0.14f*s,1.20f*s,0.24f*s,1.34f*s,lower);
    box3D(x,0.78f*s+bob,z,1.10f*s,0.10f*s,1.74f*s,(Color){54,62,58,255});
    for(int i=0;i<4;i++){
        box3D(x-0.48f*s+i*0.32f*s,0.50f*s+bob,z-0.70f*s,0.22f*s,0.18f*s,0.055f*s,glass);
    }
    box3D(x,0.52f*s+bob,z-0.89f*s,0.72f*s,0.22f*s,0.055f*s,glass);
    box3D(x,0.27f*s+bob,z-0.94f*s,0.32f*s,0.10f*s,0.05f*s,(Color){255,238,146,255});
    box3D(x,0.23f*s+bob,z+0.91f*s,0.42f*s,0.08f*s,0.05f*s,(Color){210,24,30,255});
    drawWheelModel3D(x,0.14f*s+bob,z-0.58f*s,-1.0f,s*0.95f);
    drawWheelModel3D(x,0.14f*s+bob,z+0.58f*s,-1.0f,s*0.95f);
    drawWheelModel3D(x,0.14f*s+bob,z-0.58f*s, 1.0f,s*0.95f);
    drawWheelModel3D(x,0.14f*s+bob,z+0.58f*s, 1.0f,s*0.95f);
}

static void drawPickupModel3D(float x,float z,float s,Color body,float bob){
    Color cab={54,126,194,255}, cargo={166,104,58,255};
    box3D(x,0.12f*s+bob,z+0.24f*s,1.02f*s,0.34f*s,0.98f*s,cargo);
    box3D(x,0.18f*s+bob,z-0.56f*s,0.86f*s,0.52f*s,0.72f*s,cab);
    box3D(x,0.62f*s+bob,z-0.68f*s,0.58f*s,0.22f*s,0.06f*s,(Color){82,170,202,255});
    box3D(x,0.44f*s+bob,z+0.16f*s,1.04f*s,0.08f*s,0.96f*s,(Color){88,58,38,255});
    box3D(x,0.24f*s+bob,z-0.94f*s,0.34f*s,0.09f*s,0.05f*s,(Color){255,238,150,255});
    box3D(x,0.22f*s+bob,z+0.79f*s,0.30f*s,0.07f*s,0.05f*s,(Color){210,22,28,255});
    drawWheelModel3D(x,0.13f*s+bob,z-0.52f*s,-1.0f,s*0.92f);
    drawWheelModel3D(x,0.13f*s+bob,z+0.50f*s,-1.0f,s*0.92f);
    drawWheelModel3D(x,0.13f*s+bob,z-0.52f*s, 1.0f,s*0.92f);
    drawWheelModel3D(x,0.13f*s+bob,z+0.50f*s, 1.0f,s*0.92f);
}

static void drawBangladeshVehicle3D(float x,float z,float s,int vehicle,Color body,float bob){
    switch(vehicle){
        case 1: drawCngModel3D(x,z,s,body,bob); break;
        case 2: drawRickshawModel3D(x,z,s,body,bob); break;
        case 3: drawBusModel3D(x,z,s*0.95f,body,bob); break;
        default: drawPickupModel3D(x,z,s,body,bob); break;
    }
}

static void drawWheel3D(float x,float z,float side){
    box3D(x+side*0.43f,0.06f,z-0.42f,0.22f,0.30f,0.34f,(Color){14,14,16,255});
    box3D(x+side*0.43f,0.06f,z+0.42f,0.22f,0.30f,0.34f,(Color){14,14,16,255});
    box3D(x+side*0.45f,0.13f,z-0.42f,0.04f,0.15f,0.16f,(Color){195,205,210,255});
    box3D(x+side*0.45f,0.13f,z+0.42f,0.04f,0.15f,0.16f,(Color){195,205,210,255});
}

/* ══════════════════════════════════════════════
   drawCar()
   Slim, detailed car using layered rects.
   Wheels protrude WHEEL_EXT px on each side –
   matching the collision box exactly.
   ══════════════════════════════════════════════ */
static void drawCar(Car *c,int isPlayer){
    if(!c->active) return;
    use3D();
    float carZ=isPlayer ? -4.8f : carDepth(c);
    float carLateral=carLanePos(c);
    float carX=roadCurveAtZ(carZ)+carLateral;
    if(!isPlayer && (carZ>-3.4f || carZ<-118.0f)) return;
    float carBob=(float)pulse(frameNo+(int)c->y,32,3)*0.015f;
    float s=isPlayer ? 1.15f : 1.10f;
    if(!isPlayer && carZ<-28.0f) s=1.10f+((-28.0f-carZ)/90.0f)*0.70f;

    glPushMatrix();
    glTranslatef(0.0f,roadSurfaceY(carZ,carLateral),0.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor4ub(0,0,0,85);
    glVertex3f(carX-0.55f*s,0.025f,carZ-0.85f*s);
    glVertex3f(carX+0.55f*s,0.025f,carZ-0.85f*s);
    glVertex3f(carX+0.60f*s,0.025f,carZ+0.85f*s);
    glVertex3f(carX-0.60f*s,0.025f,carZ+0.85f*s);
    glEnd();
    glDisable(GL_BLEND);

    Color body=c->body;
    if(isPlayer && invFrames>0 && (invFrames/5)%2==0) body=(Color){235,250,255,255};
    if(isPlayer) drawSportModel3D(carX,carZ,s,body,isPlayer,carBob);
    else drawBangladeshVehicle3D(carX,carZ,s,c->vehicle,body,carBob);
    glPopMatrix();
    return;
    box3D(carX,0.10f+carBob,carZ,0.82f*s,0.34f*s,1.45f*s,body);
    box3D(carX,0.42f*s+carBob,carZ-0.05f*s,0.56f*s,0.36f*s,0.72f*s,
          isPlayer?(Color){105,210,255,255}:(Color){210,220,235,255});
    box3D(carX,0.22f*s+carBob,carZ-0.72f*s,0.64f*s,0.18f*s,0.28f*s,shade(body,1.12f));
    box3D(carX,0.23f*s+carBob,carZ+0.72f*s,0.64f*s,0.16f*s,0.28f*s,shade(body,0.65f));
    if(isPlayer) box3D(carX,0.80f*s+carBob,carZ-0.05f*s,0.12f*s,0.03f*s,1.05f*s,(Color){20,255,235,255});

    /* Sport-car details: windshield, side mirrors, spoiler, trim, and grille. */
    box3D(carX,0.73f+carBob,carZ-0.22f,0.48f,0.04f,0.28f,(Color){45,95,125,255});
    box3D(carX,0.70f+carBob,carZ+0.27f,0.42f,0.035f,0.22f,(Color){34,68,92,255});
    box3D(carX-0.54f,0.50f+carBob,carZ-0.22f,0.12f,0.08f,0.18f,shade(body,0.82f));
    box3D(carX+0.54f,0.50f+carBob,carZ-0.22f,0.12f,0.08f,0.18f,shade(body,0.82f));
    box3D(carX,0.54f+carBob,carZ+0.90f,0.92f,0.07f,0.12f,shade(body,0.55f));
    box3D(carX-0.28f,0.23f+carBob,carZ-0.91f,0.18f,0.07f,0.04f,(Color){24,24,26,255});
    box3D(carX,0.24f+carBob,carZ-0.92f,0.24f,0.05f,0.035f,(Color){24,24,26,255});
    box3D(carX+0.28f,0.23f+carBob,carZ-0.91f,0.18f,0.07f,0.04f,(Color){24,24,26,255});
    box3D(carX-0.48f,0.24f+carBob,carZ,0.035f,0.11f,1.10f,shade(body,0.58f));
    box3D(carX+0.48f,0.24f+carBob,carZ,0.035f,0.11f,1.10f,shade(body,0.58f));
    box3D(carX,0.46f*s+carBob,carZ-0.62f*s,0.20f*s,0.035f*s,0.58f*s,shade(body,1.35f));
    box3D(carX,0.22f*s+carBob,carZ-0.985f*s,0.34f*s,0.07f*s,0.035f*s,(Color){232,232,210,255});
    box3D(carX,0.20f*s+carBob,carZ+0.985f*s,0.32f*s,0.06f*s,0.035f*s,(Color){235,232,190,255});
    box3D(carX-0.52f*s,0.33f*s+carBob,carZ-0.42f*s,0.055f*s,0.12f*s,0.28f*s,(Color){40,80,108,255});
    box3D(carX+0.52f*s,0.33f*s+carBob,carZ-0.42f*s,0.055f*s,0.12f*s,0.28f*s,(Color){40,80,108,255});

    drawWheel3D(carX,carZ,-1.0f);
    drawWheel3D(carX,carZ, 1.0f);
    box3D(carX-0.22f,0.34f+carBob,carZ-0.86f,0.18f,0.09f,0.06f,(Color){255,245,140,255});
    box3D(carX+0.22f,0.34f+carBob,carZ-0.86f,0.18f,0.09f,0.06f,(Color){255,245,140,255});
    box3D(carX-0.22f,0.31f+carBob,carZ+0.86f,0.18f,0.08f,0.06f,(Color){230,20,20,255});
    box3D(carX+0.22f,0.31f+carBob,carZ+0.86f,0.18f,0.08f,0.06f,(Color){230,20,20,255});
    return;
    int x=(int)c->x, y=(int)c->y;
    int bob=isPlayer ? pulse(frameNo,28,3)-1 : pulse(frameNo+(int)c->y,34,2)-1;
    y+=bob;

    /* Drop shadow */
    blendR(x+5,y+13,CAR_W,CAR_H-2,(Color){0,0,0,70});
    if(isPlayer) glowR(x-2,y+4,CAR_W+4,CAR_H-4,(Color){0,210,255,45});

    /* ── Body (3 rects for rounded silhouette) ── */
    fillR(x+5, y+2,  CAR_W-10, CAR_H-2,  c->body);
    fillR(x+2, y+10, CAR_W-4,  CAR_H-18, c->body);
    fillR(x,   y+16, CAR_W,    CAR_H-30, c->body);

    /* Darker lower-body shade for depth */
    Color dk=shade(c->body,0.72f);
    fillR(x+2,y+CAR_H/2,CAR_W-4,CAR_H/2-12,dk);
    blendR(x+4,y+7,5,CAR_H-18,(Color){255,255,255,70});
    blendR(x+CAR_W-9,y+11,4,CAR_H-26,(Color){0,0,0,45});

    /* ── Cabin ── */
    Color cabin=isPlayer?(Color){195,245,255,255}:(Color){255,205,205,255};
    fillR(x+7,y+18,CAR_W-14,CAR_H-38,cabin);

    /* ── Windshield ── */
    Color glass={130,195,255,210};
    if(isPlayer)
        blendR(x+8, y+13,CAR_W-16,12,glass);   /* front glass top */
    else
        blendR(x+8, y+CAR_H-27,CAR_W-16,12,glass); /* front glass bottom */

    /* ── Rear window ── */
    Color rearG={85,145,195,175};
    if(isPlayer)
        blendR(x+8,y+CAR_H-28,CAR_W-16,9,rearG);
    else
        blendR(x+8,y+19,CAR_W-16,9,rearG);

    /* ── Roof stripe (player only) ── */
    if(isPlayer){
        Color stripe={0,155,195,255};
        fillR(x+11,y+18,CAR_W-22,CAR_H-38,stripe);
        glowR(x+12,y+18,CAR_W-24,CAR_H-38,(Color){80,240,255,55});
    }

    /* ── Body outline ── */
    outR(x+5,y+2,  CAR_W-10,CAR_H-2,  (Color){0,0,0,255});
    outR(x,  y+16, CAR_W,    CAR_H-30,(Color){0,0,0,255});

    /* ── Wheels (4 corners, protruding WHEEL_EXT px sideways) ── */
    int ww=8,wh=18;
    drawWheel(x-WHEEL_EXT, y+10,        ww,wh);  /* front-left  */
    drawWheel(x+CAR_W-2,   y+10,        ww,wh);  /* front-right */
    drawWheel(x-WHEEL_EXT, y+CAR_H-28,  ww,wh);  /* rear-left   */
    drawWheel(x+CAR_W-2,   y+CAR_H-28,  ww,wh);  /* rear-right  */

    /* ── Lights ── */
    Color hl={255,255,150,255},hlGlow={255,255,80,110};
    Color tl={200,0,0,255},    tlGlow={255,50,50,120};

    if(isPlayer){
        /* Headlights at top */
        blendR(x+2,  y+4,12,8,hlGlow);
        blendR(x+CAR_W-14,y+4,12,8,hlGlow);
        fillR( x+3,  y+5,10,6,hl);
        fillR( x+CAR_W-13,y+5,10,6,hl);
        /* Tail lights at bottom */
        fillR(x+3,        y+CAR_H-11,10,5,tl);
        fillR(x+CAR_W-13, y+CAR_H-11,10,5,tl);
    } else {
        /* Headlights at bottom */
        blendR(x+2,        y+CAR_H-12,12,8,hlGlow);
        blendR(x+CAR_W-14, y+CAR_H-12,12,8,hlGlow);
        fillR( x+3,        y+CAR_H-11,10,6,hl);
        fillR( x+CAR_W-13, y+CAR_H-11,10,6,hl);
        /* Glowing tail lights at top */
        blendR(x+2,        y+4,12,8,tlGlow);
        blendR(x+CAR_W-14, y+4,12,8,tlGlow);
        fillR( x+3,        y+5,10,6,tl);
        fillR( x+CAR_W-13, y+5,10,6,tl);
    }

    /* ── Invincibility blink overlay ── */
    if(isPlayer && invFrames>0 && (invFrames/5)%2==0)
        blendR(x,y+2,CAR_W,CAR_H-2,(Color){255,255,255,150});
}

/* ══════════════════════════════════════════════
   drawHUD()
   Left panel: score & speed
   Right panel: heart lives & best score
   ══════════════════════════════════════════════ */
static void drawDayNightOverlay(void){
    int night=dayNightAmount();
    if(night<=0) return;
    blendR(0,0,WIN_W,WIN_H,(Color){0,10,32,(Uint8)(night*76/1000)});
}

static void drawHUDPanel(int x,int y,int w,int h,Color accent){
    glowR(x+2,y+2,w-4,h-4,(Color){accent.r,accent.g,accent.b,34});
    blendR(x+4,y+5,w,h,(Color){0,0,0,70});
    blendR(x,y,w,h,(Color){7,12,20,205});
    blendR(x+2,y+2,w-4,18,(Color){255,255,255,18});
    fillR(x,y,w,3,accent);
    fillR(x,y,3,h,shade(accent,0.70f));
    fillR(x+w-3,y,3,h,shade(accent,0.45f));
    fillR(x,y+h-3,w,3,shade(accent,0.40f));
    line(x+8,y+12,x+28,y+12,(Color){255,255,255,90});
    line(x+w-28,y+h-12,x+w-8,y+h-12,(Color){255,255,255,70});
}

static void drawHUDMeter(int x,int y,int w,int h,int value,int max,Color fill){
    if(max<=0) max=1;
    int bar=(value*w)/max;
    if(bar<0) bar=0;
    if(bar>w) bar=w;
    fillR(x,y,w,h,(Color){18,24,30,235});
    fillR(x+1,y+1,w-2,h-2,(Color){35,44,50,255});
    fillR(x+2,y+2,bar>4?bar-4:0,h-4,fill);
    blendR(x+2,y+2,w-4,3,(Color){255,255,255,42});
}

static void drawTinyCarIcon(int x,int y,Color c){
    blendR(x+1,y+4,30,10,(Color){0,0,0,80});
    fillR(x+6,y,20,11,c);
    fillR(x+2,y+7,28,10,shade(c,0.82f));
    fillR(x+9,y+2,10,7,(Color){115,190,220,255});
    circle2D(x+8,y+18,4,(Color){18,18,20,255});
    circle2D(x+24,y+18,4,(Color){18,18,20,255});
    fillR(x+1,y+10,4,3,(Color){255,232,126,255});
}

static void drawLifeSlot(int x,int y,int alive){
    Color bg=alive?(Color){72,18,30,230}:(Color){22,28,34,220};
    Color edge=alive?(Color){255,84,104,255}:(Color){70,82,90,255};
    blendR(x,y,42,34,bg);
    outR(x,y,42,34,edge);
    drawHeart2D(x+10,y+7,7,alive?(Color){238,35,66,255}:(Color){76,82,88,255});
}

static void drawHUD(void){
    Color white={245,250,255,255};
    Color mute={144,166,178,255};
    Color cyan={86,220,255,255};
    Color amber={255,194,70,255};
    Color green={112,255,150,255};
    char buf[64];

    drawHUDPanel(12,10,218,112,cyan);
    drawTinyCarIcon(24,68,(Color){36,190,238,255});
    renderText("SCORE",24,20,mute,font);
    sprintf(buf,"%d",score);
    renderText(buf,24,40,white,font);
    renderText("SPEED",70,70,mute,font);
    sprintf(buf,"%d km/h",gameSpeed*22);
    renderText(buf,150,70,amber,font);
    drawHUDMeter(70,96,136,12,gameSpeed,MAX_SPEED,amber);
    if(hudPulse>0) glowR(70,96,(gameSpeed*136)/MAX_SPEED,12,(Color){255,235,90,(Uint8)(hudPulse*7)});

    int rx=WIN_W-230;
    drawHUDPanel(rx,10,218,112,(Color){255,86,122,255});
    renderText("VITALS",rx+16,20,mute,font);
    for(int i=0;i<MAX_LIVES;i++){
        drawLifeSlot(rx+16+i*50,42,i<lives);
    }

    int hi=0;
    FILE *f=fopen("highscore.dat","r");
    if(f){fscanf(f,"%d",&hi);fclose(f);}
    sprintf(buf,"BEST %d",hi);
    renderText(buf,rx+16,84,green,font);
    if(invFrames>0){
        sprintf(buf,"SHIELD %02d",invFrames/10);
        renderText(buf,rx+118,84,cyan,font);
        drawHUDMeter(rx+118,104,76,8,invFrames,INVINCIBLE_FRM,cyan);
    }
}

/* ══════════════════════════════════════════════
   HIGH SCORE
   ══════════════════════════════════════════════ */
static void drawGameOverBackdrop(void){
    drawBackground();
    drawRoad();
    drawSpeedLines();
    blendR(0,0,WIN_W,WIN_H,(Color){6,8,16,132});
    blendR(0,0,WIN_W,102,(Color){0,0,0,82});
    blendR(0,WIN_H-122,WIN_W,122,(Color){0,0,0,96});
    for(int i=0;i<7;i++){
        int y=96+i*72+((frameNo*2+i*9)%24);
        blendR(0,y,WIN_W,3,(Color){255,64,64,16});
    }
    for(int i=0;i<5;i++){
        int x=60+i*180+((frameNo+i*13)%36);
        blendR(x,70,86,8,(Color){255,220,90,26});
    }
    blendR(0,HORIZON_Y-18,WIN_W,4,(Color){255,90,70,55});
}

static void drawCrashPanel(int hi){
    Color red={235,40,50,255},orange={255,150,40,255};
    Color white={255,255,255,255},yellow={255,220,90,255};
    Color cyan={120,235,255,255},green={110,255,140,255};

    int panelX=92, panelY=86, panelW=716, panelH=392;
    glowR(panelX-8,panelY-8,panelW+16,panelH+16,(Color){255,50,50,32});
    blendR(panelX,panelY,panelW,panelH,(Color){8,10,16,224});
    outR(panelX,panelY,panelW,panelH,(Color){255,78,78,175});
    outR(panelX+10,panelY+10,panelW-20,panelH-20,(Color){255,220,90,72});

    fillR(panelX+14,panelY+14,56,4,(Color){255,80,80,220});
    fillR(panelX+14,panelY+14,4,56,(Color){255,80,80,220});
    fillR(panelX+panelW-70,panelY+14,56,4,(Color){255,80,80,220});
    fillR(panelX+panelW-18,panelY+14,4,56,(Color){255,80,80,220});
    fillR(panelX+14,panelY+panelH-18,56,4,(Color){255,80,80,220});
    fillR(panelX+14,panelY+panelH-70,4,56,(Color){255,80,80,220});
    fillR(panelX+panelW-70,panelY+panelH-18,56,4,(Color){255,80,80,220});
    fillR(panelX+panelW-18,panelY+panelH-70,4,56,(Color){255,80,80,220});

    int cx=panelX+112, cy=panelY+146;
    glowR(cx-42,cy-26,84,52,(Color){255,60,40,55});
    fillR(cx-44,cy-10,88,32,(Color){35,40,52,255});
    fillR(cx-28,cy-28,56,18,(Color){35,40,52,255});
    fillR(cx-36,cy+18,14,12,(Color){18,18,22,255});
    fillR(cx+22,cy+18,14,12,(Color){18,18,22,255});
    line(cx-24,cy-18,cx-2,cy+2,(Color){255,102,80,255});
    line(cx-2,cy+2,cx+24,cy-12,(Color){255,102,80,255});
    line(cx-8,cy-26,cx+12,cy+18,(Color){255,102,80,255});
    circle2D(cx-20,cy+8,5,(Color){255,180,70,255});
    circle2D(cx+18,cy+8,5,(Color){255,180,70,255});
    tri2D(cx+34,cy-30,cx+52,cy-4,cx+10,cy-8,(Color){255,206,70,255});
    tri2D(cx+34,cy-24,cx+44,cy-8,cx+18,cy-10,(Color){255,60,50,255});

    if(bigFont) renderCentered("CRASHED",114+pulse(frameNo,40,4),red,bigFont);
    renderCentered("Your run ended in a wall of sparks.",172,white,font);

    char buf[64];
    sprintf(buf,"Score  %d",score);
    renderCentered(buf,234,cyan,font);
    sprintf(buf,"Best   %d",hi);
    renderCentered(buf,262,yellow,font);
    if(score>0&&score>=hi) renderCentered("NEW HIGH SCORE",292,orange,font);
    else renderCentered("ROUTE CLOSED",292,orange,font);

    blendR(142,334,616,86,(Color){0,0,0,145});
    outR(142,334,616,86,(Color){255,255,255,42});
    renderCentered("R   Restart the race",352,green,font);
    renderCentered("ESC Quit to desktop",382,white,font);

    if((frameNo/24)%2==0){
        renderCentered("Press R to run it back",428,yellow,font);
    }
}

static void saveHighScore(int s){
    int hi=0; FILE *f=fopen("highscore.dat","r");
    if(f){fscanf(f,"%d",&hi);fclose(f);}
    if(s>hi){f=fopen("highscore.dat","w");if(f){fprintf(f,"%d",s);fclose(f);}}
}

/* ══════════════════════════════════════════════
   resetGame()
   ══════════════════════════════════════════════ */
static void resetGame(void){
    score=0; lives=MAX_LIVES; gameSpeed=INITIAL_SPEED;
    invFrames=0; roadOff=0.0f; shakeFrames=0; hudPulse=0;
    player.x    =(float)laneX(1);
    player.y    =(float)(WIN_H-CAR_H-20);
    player.speed= PLAYER_SPEED;
    player.body =(Color){0,210,255,255};
    player.active=1;
    player.vehicle=0;
    for(int i=0;i<MAX_ENEMIES;i++){
        enemies[i].active=0;
        enemies[i].y=(float)(-CAR_H - i*140);  /* stagger heights */
        spawnEnemy(i);
    }
    for(int i=0;i<MAX_PARTICLES;i++) sparks[i].life=0;
    player.x=(float)laneX(1);
    resetSpeedLines();
}

/* ══════════════════════════════════════════════
   titleScreen()
   ══════════════════════════════════════════════ */
static void titleScreen(void){
    int blink=0; SDL_Event e;
    while(1){
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT){running=0;return;}
            if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_RETURN) return;
        }
        frameNo++;
        roadOff+=3.2f;
        updateSpeedLines();
        drawBackground();
        drawRoad();
        drawSpeedLines();
        blendR(82,44,536,98,(Color){0,0,0,185});
        outR(92,54,516,78,(Color){0,220,255,170});
        Color yellow={255,220,0,255},white={255,255,255,255};
        if(bigFont) renderCentered("TURBO  ROAD",58+pulse(frameNo,70,6),yellow,bigFont);
        renderCentered("SDL2 Car Racing Game",145,white,font);

        /* Three demo cars in each lane */
        Car d; d.active=1; d.speed=0;
        d.x=(float)laneX(0); d.y=200+pulse(frameNo,50,12); d.body=(Color){24,138,78,255}; d.vehicle=1; drawCar(&d,0);
        d.x=(float)laneX(1); d.y=210-pulse(frameNo,46,10); d.body=(Color){0,210,255,255}; d.vehicle=0; drawCar(&d,1);
        d.x=(float)laneX(2); d.y=198+pulse(frameNo+20,54,12); d.body=(Color){244,208,58,255}; d.vehicle=2; drawCar(&d,0);

        renderCentered("Controls:",                         295,yellow,font);
        renderCentered("LEFT / RIGHT arrows  =  steer",    317,white, font);
        renderCentered("Dodge all oncoming traffic!",       339,white, font);
        renderCentered("Speed increases every 20 points",   361,white, font);
        renderCentered("3 lives  |  Tyre hit = lose 1 life",383,white, font);
        if((blink/28)%2==0)
            renderCentered(">>  Press ENTER to Start  <<",426,yellow,font);
        SDL_RenderPresent(ren);
        SDL_Delay(16); blink++;
    }
}

/* ══════════════════════════════════════════════
   gameOverScreen()  returns 1=restart  0=quit
   ══════════════════════════════════════════════ */
static int gameOverScreen(void){
    saveHighScore(score);
    int hi=0; FILE *f=fopen("highscore.dat","r");
    if(f){fscanf(f,"%d",&hi);fclose(f);}
    spawnExplosion(WIN_W/2,WIN_H/2);
    SDL_Event e;
    while(1){
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT){running=0;return 0;}
            if(e.type==SDL_KEYDOWN){
                if(e.key.keysym.sym==SDLK_r)      return 1;
                if(e.key.keysym.sym==SDLK_ESCAPE){running=0;return 0;}
            }
        }
        frameNo++;
        glClearColor(0.02f,0.02f,0.04f,1.0f); SDL_RenderClear(ren);
        drawGameOverBackdrop();
        updateParticles(); drawParticles();
        drawCrashPanel(hi);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
}

/* ══════════════════════════════════════════════
   MAIN
   ══════════════════════════════════════════════ */
int main(int argc,char *argv[]){
    srand((unsigned)time(NULL));
    if(SDL_Init(SDL_INIT_VIDEO)<0){fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    if(TTF_Init()<0){fprintf(stderr,"TTF: %s\n",TTF_GetError());SDL_Quit();return 1;}

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);

    win=SDL_CreateWindow("Turbo Road 3D",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,WIN_W,WIN_H,
        SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN);
    if(!win){fprintf(stderr,"Window: %s\n",SDL_GetError());TTF_Quit();SDL_Quit();return 1;}
    glCtx=SDL_GL_CreateContext(win);
    if(!glCtx){fprintf(stderr,"OpenGL: %s\n",SDL_GetError());SDL_DestroyWindow(win);TTF_Quit();SDL_Quit();return 1;}
    SDL_GL_SetSwapInterval(1);
    glViewport(0,0,WIN_W,WIN_H);
    glClearColor(0.36f,0.62f,0.86f,1.0f);
    glDisable(GL_CULL_FACE);

    const char *fp[]={"C:/Windows/Fonts/arial.ttf","C:/Windows/Fonts/verdana.ttf",
                      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",NULL};
    for(int i=0;fp[i]&&!font;i++){
        font   =TTF_OpenFont(fp[i],16);
        bigFont=TTF_OpenFont(fp[i],44);
    }

    resetSpeedLines();
    titleScreen();

    /* Outer loop – one iteration per game round */
    while(running){
        resetGame();
        Uint32 last=SDL_GetTicks();

        /* Inner loop – one iteration per frame */
        while(running){
            Uint32 now=SDL_GetTicks();
            if(now-last<FRAME_MS){SDL_Delay(1);continue;}
            last=now;
            frameNo++;

            /* ── INPUT ── */
            SDL_Event ev;
            while(SDL_PollEvent(&ev)){
                if(ev.type==SDL_QUIT){running=0;break;}
                if(ev.type==SDL_KEYDOWN&&ev.key.keysym.sym==SDLK_ESCAPE) running=0;
            }
            if(!running) break;

            const Uint8 *keys=SDL_GetKeyboardState(NULL);
            if(keys[SDL_SCANCODE_LEFT]  && player.x > ROAD_LEFT+4)
                player.x-=(float)player.speed;
            if(keys[SDL_SCANCODE_RIGHT] && player.x+CAR_W < ROAD_RIGHT-4)
                player.x+=(float)player.speed;

            /* ── UPDATE ── */
            roadOff+=(float)gameSpeed*0.85f;
            updateEnemies();
            updateSpeedLines();
            if(gameSpeed>=3 || keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_RIGHT])
                spawnExhaust();
            updateParticles();
            if(invFrames>0) invFrames--;
            if(shakeFrames>0) shakeFrames--;
            if(hudPulse>0) hudPulse--;

            /* ── COLLISION DETECTION ──
               Uses AABB that covers the entire car INCLUDING side wheels.
               Any tyre overlap triggers a life deduction.              */
            if(invFrames==0){
                AABB pb=getCarAABB(&player);
                for(int i=0;i<MAX_ENEMIES;i++){
                    if(!enemies[i].active) continue;
                    if(aabbOverlap(pb, getCarAABB(&enemies[i]))){

                        /* 1) spawn sparks at impact point */
                        spawnExplosion((int)(player.x+CAR_W/2),
                                       (int)(player.y+CAR_H/2));
                        shakeFrames=16;
                        /* 2) remove the enemy that was hit */
                        enemies[i].active=0;
                        /* 3) deduct one life */
                        lives--;

                        if(lives<=0){
                            /* Show crash frame briefly, then game over */
                            SDL_RenderClear(ren);
                            drawBackground(); drawSpeedLines(); drawRoad();
                            for(int j=0;j<MAX_ENEMIES;j++) drawCar(&enemies[j],0);
                            drawCar(&player,1);
                            drawParticles(); drawDayNightOverlay(); drawHUD();
                            SDL_RenderPresent(ren);
                            SDL_Delay(500);
                            if(!gameOverScreen()) running=0;
                            goto next_round;
                        }
                        /* Grant invincibility frames so player can recover */
                        invFrames=INVINCIBLE_FRM;
                    }
                }
            }

            /* ── RENDER ── */
            SDL_RenderClear(ren);
            SDL_Rect vp={0,0,WIN_W,WIN_H};
            if(shakeFrames>0){
                vp.x=(rand()%9)-4;
                vp.y=(rand()%7)-3;
            }
            SDL_RenderSetViewport(ren,&vp);
            drawBackground();
            drawSpeedLines();
            drawRoad();
            for(int i=0;i<MAX_ENEMIES;i++) drawCar(&enemies[i],0);
            drawCar(&player,1);
            drawParticles();
            SDL_RenderSetViewport(ren,NULL);
            drawDayNightOverlay();
            drawHUD();
            SDL_RenderPresent(ren);
        }
        next_round:;
    }

    if(font)   TTF_CloseFont(font);
    if(bigFont)TTF_CloseFont(bigFont);
    TTF_Quit();
    if(glCtx) SDL_GL_DeleteContext(glCtx);
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
