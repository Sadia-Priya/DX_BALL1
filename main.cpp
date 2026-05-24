#include <GL/glut.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Window ─────────────────────────────────────────────────────────────── */
#define WIN_W 800
#define WIN_H 650

/* ── Play area ──────────────────────────────────────────────────────────── */
#define PLAY_LEFT   10.f
#define PLAY_RIGHT  790.f
#define PLAY_TOP    640.f
#define PLAY_BOTTOM 10.f

/* ── Paddle ─────────────────────────────────────────────────────────────── */
#define PAD_H      14.f
#define PAD_Y      38.f
#define PAD_WIDE   175.f
#define PAD_NARROW 65.f
#define PAD_SPEED  420.f

/* ── Ball ───────────────────────────────────────────────────────────────── */
#define BALL_R 8.f

/* ── Blocks ─────────────────────────────────────────────────────────────── */
#define COLS     12
#define ROWS     7
#define BLK_W    ((PLAY_RIGHT - PLAY_LEFT) / COLS)
#define BLK_H    34.f
#define BLK_TOP_Y (PLAY_TOP - 10.f)

/* ── Perk item ──────────────────────────────────────────────────────────── */
#define PERK_W    22.f
#define PERK_H    14.f
#define PERK_FALL 170.f
#define MAX_PERKS  128

/* ── Max array sizes (dynamic vector)*/
#define MAX_STARS  150
#define MAX_HS     10
#define MAX_PTS    32   /* polygon vertices */

/* ════════════════════════════════════════════════════════════════════════
   Enums
   ════════════════════════════════════════════════════════════════════════ */
typedef enum { STATE_MENU, STATE_DIFF_SELECT, STATE_PLAYING,
               STATE_PAUSED, STATE_HIGHSCORE, STATE_WIN, STATE_LOSE,
               STATE_WALLPAPER } GameState;

typedef enum { PERK_NONE=0, PERK_EXTRA_LIFE, PERK_SPEED_UP,
               PERK_WIDE_PAD, PERK_FIREBALL, PERK_SHRINK_PAD,
               PERK_SLOW_BALL } PerkType;

typedef enum { SND_WALL, SND_PADDLE, SND_BLOCK,
               SND_PERK, SND_DEATH, SND_WIN } SoundType;

typedef enum { WALL_SPACE=0, WALL_OCEAN, WALL_FOREST,
               WALL_SUNSET, WALL_MATRIX, WALL_COUNT } WallTheme;

/* ════════════════════════════════════════════════════════════════════════
   Structs
   ════════════════════════════════════════════════════════════════════════ */
typedef struct { float x,y,r,g,b; int hits; PerkType perk; int alive; } Block;
typedef struct { float x,y,vx,vy,speed,angle; int fireball,active; } Ball;
typedef struct { float x,y,rot; PerkType type; int alive; } Perk;
typedef struct { float x,y,br,spd; } Star;
typedef struct { int score,diff; float time; char dname[8]; } HSEntry;
typedef struct { float x,y; } Vec2;
typedef struct { int x,y; } IVec2;

typedef struct { float initSpeed,maxSpeed,incRate,padW; const char* name; } DiffConfig;

/* ════════════════════════════════════════════════════════════════════════
   Difficulty presets
   ════════════════════════════════════════════════════════════════════════ */
static const DiffConfig DIFF[3] = {
    { 240.f, 480.f, 3.5f, 130.f, "EASY"   },
    { 310.f, 620.f, 6.0f, 110.f, "MEDIUM" },
    { 400.f, 780.f, 9.5f,  85.f, "HARD"   },
};

static const char* WALL_NAMES[WALL_COUNT] = {
    "SPACE", "OCEAN", "FOREST", "SUNSET", "MATRIX"
};

/* ════════════════════════════════════════════════════════════════════════
   ACADEMIC GRAPHICS ALGORITHMS (Flood & Boundary fill removed)
   ════════════════════════════════════════════════════════════════════════ */

/* ── Pixel emitter ──────────────────────────────────────────────────────── */
static void draw_pixel(int x, int y) {
    glBegin(GL_POINTS); glVertex2i(x,y); glEnd();
}

/* ══════════════════════════════════════════════════
   1. BRESENHAM LINE ALGORITHM
   ══════════════════════════════════════════════════ */
static void draw_line(int x1,int y1,int x2,int y2) {
    int dx=x2-x1, dy=y2-y1;
    int ax=dx<0?-dx:dx, ay=dy<0?-dy:dy;
    int sx=x2<x1?-1:1,  sy=y2<y1?-1:1;
    int x=x1, y=y1;
    draw_pixel(x,y);
    if(ax>ay) {
        int e=2*ay-ax, inc1=2*(ay-ax), inc2=2*ay;
        int i;
        for(i=0;i<ax;i++){
            if(e>=0){ y+=sy; e+=inc1; } else e+=inc2;
            x+=sx; draw_pixel(x,y);
        }
    } else {
        int e=2*ax-ay, inc1=2*(ax-ay), inc2=2*ax;
        int i;
        for(i=0;i<ay;i++){
            if(e>=0){ x+=sx; e+=inc1; } else e+=inc2;
            y+=sy; draw_pixel(x,y);
        }
    }
}

static void draw_thick_line(int x1,int y1,int x2,int y2,int thick) {
    int half=thick/2, t;
    int dx=x2-x1, dy=y2-y1;
    float len=sqrtf((float)(dx*dx+dy*dy));
    if(len<1.f){ draw_line(x1,y1,x2,y2); return; }
    float nx=-dy/len, ny=dx/len;
    for(t=-half;t<=half;t++)
        draw_line((int)(x1+t*nx),(int)(y1+t*ny),
                  (int)(x2+t*nx),(int)(y2+t*ny));
}

/* ══════════════════════════════════════════════════
   2. MIDPOINT CIRCLE ALGORITHM
   ══════════════════════════════════════════════════ */
static void plot_circle_pts(int cx,int cy,int px,int py) {
    draw_pixel(cx+px,cy+py); draw_pixel(cx-px,cy+py);
    draw_pixel(cx+px,cy-py); draw_pixel(cx-px,cy-py);
    draw_pixel(cx+py,cy+px); draw_pixel(cx-py,cy+px);
    draw_pixel(cx+py,cy-px); draw_pixel(cx-py,cy-px);
}
static void midpoint_circle(int cx,int cy,int r) {
    int x=0,y=r,p=1-r;
    while(x<=y){
        plot_circle_pts(cx,cy,x,y);
        if(p<0){ x++; p+=2*x+1; }
        else   { x++; y--; p+=2*x-2*y+1; }
    }
}
static void filled_midpoint_circle(int cx,int cy,int r) {
    int x=0,y=r,p=1-r;
    while(x<=y){
        draw_line(cx-y,cy+x,cx+y,cy+x);
        draw_line(cx-y,cy-x,cx+y,cy-x);
        draw_line(cx-x,cy+y,cx+x,cy+y);
        draw_line(cx-x,cy-y,cx+x,cy-y);
        if(p<0){ x++; p+=2*x+1; }
        else   { x++; y--; p+=2*x-2*y+1; }
    }
}

/* ══════════════════════════════════════════════════
   3. SCANLINE POLYGON FILL
   ══════════════════════════════════════════════════ */
static void scanline_fill_polygon(IVec2* pts, int n) {
    int i,j,k,y;
    float xs[MAX_PTS];
    int nx;
    int yMin, yMax;
    if(n<3) return;
    yMin=pts[0].y; yMax=pts[0].y;
    for(i=0;i<n;i++){
        if(pts[i].y<yMin) yMin=pts[i].y;
        if(pts[i].y>yMax) yMax=pts[i].y;
    }
    for(y=yMin;y<=yMax;y++){
        nx=0;
        for(i=0;i<n;i++){
            j=(i+1)%n;
            int y0=pts[i].y, y1=pts[j].y;
            float x0=(float)pts[i].x, x1=(float)pts[j].x;
            if((y0<=y&&y<y1)||(y1<=y&&y<y0)){
                float t=(float)(y-y0)/(float)(y1-y0);
                xs[nx++]=x0+t*(x1-x0);
            }
        }
        /* bubble sort  */
        for(i=0;i<nx-1;i++) for(k=0;k<nx-1-i;k++)
            if(xs[k]>xs[k+1]){ float tmp=xs[k]; xs[k]=xs[k+1]; xs[k+1]=tmp; }
        for(k=0;k+1<nx;k+=2)
            draw_line((int)xs[k],y,(int)xs[k+1],y);
    }
}

static void scanline_fill_rect(int x,int y,int w,int h) {
    IVec2 pts[4]={{x,y},{x+w,y},{x+w,y+h},{x,y+h}};
    scanline_fill_polygon(pts,4);
}

/* ══════════════════════════════════════════════════
   4. 2D TRANSFORMATION HELPERS
   ══════════════════════════════════════════════════ */
static Vec2 transform_point(float px,float py,
                             float tx,float ty,
                             float angle,float sx,float sy) {
    float qx=px*sx, qy=py*sy;
    float c=(float)cos(angle), s=(float)sin(angle);
    Vec2 r;
    r.x = c*qx - s*qy + tx;
    r.y = s*qx + c*qy + ty;
    return r;
}

static void draw_transformed_poly(Vec2* local,int n,
                                   float tx,float ty,float angle,
                                   float sx,float sy) {
    IVec2 world[MAX_PTS];
    int i;
    for(i=0;i<n;i++){
        Vec2 p=transform_point(local[i].x,local[i].y,tx,ty,angle,sx,sy);
        world[i].x=(int)p.x; world[i].y=(int)p.y;
    }
    scanline_fill_polygon(world,n);
}

static void draw_transformed_poly_outline(Vec2* local,int n,
                                           float tx,float ty,float angle,
                                           float sx,float sy) {
    IVec2 world[MAX_PTS];
    int i;
    for(i=0;i<n;i++){
        Vec2 p=transform_point(local[i].x,local[i].y,tx,ty,angle,sx,sy);
        world[i].x=(int)p.x; world[i].y=(int)p.y;
    }
    for(i=0;i<n;i++){
        int j=(i+1)%n;
        draw_line(world[i].x,world[i].y,world[j].x,world[j].y);
    }
}

/* ════════════════════════════════════════════════════════════════════════
   SOUND SYSTEM (pthread background thread)
   ════════════════════════════════════════════════════════════════════════ */
static pthread_mutex_t g_sndMtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_sndCond = PTHREAD_COND_INITIALIZER;
static SoundType       g_sndQ[16];
static int             g_sndHead=0, g_sndTail=0;
static volatile int    g_sndRun=1;

static float sndFreq(SoundType t) {
    switch(t){
        case SND_WALL:   return 220.f;
        case SND_PADDLE: return 330.f;
        case SND_BLOCK:  return 440.f;
        case SND_PERK:   return 660.f;
        case SND_DEATH:  return 110.f;
        default:         return 880.f;
    }
}
static int sndMs(SoundType t) {
    switch(t){
        case SND_DEATH: return 280;
        case SND_WIN:   return 480;
        case SND_PERK:  return 140;
        default:        return 55;
    }
}

static void emitBeep(float freq, int ms) {
    Beep((DWORD)freq,(DWORD)ms);
    (void)freq; (void)ms;
}

static void* sndThread(void* arg) {
    (void)arg;
    while(g_sndRun){
        pthread_mutex_lock(&g_sndMtx);
        while(g_sndHead==g_sndTail && g_sndRun)
            pthread_cond_wait(&g_sndCond,&g_sndMtx);
        if(!g_sndRun){ pthread_mutex_unlock(&g_sndMtx); break; }
        SoundType t=g_sndQ[g_sndHead]; g_sndHead=(g_sndHead+1)%16;
        pthread_mutex_unlock(&g_sndMtx);
        emitBeep(sndFreq(t), sndMs(t));
    }
    return NULL;
}
static void playSound(SoundType t) {
    pthread_mutex_lock(&g_sndMtx);
    int next=(g_sndTail+1)%16;
    if(next!=g_sndHead){
        g_sndQ[g_sndTail]=t; g_sndTail=next;
        pthread_cond_signal(&g_sndCond);
    }
    pthread_mutex_unlock(&g_sndMtx);
}

/* ════════════════════════════════════════════════════════════════════════
   GLOBALS
   ════════════════════════════════════════════════════════════════════════ */
static GameState gState    = STATE_MENU;
static int  gMenuItem = 0;
static int  gDiffSel  = 1;
static int  gCurDiff  = 1;
static WallTheme gWall = WALL_SPACE;

static float padX=WIN_W/2.f, padWidth=110.f;
static float padSquash=1.f;
static Ball  ball;
static Block blocks[ROWS][COLS];
static int   blocksLeft=0;

static Perk    perks[MAX_PERKS];  int  perkCount=0;
static Star    stars[MAX_STARS];
static HSEntry highScores[MAX_HS]; int hsCount=0;

static int   lives=3, score=0;
static float elapsed=0.f, wideTimer=0.f, narrowTimer=0.f, fireTimer=0.f;
static int   keyLeft=0, keyRight=0;
static float gMenuTime=0.f;
static float lastTime=0.f;

/* ════════════════════════════════════════════════════════════════════════
   HELPER MACROS / FUNCTIONS
   ════════════════════════════════════════════════════════════════════════ */
#define SC(r,g,b)     glColor4f(r,g,b,1.f)
#define SCA(r,g,b,a)  glColor4f(r,g,b,a)

static float fmaxf2(float a,float b){ return a>b?a:b; }
static float fminf2(float a,float b){ return a<b?a:b; }
static float fabsf2(float a){ return a<0?-a:a; }
static int   iabs2(int a)   { return a<0?-a:a; }

static void drawText(float x,float y,const char* s, void* font){
    if(!font) font=GLUT_BITMAP_HELVETICA_18;
    glRasterPos2f(x,y);
    while(*s) glutBitmapCharacter(font,*s++);
}

static void fmtTime(char* buf,int sz,float t){
    int m=(int)t/60, s=(int)t%60;
    snprintf(buf,sz,"%02d:%02d",m,s);
}

/* ════════════════════════════════════════════════════════════════════════
   STARS / BACKGROUND INIT
   ════════════════════════════════════════════════════════════════════════ */
static void initStars(void) {
    int i;
    for(i=0;i<MAX_STARS;i++){
        stars[i].x=(float)(rand()%WIN_W);
        stars[i].y=(float)(rand()%WIN_H);
        stars[i].br=(float)(rand()%100)/100.f;
        stars[i].spd=(float)(rand()%30+5)/10.f;
    }
}

static void updateMatrix(float dt){
    int i;
    if(gWall!=WALL_MATRIX) return;
    for(i=0;i<MAX_STARS;i++){
        stars[i].y -= stars[i].spd*dt*60.f;
        if(stars[i].y<0){
            stars[i].y=(float)WIN_H;
            stars[i].x=(float)(rand()%WIN_W);
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════
   DRAW BACKGROUND
   ════════════════════════════════════════════════════════════════════════ */
static void drawBG(void) {
    int i;
    /* Base colour */
    switch(gWall){
        case WALL_OCEAN:  SC(0.02f,0.05f,0.18f); break;
        case WALL_FOREST: SC(0.02f,0.08f,0.03f); break;
        case WALL_SUNSET: SC(0.08f,0.02f,0.12f); break;
        case WALL_MATRIX: SC(0.00f,0.03f,0.00f); break;
        default:          SC(0.03f,0.03f,0.10f); break;
    }
    glBegin(GL_QUADS);
    glVertex2f(0,0); glVertex2f(WIN_W,0);
    glVertex2f(WIN_W,WIN_H); glVertex2f(0,WIN_H);
    glEnd();

    /* Theme decorations */
    if(gWall==WALL_SPACE){
        for(i=0;i<MAX_STARS;i++){
            float b=stars[i].br;
            SCA(b,b,b+.1f,1.f);
            glPointSize(b<.5f?1.f:2.f);
            glBegin(GL_POINTS); glVertex2f(stars[i].x,stars[i].y); glEnd();
        }
    } else if(gWall==WALL_OCEAN){
        for(i=0;i<MAX_STARS;i+=3){
            float b=stars[i].br;
            SCA(b*.3f,b*.5f+.3f,b*.8f+.1f, b*.4f);
            midpoint_circle((int)stars[i].x,(int)stars[i].y,(int)(b*5+2));
        }
        SCA(.1f,.3f,.6f,.2f);
        {
            int wx,wy;
            for(wy=50;wy<WIN_H;wy+=60)
                for(wx=0;wx<WIN_W-20;wx+=20){
                    int dy1=(int)(sinf((float)wx*.05f+gMenuTime)*6.f);
                    int dy2=(int)(sinf((float)(wx+20)*.05f+gMenuTime)*6.f);
                    draw_line(wx,wy+dy1,wx+20,wy+dy2);
                }
        }
    } else if(gWall==WALL_FOREST){
        for(i=0;i<MAX_STARS;i++){
            float pulse=sinf(gMenuTime*stars[i].spd+stars[i].x*.05f)*.5f+.5f;
            SCA(.6f,1.f,.3f,pulse*.5f);
            filled_midpoint_circle((int)stars[i].x,(int)stars[i].y,2);
        }
        SC(.01f,.04f,.01f);
        {
            int ti;
            for(ti=0;ti<6;ti++){
                int tx=60+ti*130;
                IVec2 tree[7]={{tx,80},{tx-35,160},{tx-15,160},
                               {tx-20,230},{tx+20,230},{tx+15,160},{tx+35,160}};
                scanline_fill_polygon(tree,7);
            }
        }
    } else if(gWall==WALL_SUNSET){
        float bands[5][3]={{.08f,.02f,.15f},{.18f,.04f,.12f},{.35f,.1f,.05f},{.6f,.22f,.02f},{.9f,.45f,.05f}};
        {
            int bi;
            for(bi=0;bi<5;bi++){
                SC(bands[bi][0],bands[bi][1],bands[bi][2]);
                scanline_fill_rect(0,bi*130,WIN_W,130);
            }
        }
        SC(1.f,.85f,.1f); filled_midpoint_circle(400,80,45);
        SCA(1.f,.95f,.4f,.5f);
        {
            int ki;
            for(ki=0;ki<12;ki++){
                float ang=ki*(float)M_PI/6.f+gMenuTime*.3f;
                draw_thick_line(400,80,(int)(400+cosf(ang)*65),(int)(80+sinf(ang)*65),2);
            }
        }
        for(i=0;i<MAX_STARS;i++){
            float pulse=sinf(gMenuTime*stars[i].spd+stars[i].y*.02f)*.3f+.2f;
            SCA(1.f,.95f,.7f,pulse); draw_pixel((int)stars[i].x,(int)stars[i].y);
        }
    } else if(gWall==WALL_MATRIX){
        char cbuf[2]={0,0};
        for(i=0;i<MAX_STARS;i++){
            float b=stars[i].br*.8f+.2f;
            int ji;
            for(ji=0;ji<4;ji++){
                float fade=1.f-(float)ji*.25f;
                SCA(0.f,b*fade,0.f,b*fade*.6f);
                cbuf[0]=(char)('0'+rand()%10);
                int cy2=(int)(stars[i].y)+ji*12;
                if(cy2>=0&&cy2<WIN_H) drawText(stars[i].x,(float)cy2,cbuf,GLUT_BITMAP_HELVETICA_10);
            }
        }
    }
    glPointSize(1.f);

    /* Border lines */
    switch(gWall){
        case WALL_OCEAN:  SCA(.2f,.6f,.9f,1); break;
        case WALL_FOREST: SCA(.1f,.6f,.1f,1); break;
        case WALL_SUNSET: SCA(.9f,.4f,.1f,1); break;
        case WALL_MATRIX: SCA(.0f,.7f,.0f,1); break;
        default:          SCA(.25f,.25f,.45f,1);
    }
    draw_thick_line((int)PLAY_LEFT,0,(int)PLAY_LEFT,WIN_H,2);
    draw_thick_line((int)PLAY_RIGHT,0,(int)PLAY_RIGHT,WIN_H,2);
    draw_thick_line((int)PLAY_LEFT,WIN_H-2,(int)PLAY_RIGHT,WIN_H-2,3);
}

/* ════════════════════════════════════════════════════════════════════════
   PERK / BLOCK COLOR HELPERS
   ════════════════════════════════════════════════════════════════════════ */
static void perkColor(PerkType t){
    switch(t){
        case PERK_EXTRA_LIFE: SC(0.1f,0.9f,0.2f); break;
        case PERK_SPEED_UP:   SC(1.f,0.9f,0.f);   break;
        case PERK_WIDE_PAD:   SC(0.2f,0.6f,1.f);  break;
        case PERK_FIREBALL:   SC(1.f,0.4f,0.f);   break;
        case PERK_SHRINK_PAD: SC(0.7f,0.1f,0.9f); break;
        case PERK_SLOW_BALL:  SC(0.9f,0.9f,0.9f); break;
        default:              SC(1,1,1);
    }
}
static const char* perkLbl(PerkType t){
    switch(t){
        case PERK_EXTRA_LIFE: return "+L";
        case PERK_SPEED_UP:   return "+S";
        case PERK_WIDE_PAD:   return "+W";
        case PERK_FIREBALL:   return "FB";
        case PERK_SHRINK_PAD: return "-W";
        case PERK_SLOW_BALL:  return "SL";
        default: return "  ";
    }
}
static void blockColor(Block* b){
    if(b->hits==3)      SC(b->r,b->g,b->b);
    else if(b->hits==2) SC(b->r*.75f,b->g*.75f,b->b*.75f);
    else                SC(b->r*.45f,b->g*.45f,b->b*.45f);
}

/* ════════════════════════════════════════════════════════════════════════
   GAME INIT
   ════════════════════════════════════════════════════════════════════════ */
static PerkType randPerk(void){
    int r=rand()%12;
    if(r==0) return PERK_EXTRA_LIFE; if(r==1) return PERK_SPEED_UP;
    if(r==2) return PERK_WIDE_PAD;   if(r==3) return PERK_FIREBALL;
    if(r==4) return PERK_SHRINK_PAD; if(r==5) return PERK_SLOW_BALL;
    return PERK_NONE;
}

static void initLevel(void){
    int r,c;
    float rc[ROWS][3]={{1.f,.2f,.2f},{1.f,.6f,0.f},{1.f,1.f,.1f},
                       {.2f,.9f,.2f},{.2f,.7f,1.f},{.6f,.2f,1.f},{.9f,.3f,.7f}};
    blocksLeft=0;
    for(r=0;r<ROWS;r++) for(c=0;c<COLS;c++){
        Block* b=&blocks[r][c];
        b->x=PLAY_LEFT+c*BLK_W; b->y=BLK_TOP_Y-r*(BLK_H+4.f);
        b->alive=1; b->hits=(r<2)?3:(r<4)?2:1;
        b->perk=randPerk();
        b->r=rc[r][0]; b->g=rc[r][1]; b->b=rc[r][2];
        blocksLeft++;
    }
}

static void resetBall(int stuck){
    float ang=(float)M_PI/2.f+((float)(rand()%40)-20.f)*(float)M_PI/180.f;
    ball.x=padX; ball.y=PAD_Y+PAD_H/2.f+BALL_R+1.f;
    ball.speed=DIFF[gCurDiff].initSpeed; ball.angle=0.f;
    ball.vx=cosf(ang)*ball.speed; ball.vy=sinf(ang)*ball.speed;
    ball.fireball=0; ball.active=stuck;
}

static void saveHS(void){
    HSEntry e;
    int i,j;
    e.score=score; e.time=elapsed; e.diff=gCurDiff;
    strncpy(e.dname,DIFF[gCurDiff].name,7); e.dname[7]='\0';
    if(hsCount<MAX_HS) highScores[hsCount++]=e;
    else if(e.score>highScores[MAX_HS-1].score) highScores[MAX_HS-1]=e;
    for(i=1;i<hsCount;i++){
        HSEntry key=highScores[i];
        for(j=i-1;j>=0&&highScores[j].score<key.score;j--)
            highScores[j+1]=highScores[j];
        highScores[j+1]=key;
    }
}

static void startGame(void){
    gCurDiff=gDiffSel;
    lives=3; score=0; elapsed=0.f;
    wideTimer=narrowTimer=fireTimer=0.f; padSquash=1.f;
    padWidth=DIFF[gCurDiff].padW;
    perkCount=0; initLevel(); resetBall(1);
    gState=STATE_PLAYING;
}

/* ════════════════════════════════════════════════════════════════════════
   APPLY PERK
   ════════════════════════════════════════════════════════════════════════ */
static void applyPerk(PerkType t){
    float l;
    playSound(SND_PERK);
    switch(t){
        case PERK_EXTRA_LIFE: lives++; score+=50; break;
        case PERK_SPEED_UP:
            ball.speed=fminf2(ball.speed*1.3f,DIFF[gCurDiff].maxSpeed);
            l=sqrtf(ball.vx*ball.vx+ball.vy*ball.vy);
            if(l>0){ball.vx=ball.vx/l*ball.speed; ball.vy=ball.vy/l*ball.speed;}
            score+=20; break;
        case PERK_WIDE_PAD:   wideTimer=15.f;narrowTimer=0.f;padWidth=PAD_WIDE;  break;
        case PERK_FIREBALL:   fireTimer=12.f; ball.fireball=1; break;
        case PERK_SHRINK_PAD: narrowTimer=10.f;wideTimer=0.f;padWidth=PAD_NARROW; break;
        case PERK_SLOW_BALL:
            ball.speed=fmaxf2(ball.speed*.7f,DIFF[gCurDiff].initSpeed*.6f);
            l=sqrtf(ball.vx*ball.vx+ball.vy*ball.vy);
            if(l>0){ball.vx=ball.vx/l*ball.speed; ball.vy=ball.vy/l*ball.speed;}
            break;
        default: break;
    }
}

/* ════════════════════════════════════════════════════════════════════════
   DRAW GAME OBJECTS (Blocks now use scanline_fill_rect, no flood/boundary fill)
   ════════════════════════════════════════════════════════════════════════ */

/* ── Paddle ──────────────────────────────────────────────────────────────── */
static void drawPaddle(void){
    float px=padX, py=PAD_Y;
    float hw=padWidth/2.f, hh=PAD_H/2.f;
    float sy2=padSquash;
    float sx2=1.f+(1.f-padSquash)*.5f;
    Vec2 body[4]={{-hw,-hh},{hw,-hh},{hw,hh},{-hw,hh}};
    Vec2 hi[4]={{-hw,hh*.1f},{hw,hh*.1f},{hw,hh},{-hw,hh}};
    SC(.2f,.65f,.95f);
    draw_transformed_poly(body,4,px,py,0.f,sx2,sy2);
    SC(.5f,.85f,1.f);
    draw_transformed_poly(hi,4,px,py,0.f,sx2,sy2);
    SC(.8f,.95f,1.f);
    draw_transformed_poly_outline(body,4,px,py,0.f,sx2,sy2);
}

/* ── Ball ────────────────────────────────────────────────────────────────── */
static void drawBall(void){
    int cx=(int)ball.x, cy=(int)ball.y, r=(int)BALL_R;
    int k;
    glPointSize(1.f);
    if(ball.fireball){
        SCA(1.f,.3f,0.f,.4f); midpoint_circle(cx,cy,r*2);
        SCA(1.f,.6f,0.f,.7f); midpoint_circle(cx,cy,(int)(r*1.5f));
        SC(1.f,.8f,.1f);
        for(k=0;k<4;k++){
            float ang=ball.angle+k*(float)M_PI/2.f;
            int x2=cx+(int)(cosf(ang)*(r+5));
            int y2=cy+(int)(sinf(ang)*(r+5));
            draw_line(cx,cy,x2,y2);
        }
        SC(1.f,.95f,.2f); filled_midpoint_circle(cx,cy,r);
    } else {
        SCA(.1f,.1f,.2f,.5f); filled_midpoint_circle(cx-2,cy-2,r);
        SC(.9f,.9f,.95f);     filled_midpoint_circle(cx,cy,r);
        SC(1,1,1); filled_midpoint_circle(cx+(int)(r*.3f),cy+(int)(r*.3f),(int)(r*.3f));
    }
}

/* ── Blocks – now uses simple scanline_fill_rect (no flood/boundary fill) ── */
static void drawBlocks(void){
    int row,col;
    for(row=0;row<ROWS;row++) for(col=0;col<COLS;col++){
        Block* b=&blocks[row][col]; if(!b->alive) continue;
        int bx=(int)(b->x+1), by=(int)(b->y-BLK_H+1);
        int bw=(int)(BLK_W-2), bh=(int)(BLK_H-2);

        /* fill block with its current colour (depending on hits) */
        blockColor(b);
        scanline_fill_rect(bx,by,bw,bh);

        /* border */
        SCA(0,0,0,.9f);
        draw_line(bx,by,bx+bw,by);
        draw_line(bx+bw,by,bx+bw,by+bh);
        draw_line(bx+bw,by+bh,bx,by+bh);
        draw_line(bx,by+bh,bx,by);

        /* perk indicator dot */
        if(b->perk!=PERK_NONE){
            perkColor(b->perk);
            filled_midpoint_circle((int)(b->x+BLK_W/2.f),(int)(b->y-BLK_H/2.f),4);
        }
    }
}

/* ── Perk items ─────────────────────────────────────────────────────────── */
static void drawPerkItems(void){
    int i;
    for(i=0;i<perkCount;i++){
        Perk* p=&perks[i]; if(!p->alive) continue;
        float hw=PERK_W/2.f, hh=PERK_H/2.f;
        Vec2 diamond[4]={{0,hh},{hw,0},{0,-hh},{-hw,0}};
        perkColor(p->type);
        glPointSize(1.f);
        draw_transformed_poly(diamond,4,p->x,p->y,p->rot,1.f,1.f);
        SCA(1,1,1,.6f);
        draw_transformed_poly_outline(diamond,4,p->x,p->y,p->rot,1.f,1.f);
        SC(0,0,0);
        drawText(p->x-7,p->y-4,perkLbl(p->type),GLUT_BITMAP_HELVETICA_10);
    }
}

/* ── HUD ─────────────────────────────────────────────────────────────────── */
static void drawHUD(void){
    char buf[64]; int i;
    float tx;
    SC(.05f,.05f,.12f);
    glBegin(GL_QUADS);
    glVertex2f(PLAY_LEFT,PLAY_BOTTOM); glVertex2f(PLAY_RIGHT,PLAY_BOTTOM);
    glVertex2f(PLAY_RIGHT,PLAY_BOTTOM+26.f); glVertex2f(PLAY_LEFT,PLAY_BOTTOM+26.f);
    glEnd();
    SC(.3f,.5f,.8f);
    draw_thick_line((int)PLAY_LEFT,(int)(PLAY_BOTTOM+27),(int)PLAY_RIGHT,(int)(PLAY_BOTTOM+27),1);

    SC(.6f,.85f,1.f);
    snprintf(buf,sizeof(buf),"Score:%d",score);
    drawText(15,18,buf,GLUT_BITMAP_HELVETICA_12);
    {char tb[16]; fmtTime(tb,sizeof(tb),elapsed);
     snprintf(buf,sizeof(buf),"Time:%s",tb);}
    drawText(150,18,buf,GLUT_BITMAP_HELVETICA_12);

    if(gCurDiff==0) SC(.2f,.9f,.2f);
    else if(gCurDiff==1) SC(1.f,.85f,0.f);
    else SC(1.f,.2f,.2f);
    snprintf(buf,sizeof(buf),"[%s]",DIFF[gCurDiff].name);
    drawText(285,18,buf,GLUT_BITMAP_HELVETICA_12);

    SC(.6f,.85f,1.f); drawText(370,18,"Lives:",GLUT_BITMAP_HELVETICA_12);
    for(i=0;i<lives;i++){
        SC(1.f,.3f,.3f); filled_midpoint_circle(415+i*20,18,6);
        SC(1.f,.6f,.6f); midpoint_circle(415+i*20,18,6);
    }

    tx=495.f;
    if(wideTimer>0){
        SC(.2f,.6f,1.f);
        snprintf(buf,sizeof(buf),"W:%ds",(int)wideTimer+1);
        drawText(tx,18,buf,GLUT_BITMAP_HELVETICA_12); tx+=68.f;
    }
    if(narrowTimer>0){
        SC(.7f,.1f,.9f);
        snprintf(buf,sizeof(buf),"N:%ds",(int)narrowTimer+1);
        drawText(tx,18,buf,GLUT_BITMAP_HELVETICA_12); tx+=68.f;
    }
    if(fireTimer>0){
        SC(1.f,.5f,0.f);
        snprintf(buf,sizeof(buf),"F:%ds",(int)fireTimer+1);
        drawText(tx,18,buf,GLUT_BITMAP_HELVETICA_12);
    }

    SC(.4f,.4f,.6f);
    snprintf(buf,sizeof(buf),"BG:%s",WALL_NAMES[gWall]);
    drawText(680,18,buf,GLUT_BITMAP_HELVETICA_10);
}

/* ════════════════════════════════════════════════════════════════════════
   MENU SCREENS
   ════════════════════════════════════════════════════════════════════════ */
static void drawMenuBall(float t){
    float mx=400+cosf(t*.8f)*60.f, my=425+sinf(t*1.2f)*20.f;
    SCA(.2f,.2f,.4f,.5f); filled_midpoint_circle((int)mx,(int)my,(int)(BALL_R*2.2f));
    SC(.9f,.9f,.95f);     filled_midpoint_circle((int)mx,(int)my,(int)BALL_R);
    SC(1,1,1); filled_midpoint_circle((int)(mx+BALL_R*.3f),(int)(my+BALL_R*.3f),(int)(BALL_R*.3f));
}

static void drawMenu(void){
    char buf[32];
    drawBG();
    SC(.3f,.8f,1.f);  drawText(235,535,"DX  BALL",GLUT_BITMAP_TIMES_ROMAN_24);
    SC(.6f,.9f,1.f);  drawText(185,510,"Prepared by Sadia Islam Khan & Sumaya Akhter",GLUT_BITMAP_HELVETICA_18);
    drawMenuBall(gMenuTime);

    {
        const char* items[4]={"  START GAME  ","  HIGH SCORE  "," BACKGROUND  ","     EXIT     "};
        int i;
        for(i=0;i<4;i++){
            float iy=365.f-i*57.f;
            if(i==gMenuItem){
                SC(.2f,.5f,.9f); scanline_fill_rect(228,(int)(iy-8),344,36);
                SC(.7f,.9f,1.f);
                draw_thick_line(228,(int)(iy-8),572,(int)(iy-8),2);
                draw_thick_line(572,(int)(iy-8),572,(int)(iy+28),2);
                draw_thick_line(572,(int)(iy+28),228,(int)(iy+28),2);
                draw_thick_line(228,(int)(iy+28),228,(int)(iy-8),2);
                SC(1,1,1);
            } else SC(.4f,.6f,.85f);
            drawText(268,iy+8,items[i],GLUT_BITMAP_HELVETICA_18);
        }
    }
    SC(.4f,.5f,.7f);
    drawText(110,95,"Up/Down: Navigate    Enter/Space: Select    B: Change BG    ESC: Exit",GLUT_BITMAP_HELVETICA_12);
    SC(.3f,.3f,.5f);
    drawText(70,75,"Algorithms: Bresenham | Midpoint Circle | Scanline Fill | 2D Transform",GLUT_BITMAP_HELVETICA_10);
    SC(.4f,.7f,.4f);
    snprintf(buf,sizeof(buf),"Current BG: %s",WALL_NAMES[gWall]);
    drawText(300,55,buf,GLUT_BITMAP_HELVETICA_12);
}

static void drawDiffSelect(void){
    int i;
    const char* desc[3]={
        "Slow ball, wide paddle. Great for beginners!",
        "Balanced speed & paddle. The classic experience.",
        "Fast ball, narrow paddle. For hardcore players!"
    };
    float dc[3][3]={{.2f,.9f,.2f},{1.f,.85f,0.f},{1.f,.25f,.25f}};
    drawBG();
    SC(1.f,.85f,.2f); drawText(235,548,"SELECT  DIFFICULTY",GLUT_BITMAP_TIMES_ROMAN_24);
    for(i=0;i<3;i++){
        char st[80]; float iy=440.f-i*130.f;
        int sel=(i==gDiffSel);
        SC(dc[i][0]*.25f,dc[i][1]*.25f,dc[i][2]*.25f);
        scanline_fill_rect(170,(int)(iy-68),460,98);
        SC(dc[i][0],dc[i][1],dc[i][2]);
        if(sel) draw_thick_line(170,(int)(iy-68),630,(int)(iy-68),3);
        else    draw_line(170,(int)(iy-68),630,(int)(iy-68));
        draw_line(630,(int)(iy-68),630,(int)(iy+30));
        if(sel) draw_thick_line(630,(int)(iy+30),170,(int)(iy+30),3);
        else    draw_line(630,(int)(iy+30),170,(int)(iy+30));
        draw_line(170,(int)(iy+30),170,(int)(iy-68));
        SC(dc[i][0],dc[i][1],dc[i][2]);
        drawText(205,iy+12,DIFF[i].name,GLUT_BITMAP_TIMES_ROMAN_24);
        SC(.75f,.75f,.9f);
        snprintf(st,sizeof(st),"Init speed: %.0f px/s    Paddle: %.0f px",DIFF[i].initSpeed,DIFF[i].padW);
        drawText(205,iy-14,st,GLUT_BITMAP_HELVETICA_12);
        SC(.55f,.55f,.75f); drawText(205,iy-36,desc[i],GLUT_BITMAP_HELVETICA_12);
        if(sel){ SC(1,1,1); drawText(138,iy+12,">>>",GLUT_BITMAP_HELVETICA_18); }
    }
    SC(.5f,.6f,.8f);
    drawText(155,40,"Up/Down: Choose    Enter/Space: Start Game    ESC: Back",GLUT_BITMAP_HELVETICA_12);
}

static void drawHighScorePage(void){
    int i;
    drawBG();
    SC(1.f,.85f,.2f); drawText(250,592,"HIGH  SCORES",GLUT_BITMAP_TIMES_ROMAN_24);
    SC(1.f,.8f,0.f);
    draw_thick_line(382,570,418,570,6);
    draw_thick_line(388,558,412,558,8);
    draw_thick_line(388,542,412,542,8);
    draw_thick_line(395,542,395,556,2);
    draw_thick_line(405,542,405,556,2);
    if(hsCount==0){
        SC(.55f,.55f,.8f);
        drawText(215,390,"No scores yet - play a game first!",GLUT_BITMAP_HELVETICA_18);
    } else {
        SC(.4f,.7f,1.f);
        drawText(55,528,"RANK",GLUT_BITMAP_HELVETICA_12);
        drawText(145,528,"SCORE",GLUT_BITMAP_HELVETICA_12);
        drawText(270,528,"TIME",GLUT_BITMAP_HELVETICA_12);
        drawText(380,528,"DIFFICULTY",GLUT_BITMAP_HELVETICA_12);
        SC(.25f,.35f,.55f); draw_thick_line(48,520,752,520,1);
        int n=hsCount<10?hsCount:10;
        for(i=0;i<n;i++){
            char buf[32]; float ry=495.f-i*46.f;
            HSEntry* e=&highScores[i];
            if(i==0){ SCA(1.f,.85f,0.f,.09f); scanline_fill_rect(48,(int)(ry-10),704,40); }
            else if(i==1){ SCA(.7f,.7f,.8f,.06f); scanline_fill_rect(48,(int)(ry-10),704,40); }
            else if(i==2){ SCA(.8f,.5f,.2f,.06f); scanline_fill_rect(48,(int)(ry-10),704,40); }
            float mr=.8f,mg=.8f,mb=.8f;
            if(i==0){mr=1.f;mg=.85f;mb=0.f;}
            else if(i==1){mr=.78f;mg=.78f;mb=.82f;}
            else if(i==2){mr=.82f;mg=.52f;mb=.22f;}
            SC(mr,mg,mb);
            snprintf(buf,sizeof(buf),"#%d",i+1);
            drawText(58,ry+8,buf,GLUT_BITMAP_HELVETICA_18);
            SC(1,1,1); snprintf(buf,sizeof(buf),"%d",e->score);
            drawText(145,ry+8,buf,GLUT_BITMAP_HELVETICA_18);
            SC(.7f,.9f,1.f);
            {char tb[16]; fmtTime(tb,sizeof(tb),e->time); drawText(270,ry+8,tb,GLUT_BITMAP_HELVETICA_18);}
            float br=1,bg2=1,bb2=1;
            if(e->diff==0){br=.2f;bg2=.9f;bb2=.2f;}
            else if(e->diff==1){br=1.f;bg2=.85f;bb2=0.f;}
            else{br=1.f;bg2=.25f;bb2=.25f;}
            SC(br*.2f,bg2*.2f,bb2*.2f); scanline_fill_rect(374,(int)(ry-2),110,28);
            SC(br,bg2,bb2);
            draw_line(374,(int)(ry-2),484,(int)(ry-2)); draw_line(484,(int)(ry-2),484,(int)(ry+26));
            draw_line(484,(int)(ry+26),374,(int)(ry+26)); draw_line(374,(int)(ry+26),374,(int)(ry-2));
            drawText(382,ry+8,e->dname,GLUT_BITMAP_HELVETICA_12);
        }
    }
    SC(.4f,.5f,.7f); drawText(285,22,"ESC - Back to Menu",GLUT_BITMAP_HELVETICA_12);
}

static void drawWallpaperSelect(void){
    int i;
    float previewBG[WALL_COUNT][3]={
        {.03f,.03f,.10f},{.02f,.05f,.18f},{.02f,.08f,.03f},{.25f,.05f,.08f},{.00f,.08f,.00f}
    };
    float accent[WALL_COUNT][3]={
        {.4f,.6f,1.f},{.2f,.6f,.9f},{.1f,.8f,.1f},{.9f,.4f,.1f},{.0f,.9f,.0f}
    };
    const char* desc[WALL_COUNT]={
        "Classic dark space with twinkling stars",
        "Deep ocean with bubbles and wave lines",
        "Dark forest with glowing fireflies & trees",
        "Vivid sunset gradient with sun rays",
        "Iconic matrix digital rain effect",
    };
    drawBG();
    SC(1.f,.85f,.2f); drawText(220,575,"SELECT  BACKGROUND",GLUT_BITMAP_TIMES_ROMAN_24);
    for(i=0;i<WALL_COUNT;i++){
        float iy=490.f-i*95.f;
        int sel=(i==(int)gWall);
        SC(previewBG[i][0],previewBG[i][1],previewBG[i][2]);
        scanline_fill_rect(50,(int)(iy-42),120,72);
        if(sel){
            SC(accent[i][0],accent[i][1],accent[i][2]);
            draw_thick_line(50,(int)(iy-42),170,(int)(iy-42),3);
            draw_thick_line(170,(int)(iy-42),170,(int)(iy+30),3);
            draw_thick_line(170,(int)(iy+30),50,(int)(iy+30),3);
            draw_thick_line(50,(int)(iy+30),50,(int)(iy-42),3);
            SC(1,1,1); drawText(30,iy+5,">>",GLUT_BITMAP_HELVETICA_18);
        } else {
            SC(accent[i][0]*.5f,accent[i][1]*.5f,accent[i][2]*.5f);
            draw_line(50,(int)(iy-42),170,(int)(iy-42));
            draw_line(170,(int)(iy-42),170,(int)(iy+30));
            draw_line(170,(int)(iy+30),50,(int)(iy+30));
            draw_line(50,(int)(iy+30),50,(int)(iy-42));
        }
        SC(accent[i][0],accent[i][1],accent[i][2]);
        drawText(185,iy+8,WALL_NAMES[i],GLUT_BITMAP_TIMES_ROMAN_24);
        SC(.7f,.7f,.9f); drawText(185,iy-14,desc[i],GLUT_BITMAP_HELVETICA_12);
        if(sel){ SC(.2f,1.f,.5f); drawText(185,iy-32,"[ACTIVE - Live Preview!]",GLUT_BITMAP_HELVETICA_10); }
    }
    SC(.5f,.6f,.8f);
    drawText(100,22,"Up/Down: Choose    Enter/Space: Confirm & Back    ESC: Back",GLUT_BITMAP_HELVETICA_12);
}

static void drawOverlay(void){
    int k;
    SCA(0,0,0,.58f); scanline_fill_rect(0,0,WIN_W,WIN_H);
    if(gState==STATE_PAUSED){
        SC(1.f,.9f,.2f); drawText(310,395,"PAUSED",GLUT_BITMAP_TIMES_ROMAN_24);
        SC(.8f,.8f,1.f);
        drawText(100,345,"P - Resume    ESC - Main Menu    B - Change Background",GLUT_BITMAP_HELVETICA_18);
    } else if(gState==STATE_WIN){
        SCA(.2f,1.f,.3f,.6f);
        for(k=0;k<8;k++){
            float ang=k*(float)M_PI/4.f+gMenuTime;
            draw_thick_line(400,380,(int)(400+cosf(ang)*55.f),(int)(380+sinf(ang)*55.f),2);
        }
        SC(.2f,1.f,.4f); drawText(278,425,"YOU  WIN!!!",GLUT_BITMAP_TIMES_ROMAN_24);
        SC(.9f,.9f,1.f);
        {char tb[16],buf[64]; fmtTime(tb,sizeof(tb),elapsed);
         snprintf(buf,sizeof(buf),"Score: %d     Time: %s",score,tb);
         drawText(185,378,buf,GLUT_BITMAP_HELVETICA_18);}
        drawText(175,332,"Enter/Space - New Game    ESC - Menu",GLUT_BITMAP_HELVETICA_18);
    } else if(gState==STATE_LOSE){
        SCA(1.f,.1f,.1f,.5f);
        draw_thick_line(350,395,450,455,4);
        draw_thick_line(450,395,350,455,4);
        SC(1.f,.2f,.2f); drawText(268,425,"GAME  OVER",GLUT_BITMAP_TIMES_ROMAN_24);
        SC(.9f,.9f,1.f);
        {char tb[16],buf[64]; fmtTime(tb,sizeof(tb),elapsed);
         snprintf(buf,sizeof(buf),"Score: %d     Time: %s",score,tb);
         drawText(190,378,buf,GLUT_BITMAP_HELVETICA_18);}
        drawText(175,332,"Enter/Space - New Game    ESC - Menu",GLUT_BITMAP_HELVETICA_18);
    }
}

/* ════════════════════════════════════════════════════════════════════════
   PHYSICS UPDATE
   ════════════════════════════════════════════════════════════════════════ */
static void update(float dt){
    int i,r,c;
    gMenuTime+=dt;
    updateMatrix(dt);
    if(padSquash<1.f) padSquash=fminf2(1.f,padSquash+dt*4.f);
    for(i=0;i<perkCount;i++) perks[i].rot+=dt*2.f;
    ball.angle+=dt*3.f;
    if(gState!=STATE_PLAYING) return;
    elapsed+=dt;

    if(wideTimer>0){   wideTimer-=dt;   if(wideTimer<=0){   wideTimer=0; padWidth=(narrowTimer>0)?PAD_NARROW:DIFF[gCurDiff].padW;} }
    if(narrowTimer>0){ narrowTimer-=dt; if(narrowTimer<=0){ narrowTimer=0; padWidth=(wideTimer>0)?PAD_WIDE:DIFF[gCurDiff].padW;} }
    if(fireTimer>0){   fireTimer-=dt;   if(fireTimer<=0){   fireTimer=0; ball.fireball=0;} }

    ball.speed=fminf2(ball.speed+DIFF[gCurDiff].incRate*dt,DIFF[gCurDiff].maxSpeed);
    if(keyLeft)  padX-=PAD_SPEED*dt;
    if(keyRight) padX+=PAD_SPEED*dt;
    {float hw=padWidth/2.f; padX=fmaxf2(PLAY_LEFT+hw,fminf2(PLAY_RIGHT-hw,padX));}

    if(ball.active){ ball.x=padX; ball.y=PAD_Y+PAD_H/2.f+BALL_R+1.f; return; }

    ball.x+=ball.vx*dt; ball.y+=ball.vy*dt;

    if(ball.x-BALL_R<PLAY_LEFT){  ball.x=PLAY_LEFT+BALL_R;  ball.vx= fabsf2(ball.vx); playSound(SND_WALL); }
    if(ball.x+BALL_R>PLAY_RIGHT){ ball.x=PLAY_RIGHT-BALL_R; ball.vx=-fabsf2(ball.vx); playSound(SND_WALL); }
    if(ball.y+BALL_R>PLAY_TOP){   ball.y=PLAY_TOP-BALL_R;   ball.vy=-fabsf2(ball.vy); playSound(SND_WALL); }

    /* Paddle */
    {
        float px=padX-padWidth/2.f, py=PAD_Y-PAD_H/2.f;
        if(ball.y-BALL_R<py+PAD_H && ball.y+BALL_R>py &&
           ball.x+BALL_R>px && ball.x-BALL_R<px+padWidth && ball.vy<0){
            float rel=(ball.x-padX)/(padWidth/2.f);
            float ang=rel*65.f*(float)M_PI/180.f;
            float spd=ball.speed;
            ball.vx=sinf(ang)*spd; ball.vy=cosf(ang)*spd;
            ball.y=py+PAD_H+BALL_R+1.f;
            padSquash=0.55f; playSound(SND_PADDLE);
        }
    }

    /* Ball lost */
    if(ball.y-BALL_R<PLAY_BOTTOM){
        playSound(SND_DEATH); lives--;
        if(lives<=0){ saveHS(); gState=STATE_LOSE; }
        else resetBall(1);
        return;
    }

    /* Block collision */
    for(r=0;r<ROWS;r++) for(c=0;c<COLS;c++){
        Block* b=&blocks[r][c]; if(!b->alive) continue;
        float bx=b->x, by=b->y-BLK_H, bw=BLK_W-2.f, bh=BLK_H-2.f;
        float cx2=fmaxf2(bx+1.f,fminf2(ball.x,bx+1.f+bw));
        float cy2=fmaxf2(by+1.f,fminf2(ball.y,by+1.f+bh));
        float dx=ball.x-cx2, dy=ball.y-cy2;
        if(dx*dx+dy*dy>BALL_R*BALL_R) continue;

        b->hits=ball.fireball?0:b->hits-1;
        score+=10*(4-(b->hits<0?0:b->hits));
        playSound(SND_BLOCK);

        if(b->hits<=0){
            b->alive=0; blocksLeft--; score+=5;
            if(b->perk!=PERK_NONE && perkCount<MAX_PERKS){
                Perk np;
                np.x=b->x+BLK_W/2.f; np.y=b->y-BLK_H/2.f;
                np.rot=0.f; np.type=b->perk; np.alive=1;
                perks[perkCount++]=np;
            }
        }
        if(!ball.fireball){
            float ox=(dx>0)?(bx+1+bw-(ball.x-BALL_R)):((ball.x+BALL_R)-(bx+1));
            float oy=(dy>0)?(by+1+bh-(ball.y-BALL_R)):((ball.y+BALL_R)-(by+1));
            if(fabsf2(ox)<fabsf2(oy)) ball.vx=-ball.vx; else ball.vy=-ball.vy;
        }
        {float l=sqrtf(ball.vx*ball.vx+ball.vy*ball.vy);
         if(l>0){ball.vx=ball.vx/l*ball.speed; ball.vy=ball.vy/l*ball.speed;}}
        if(!ball.fireball) goto done;
    }
    done:;

    /* Perk items */
    for(i=0;i<perkCount;i++){
        Perk* p=&perks[i]; if(!p->alive) continue;
        p->y-=PERK_FALL*dt;
        if(p->y-PERK_H/2<PAD_Y+PAD_H/2 && p->y+PERK_H/2>PAD_Y-PAD_H/2 &&
           p->x+PERK_W/2>padX-padWidth/2.f && p->x-PERK_W/2<padX+padWidth/2.f){
            p->alive=0; applyPerk(p->type);
        }
        if(p->y<PLAY_BOTTOM) p->alive=0;
    }

    if(blocksLeft<=0){ saveHS(); playSound(SND_WIN); gState=STATE_WIN; }
}

/* ════════════════════════════════════════════════════════════════════════
   GLUT CALLBACKS
   ════════════════════════════════════════════════════════════════════════ */
void display(void){
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glPointSize(1.f);
    switch(gState){
        case STATE_MENU:        drawMenu();           break;
        case STATE_DIFF_SELECT: drawDiffSelect();     break;
        case STATE_HIGHSCORE:   drawHighScorePage();  break;
        case STATE_WALLPAPER:   drawWallpaperSelect();break;
        default:
            drawBG();
            drawBlocks(); drawPerkItems(); drawPaddle(); drawBall(); drawHUD();
            if(gState==STATE_PAUSED||gState==STATE_WIN||gState==STATE_LOSE)
                drawOverlay();
    }
    glutSwapBuffers();
}

void reshape(int w,int h){
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluOrtho2D(0,WIN_W,0,WIN_H);
    glMatrixMode(GL_MODELVIEW);
}

void idle(void){
    float t=glutGet(GLUT_ELAPSED_TIME)/1000.f;
    float dt=t-lastTime; lastTime=t;
    if(dt>.05f)dt=.05f;
    update(dt);
    glutPostRedisplay();
}

void keyboard(unsigned char key,int x,int y){
    (void)x;(void)y;
    if(key=='b'||key=='B'){
        gWall=(WallTheme)((gWall+1)%WALL_COUNT);
        return;
    }
    if(key==27){
        if(gState==STATE_PLAYING||gState==STATE_PAUSED) gState=STATE_MENU;
        else if(gState==STATE_DIFF_SELECT)  gState=STATE_MENU;
        else if(gState==STATE_HIGHSCORE)    gState=STATE_MENU;
        else if(gState==STATE_WALLPAPER)    gState=STATE_MENU;
        else if(gState==STATE_WIN||gState==STATE_LOSE) gState=STATE_MENU;
        return;
    }
    if(key==' '||key==13){
        if(gState==STATE_MENU){
            if(gMenuItem==0)      gState=STATE_DIFF_SELECT;
            else if(gMenuItem==1) gState=STATE_HIGHSCORE;
            else if(gMenuItem==2) gState=STATE_WALLPAPER;
            else                  exit(0);
        } else if(gState==STATE_DIFF_SELECT){
            startGame();
        } else if(gState==STATE_WALLPAPER){
            gState=STATE_MENU;
        } else if(gState==STATE_PLAYING && ball.active){
            ball.active=0;
        } else if(gState==STATE_WIN||gState==STATE_LOSE){
            gState=STATE_DIFF_SELECT;
        }
        return;
    }
    if(key=='p'||key=='P'){
        if(gState==STATE_PLAYING)       gState=STATE_PAUSED;
        else if(gState==STATE_PAUSED)   gState=STATE_PLAYING;
    }
}

void specialKey(int key,int x,int y){
    (void)x;(void)y;
    if(gState==STATE_MENU){
        if(key==GLUT_KEY_DOWN) gMenuItem=(gMenuItem+1)%4;
        if(key==GLUT_KEY_UP)   gMenuItem=(gMenuItem+3)%4;
        return;
    }
    if(gState==STATE_DIFF_SELECT){
        if(key==GLUT_KEY_UP)   gDiffSel=(gDiffSel+2)%3;
        if(key==GLUT_KEY_DOWN) gDiffSel=(gDiffSel+1)%3;
        return;
    }
    if(gState==STATE_WALLPAPER){
        if(key==GLUT_KEY_UP)   gWall=(WallTheme)((gWall+WALL_COUNT-1)%WALL_COUNT);
        if(key==GLUT_KEY_DOWN) gWall=(WallTheme)((gWall+1)%WALL_COUNT);
        return;
    }
    if(key==GLUT_KEY_LEFT)  keyLeft=1;
    if(key==GLUT_KEY_RIGHT) keyRight=1;
}

void specialKeyUp(int key,int x,int y){
    (void)x;(void)y;
    if(key==GLUT_KEY_LEFT)  keyLeft=0;
    if(key==GLUT_KEY_RIGHT) keyRight=0;
}

void mouseMove(int x,int y){
    (void)y;
    if(gState==STATE_PLAYING){
        float hw=padWidth/2.f;
        padX=(float)x;
        padX=fmaxf2(PLAY_LEFT+hw,fminf2(PLAY_RIGHT-hw,padX));
    }
}

void mouseClick(int btn,int state,int x,int y){
    (void)x;(void)y;
    if(btn==GLUT_LEFT_BUTTON && state==GLUT_DOWN)
        if(gState==STATE_PLAYING && ball.active) ball.active=0;
}

/* ════════════════════════════════════════════════════════════════════════
   MAIN
   ════════════════════════════════════════════════════════════════════════ */
int main(int argc,char** argv){
    pthread_t sndT;
    srand((unsigned int)time(NULL));
    initStars();
    pthread_create(&sndT,NULL,sndThread,NULL);

    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGB);
    glutInitWindowSize(WIN_W,WIN_H);
    glutInitWindowPosition(100,80);
    glutCreateWindow("DX Ball v4  [Bresenham | Midpoint Circle | Scanline Fill | 2D Transform]");
    glClearColor(.03f,.03f,.10f,1.f);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKey);
    glutSpecialUpFunc(specialKeyUp);
    glutPassiveMotionFunc(mouseMove);
    glutMotionFunc(mouseMove);
    glutMouseFunc(mouseClick);

    lastTime=glutGet(GLUT_ELAPSED_TIME)/1000.f;
    glutMainLoop();

    g_sndRun=0;
    pthread_cond_signal(&g_sndCond);
    pthread_join(sndT,NULL);
    return 0;
}
