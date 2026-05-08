/*
 =============================================================
   TURBO ROAD  -  SDL2 Car Racing Game in C
   Build: gcc car_game.c -o car_game -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf
 =============================================================
*/

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

/* ─── Car dimensions (thinner than before) ─── */
#define CAR_W        32    /* body width  – slim fit in lane */
#define CAR_H        62    /* body height                   */
#define WHEEL_EXT     6    /* wheels protrude this far sideways */
/* Full visual span = CAR_W + WHEEL_EXT*2 = 44px  (lane is 180px → lots of room) */

/* ─── Game limits ─── */
#define MAX_ENEMIES    5
#define MAX_LIVES      3
#define MAX_PARTICLES  40

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

/* ══════════════════════════════════════════════
   GLOBALS
   ══════════════════════════════════════════════ */
static SDL_Window   *win     = NULL;
static SDL_Renderer *ren     = NULL;
static TTF_Font     *font    = NULL;
static TTF_Font     *bigFont = NULL;

static Car      player;
static Car      enemies[MAX_ENEMIES];
static Particle sparks[MAX_PARTICLES];

static int   score     = 0;
static int   lives     = MAX_LIVES;
static int   gameSpeed = INITIAL_SPEED;
static float roadOff   = 0.0f;
static int   invFrames = 0;   /* countdown – player is untouchable while > 0 */
static int   running   = 1;

static Color enemyCols[6] = {
    {220,  0,  0,255},   /* red     */
    {200,  0,200,255},   /* magenta */
    {255,120,  0,255},   /* orange  */
    {255, 80,180,255},   /* pink    */
    {100,180,  0,255},   /* lime    */
    {  0,160,220,255},   /* sky blue*/
};

/* ══════════════════════════════════════════════
   DRAW PRIMITIVES
   ══════════════════════════════════════════════ */
static void setCol(SDL_Renderer *r, Color c){
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}
static void fillR(int x,int y,int w,int h,Color c){
    setCol(ren,c); SDL_Rect r={x,y,w,h}; SDL_RenderFillRect(ren,&r);
}
static void outR(int x,int y,int w,int h,Color c){
    setCol(ren,c); SDL_Rect r={x,y,w,h}; SDL_RenderDrawRect(ren,&r);
}
static void blendR(int x,int y,int w,int h,Color c){
    SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);
    fillR(x,y,w,h,c);
    SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_NONE);
}

/* ══════════════════════════════════════════════
   TEXT HELPERS
   ══════════════════════════════════════════════ */
static void renderText(const char *t,int x,int y,Color c,TTF_Font *f){
    if(!f) return;
    SDL_Color sc={c.r,c.g,c.b,c.a};
    SDL_Surface *s=TTF_RenderText_Blended(f,t,sc); if(!s) return;
    SDL_Texture *tx=SDL_CreateTextureFromSurface(ren,s);
    SDL_Rect d={x,y,s->w,s->h};
    SDL_RenderCopy(ren,tx,NULL,&d);
    SDL_DestroyTexture(tx); SDL_FreeSurface(s);
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
            if(score%SPEED_UP_EVERY==0 && gameSpeed<MAX_SPEED) gameSpeed++;
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

/* ══════════════════════════════════════════════
   drawBackground()
   Sky, wide grass strips, road shoulders, trees
   ══════════════════════════════════════════════ */
static void drawBackground(void){
    /* Sky */
    fillR(0,0,WIN_W,WIN_H,(Color){135,206,235,255});

    /* Grass strips (narrower now – road is wider) */
    Color gr={34,139,34,255};
    fillR(0,0,ROAD_LEFT,WIN_H,gr);
    fillR(ROAD_RIGHT,0,WIN_W-ROAD_RIGHT,WIN_H,gr);

    /* Road shoulders */
    Color sh={40,40,40,255};
    fillR(ROAD_LEFT-8,0,8,WIN_H,sh);
    fillR(ROAD_RIGHT, 0,8,WIN_H,sh);

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
}

/* ══════════════════════════════════════════════
   drawRoad()
   Road surface, yellow kerbs, scrolling dashes
   ══════════════════════════════════════════════ */
static void drawRoad(void){
    fillR(ROAD_LEFT,0,ROAD_W,WIN_H,(Color){55,55,55,255});

    /* Yellow kerb lines */
    Color kerb={255,220,0,255};
    fillR(ROAD_LEFT-4,0,5,WIN_H,kerb);
    fillR(ROAD_RIGHT-1,0,5,WIN_H,kerb);

    /* Scrolling dashed lane dividers */
    Color wh={255,255,255,255};
    int dashH=36,gapH=22,period=dashH+gapH;
    int off=(int)roadOff%period;
    for(int y=-period+off;y<WIN_H;y+=period){
        fillR(ROAD_LEFT+LANE_W-2,    y,4,dashH,wh);  /* divider 1 */
        fillR(ROAD_LEFT+LANE_W*2-2,  y,4,dashH,wh);  /* divider 2 */
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

/* ══════════════════════════════════════════════
   drawCar()
   Slim, detailed car using layered rects.
   Wheels protrude WHEEL_EXT px on each side –
   matching the collision box exactly.
   ══════════════════════════════════════════════ */
static void drawCar(Car *c,int isPlayer){
    if(!c->active) return;
    int x=(int)c->x, y=(int)c->y;

    /* Drop shadow */
    blendR(x+4,y+10,CAR_W,CAR_H-4,(Color){0,0,0,50});

    /* ── Body (3 rects for rounded silhouette) ── */
    fillR(x+5, y+2,  CAR_W-10, CAR_H-2,  c->body);
    fillR(x+2, y+10, CAR_W-4,  CAR_H-18, c->body);
    fillR(x,   y+16, CAR_W,    CAR_H-30, c->body);

    /* Darker lower-body shade for depth */
    Color dk={(Uint8)(c->body.r*0.72f),(Uint8)(c->body.g*0.72f),(Uint8)(c->body.b*0.72f),255};
    fillR(x+2,y+CAR_H/2,CAR_W-4,CAR_H/2-12,dk);

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

    Color yellow={255,220,0,255},white={255,255,255,255};
    Color green={100,255,100,255},red={220,0,0,255};

    char buf[64];
    renderText("SCORE", 6, 8,  yellow, font);
    sprintf(buf,"%d",score);
    renderText(buf,     6, 28, white,  font);
    renderText("SPEED", 6, 58, yellow, font);
    sprintf(buf,"%d km/h",gameSpeed*22);
    renderText(buf,     6, 78, white,  font);

    renderText("LIVES",ROAD_RIGHT+10, 8,yellow,font);
    /* Heart icons for lives */
    for(int i=0;i<lives;i++){
        int hx=ROAD_RIGHT+10+i*26;
        fillR(hx,   30, 9,13,red);
        fillR(hx+9, 30, 9,13,red);
        fillR(hx+2, 25, 6, 8,red);
        fillR(hx+10,25, 6, 8,red);
        fillR(hx+3, 40, 6, 5,red);
        fillR(hx+5, 44, 4, 4,red);
        fillR(hx+6, 47, 2, 3,red);
    }

    int hi=0;
    FILE *f=fopen("highscore.dat","r");
    if(f){fscanf(f,"%d",&hi);fclose(f);}
    sprintf(buf,"BEST:%d",hi);
    renderText(buf,ROAD_RIGHT+10,60,green,font);
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
    invFrames=0; roadOff=0.0f;
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
        /* Dark gradient */
        for(int y=0;y<WIN_H;y++){
            int v=18+(int)(y*0.12f); if(v>72)v=72;
            SDL_SetRenderDrawColor(ren,0,0,v,255);
            SDL_RenderDrawLine(ren,0,y,WIN_W,y);
        }
        /* Road preview */
        fillR(ROAD_LEFT,0,ROAD_W,WIN_H,(Color){55,55,55,255});
        for(int y=0;y<WIN_H;y+=54){
            fillR(ROAD_LEFT+LANE_W-2,    y,4,36,(Color){255,255,255,255});
            fillR(ROAD_LEFT+LANE_W*2-2,  y,4,36,(Color){255,255,255,255});
        }
        blendR(100,50,500,85,(Color){0,0,0,170});
        Color yellow={255,220,0,255},white={255,255,255,255};
        if(bigFont) renderCentered("TURBO  ROAD",62,yellow,bigFont);
        renderCentered("SDL2 Car Racing Game",145,white,font);

        /* Three demo cars in each lane */
        Car d; d.active=1; d.speed=0;
        d.x=(float)laneX(0); d.y=200; d.body=(Color){220,0,0,255};   drawCar(&d,0);
        d.x=(float)laneX(1); d.y=200; d.body=(Color){0,210,255,255}; drawCar(&d,1);
        d.x=(float)laneX(2); d.y=200; d.body=(Color){100,180,0,255}; drawCar(&d,0);

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
        SDL_SetRenderDrawColor(ren,8,8,8,255); SDL_RenderClear(ren);
        updateParticles(); drawParticles();
        blendR(120,70,460,310,(Color){0,0,0,215});
        Color red={220,0,0,255},yellow={255,220,0,255};
        Color white={255,255,255,255},green={100,255,100,255};
        if(bigFont) renderCentered("GAME  OVER",85,red,bigFont);
        char buf[64];
        sprintf(buf,"Your Score :  %d",score); renderCentered(buf,190,white,font);
        sprintf(buf,"Best Score :  %d",hi);    renderCentered(buf,218,yellow,font);
        if(score>0&&score>=hi) renderCentered("★  NEW HIGH SCORE!  ★",250,yellow,font);
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

    win=SDL_CreateWindow("Turbo Road",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,WIN_W,WIN_H,SDL_WINDOW_SHOWN);
    ren=SDL_CreateRenderer(win,-1,
        SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);

    const char *fp[]={"C:/Windows/Fonts/arial.ttf","C:/Windows/Fonts/verdana.ttf",
                      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",NULL};
    for(int i=0;fp[i]&&!font;i++){
        font   =TTF_OpenFont(fp[i],16);
        bigFont=TTF_OpenFont(fp[i],44);
    }

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
            updateParticles();
            if(invFrames>0) invFrames--;

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
                        /* 2) remove the enemy that was hit */
                        enemies[i].active=0;
                        /* 3) deduct one life */
                        lives--;

                        if(lives<=0){
                            /* Show crash frame briefly, then game over */
                            SDL_RenderClear(ren);
                            drawBackground(); drawRoad();
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
            drawBackground();
            drawRoad();
            for(int i=0;i<MAX_ENEMIES;i++) drawCar(&enemies[i],0);
            drawCar(&player,1);
            drawParticles();
            drawHUD();
            SDL_RenderPresent(ren);
        }
        next_round:;
    }

    if(font)   TTF_CloseFont(font);
    if(bigFont)TTF_CloseFont(bigFont);
    TTF_Quit();
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}