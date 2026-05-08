/*
 =============================================================
   TURBO ROAD  -  SDL2 Car Racing Game in C
   Build: gcc car_game.c -o car_game -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf -lopengl32
 =============================================================
*/

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_opengl.h>
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
#define WIN_W       700
#define WIN_H       500
#define FPS         60
#define FRAME_MS    (1000 / FPS)

/* ─── Road layout (wider road, narrow grass strips) ─── */
#define ROAD_LEFT    175               /* left edge of road  */
#define ROAD_RIGHT   525              /* right edge of road */
#define ROAD_W       (ROAD_RIGHT - ROAD_LEFT)   /* 350 px total road */
#define NUM_LANES    3
#define LANE_W       (ROAD_W / NUM_LANES)       /* 117 px per lane  */
#define HORIZON_Y    132
#define ROAD_NEAR_W  500
#define ROAD_FAR_W    72

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

static float playerLanePos(void){
    return ((player.x-(float)ROAD_LEFT)/(float)ROAD_W)*6.0f-3.0f;
}
static float carLanePos(Car *c){
    return ((c->x-(float)ROAD_LEFT)/(float)ROAD_W)*6.0f-3.0f;
}
static float carDepth(Car *c){
    return -7.0f - ((float)WIN_H-c->y)*0.105f;
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
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-aspect*0.075,aspect*0.075,-0.055,0.105,0.1,140.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(9.0f,1.0f,0.0f,0.0f);
    glTranslatef(-playerLanePos()*0.14f,-3.0f,0.4f);
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
static void glowR(int x,int y,int w,int h,Color c);
static void drawHeart2D(int x,int y,int s,Color c){
    Color shine={255,170,180,190};
    glowR(x+2,y+2,s*2,s*2,(Color){255,35,70,85});
    circle2D(x+s/2,y+s/2,s/2,c);
    circle2D(x+s+s/2,y+s/2,s/2,c);
    tri2D(x,y+s/2,x+s*2,y+s/2,x+s,y+s*2,c);
    circle2D(x+s/2-1,y+s/2-1,s/5,shine);
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

    /* Calm morning sky */
    for(int y=0;y<WIN_H;y+=2){
        Uint8 r=(Uint8)(96+y/12), g=(Uint8)(166+y/13), b=(Uint8)(224+y/34);
        fillR(0,y,WIN_W,2,(Color){r,g,b,255});
    }

    circle2D(105,72,30,(Color){255,224,145,170});
    circle2D(105,72,18,(Color){255,242,198,235});

    /* Very slow cloud banks */
    for(int i=0;i<4;i++){
        int cx=(i*230-cloudShift+WIN_W)%WIN_W;
        int cy=34+(i%2)*34;
        blendR(cx,cy,116,13,(Color){255,255,255,82});
        blendR(cx+26,cy-9,70,16,(Color){255,255,255,70});
        blendR(cx+82,cy+5,58,10,(Color){255,255,255,52});
    }

    /* Soft distant hills, almost fixed to the horizon */
    for(int x=-220-hillShift;x<WIN_W+240;x+=170){
        tri2D(x,HORIZON_Y+22,x+96,70+((x+11)&18),x+205,HORIZON_Y+22,(Color){91,131,128,245});
        tri2D(x+46,HORIZON_Y+22,x+142,86+((x+23)&16),x+260,HORIZON_Y+22,(Color){75,118,111,245});
    }
    for(int x=-120-(hillShift/2);x<WIN_W+180;x+=150){
        tri2D(x,HORIZON_Y+44,x+84,104+((x+19)&16),x+190,HORIZON_Y+44,(Color){48,112,82,255});
        tri2D(x+78,HORIZON_Y+44,x+154,113+((x+3)&14),x+250,HORIZON_Y+44,(Color){39,96,72,255});
    }

    /* Field with subtle bands, not scrolling clutter */
    fillR(0,HORIZON_Y+18,WIN_W,WIN_H-HORIZON_Y-18,(Color){31,128,59,255});
    blendR(0,HORIZON_Y+40,WIN_W,22,(Color){90,168,74,95});
    blendR(0,HORIZON_Y+82,WIN_W,18,(Color){21,101,48,80});

    /* Small village held near the horizon so it feels distant and realistic */
    int villageX=74-(hillShift/5);
    for(int i=0;i<5;i++){
        int x=villageX+i*92;
        fillR(x,HORIZON_Y+29,44,24,(Color){205,184,142,255});
        tri2D(x-5,HORIZON_Y+29,x+22,HORIZON_Y+14,x+49,HORIZON_Y+29,(Color){132,65,52,255});
        fillR(x+9,HORIZON_Y+39,8,14,(Color){92,66,46,255});
        fillR(x+25,HORIZON_Y+35,9,7,(Color){132,180,205,255});
    }

    /* Fence line and tree clusters, slow enough not to distract */
    int fenceShift=(int)(roadOff*0.12f)%80;
    for(int x=-80-fenceShift;x<WIN_W+80;x+=80){
        fillR(x,HORIZON_Y+86,54,3,(Color){196,176,128,230});
        fillR(x+8,HORIZON_Y+75,4,22,(Color){148,106,67,255});
        fillR(x+44,HORIZON_Y+75,4,22,(Color){148,106,67,255});
    }
    for(int i=0;i<7;i++){
        int tx=(i*118-((int)(roadOff*0.10f)%130)+WIN_W)%WIN_W;
        int ty=HORIZON_Y+54+(i%2)*22;
        fillR(tx,ty+18,7,28,(Color){104,75,48,255});
        circle2D(tx+4,ty+9,17,(Color){31,105,55,255});
        circle2D(tx-8,ty+18,13,(Color){24,87,47,255});
        circle2D(tx+16,ty+18,13,(Color){42,127,65,255});
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
static void drawRoad(void){
    use3D();

    /* Ground plane */
    glBegin(GL_QUADS);
    glColor3ub(18,95,45);
    glVertex3f(-80.0f,-0.08f,-2.6f);
    glVertex3f( 80.0f,-0.08f,-2.6f);
    glColor3ub(8,55,35);
    glVertex3f( 80.0f,-0.08f,-125.0f);
    glVertex3f(-80.0f,-0.08f,-125.0f);
    glEnd();

    /* Actual 3D asphalt plane */
    glBegin(GL_QUADS);
    glColor3ub(62,64,68);
    glVertex3f(-4.8f,0.0f,-2.6f);
    glVertex3f( 4.8f,0.0f,-2.6f);
    glColor3ub(28,30,34);
    glVertex3f( 4.1f,0.0f,-125.0f);
    glVertex3f(-4.1f,0.0f,-125.0f);
    glEnd();

    /* Shoulders */
    glBegin(GL_QUADS);
    glColor3ub(115,105,88);
    glVertex3f(-6.0f,0.01f,-2.6f); glVertex3f(-4.8f,0.01f,-2.6f);
    glColor3ub(72,70,62);
    glVertex3f(-4.1f,0.01f,-125.0f); glVertex3f(-5.1f,0.01f,-125.0f);
    glColor3ub(115,105,88);
    glVertex3f(4.8f,0.01f,-2.6f); glVertex3f(6.0f,0.01f,-2.6f);
    glColor3ub(72,70,62);
    glVertex3f(5.1f,0.01f,-125.0f); glVertex3f(4.1f,0.01f,-125.0f);
    glEnd();

    /* Road edge lines */
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glColor3ub(255,218,48);
    glVertex3f(-4.55f,0.035f,-2.6f); glVertex3f(-3.95f,0.035f,-125.0f);
    glVertex3f( 4.55f,0.035f,-2.6f); glVertex3f( 3.95f,0.035f,-125.0f);
    glEnd();

    /* Lane divider dashes moving in world space */
    float off=(float)((int)(roadOff*0.10f)%12);
    for(float z=-4.0f+off;z>-122.0f;z-=12.0f){
        for(int lane=1;lane<NUM_LANES;lane++){
            float x=-4.45f+(8.9f/NUM_LANES)*lane;
            glBegin(GL_QUADS);
            glColor3ub(245,245,235);
            glVertex3f(x-0.045f,0.045f,z);
            glVertex3f(x+0.045f,0.045f,z);
            glVertex3f(x+0.045f,0.045f,z-5.2f);
            glVertex3f(x-0.045f,0.045f,z-5.2f);
            glEnd();
        }
    }

    /* Red/white rumble strip blocks */
    for(float z=-3.2f+off;z>-120.0f;z-=6.0f){
        Color c=((int)(-z/6.0f)%2)?(Color){230,30,28,255}:(Color){240,240,230,255};
        glColor3ub(c.r,c.g,c.b);
        glBegin(GL_QUADS);
        glVertex3f(-4.7f,0.05f,z); glVertex3f(-4.25f,0.05f,z);
        glVertex3f(-4.25f,0.05f,z-3.0f); glVertex3f(-4.7f,0.05f,z-3.0f);
        glVertex3f(4.25f,0.05f,z); glVertex3f(4.7f,0.05f,z);
        glVertex3f(4.7f,0.05f,z-3.0f); glVertex3f(4.25f,0.05f,z-3.0f);
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
    float carX=carLanePos(c);
    float carZ=isPlayer ? -5.8f : carDepth(c);
    if(!isPlayer && (carZ>-3.4f || carZ<-118.0f)) return;
    float carBob=(float)pulse(frameNo+(int)c->y,32,3)*0.015f;
    float s=isPlayer ? 1.08f : 1.0f;
    if(!isPlayer && carZ<-35.0f) s=1.0f+((-35.0f-carZ)/83.0f)*0.55f;

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
static void drawHUD(void){
    Color panelBg={0,0,0,165};
    blendR(0,           0,ROAD_LEFT-8,         115,panelBg);
    blendR(ROAD_RIGHT+8,0,WIN_W-ROAD_RIGHT-8,  115,panelBg);
    outR(5,5,ROAD_LEFT-18,105,(Color){80,220,255,150});
    outR(ROAD_RIGHT+13,5,WIN_W-ROAD_RIGHT-23,105,(Color){255,220,80,150});

    Color yellow={255,220,0,255},white={255,255,255,255};
    Color green={100,255,100,255},red={220,0,0,255};

    char buf[64];
    renderText("SCORE", 6, 8,  yellow, font);
    sprintf(buf,"%d",score);
    renderText(buf,     6, 28, white,  font);
    renderText("SPEED", 6, 58, yellow, font);
    sprintf(buf,"%d km/h",gameSpeed*22);
    renderText(buf,     6, 78, white,  font);
    fillR(72,84,82,8,(Color){35,35,35,255});
    int bar=(gameSpeed*82)/MAX_SPEED;
    fillR(72,84,bar,8,(Color){255,190,0,255});
    if(hudPulse>0) glowR(72,84,bar,8,(Color){255,240,80,(Uint8)(hudPulse*7)});

    renderText("LIVES",ROAD_RIGHT+10, 8,yellow,font);
    /* Heart icons for lives */
    for(int i=0;i<MAX_LIVES;i++){
        int hx=ROAD_RIGHT+10+i*28;
        Color hc=(i<lives)?red:(Color){75,75,80,210};
        drawHeart2D(hx,28,9,hc);
    }

    int hi=0;
    FILE *f=fopen("highscore.dat","r");
    if(f){fscanf(f,"%d",&hi);fclose(f);}
    sprintf(buf,"BEST:%d",hi);
    renderText(buf,ROAD_RIGHT+10,60,green,font);
    if(invFrames>0){
        sprintf(buf,"SHIELD %02d",invFrames/10);
        renderText(buf,ROAD_RIGHT+10,84,(Color){120,230,255,255},font);
    }
}

/* ══════════════════════════════════════════════
   HIGH SCORE
   ══════════════════════════════════════════════ */
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
        d.x=(float)laneX(0); d.y=200+pulse(frameNo,50,12); d.body=(Color){220,0,0,255};   drawCar(&d,0);
        d.x=(float)laneX(1); d.y=210-pulse(frameNo,46,10); d.body=(Color){0,210,255,255}; drawCar(&d,1);
        d.x=(float)laneX(2); d.y=198+pulse(frameNo+20,54,12); d.body=(Color){100,180,0,255}; drawCar(&d,0);

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
    int blink=0; SDL_Event e;
    while(1){
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT){running=0;return 0;}
            if(e.type==SDL_KEYDOWN){
                if(e.key.keysym.sym==SDLK_r)      return 1;
                if(e.key.keysym.sym==SDLK_ESCAPE){running=0;return 0;}
            }
        }
        frameNo++;
        glClearColor(0.03f,0.03f,0.05f,1.0f); SDL_RenderClear(ren);
        for(int y=0;y<WIN_H;y+=12)
            blendR(0,y+pulse(frameNo,80,10),WIN_W,2,(Color){255,40,40,35});
        updateParticles(); drawParticles();
        blendR(104,58,492,335,(Color){0,0,0,220});
        outR(114,68,472,315,(Color){255,80,80,170});
        Color red={220,0,0,255},yellow={255,220,0,255};
        Color white={255,255,255,255},green={100,255,100,255};
        if(bigFont) renderCentered("GAME  OVER",82+pulse(frameNo,50,5),red,bigFont);
        char buf[64];
        sprintf(buf,"Your Score :  %d",score); renderCentered(buf,190,white,font);
        sprintf(buf,"Best Score :  %d",hi);    renderCentered(buf,218,yellow,font);
        if(score>0&&score>=hi) renderCentered("NEW HIGH SCORE!",250,yellow,font);
        renderCentered("R      =   Play Again",305,green,font);
        renderCentered("ESC  =   Quit",         331,white,font);
        if((blink/28)%2==0) renderCentered("Better luck next time!",372,red,font);
        SDL_RenderPresent(ren); SDL_Delay(16); blink++;
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
                            drawParticles(); drawHUD();
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
