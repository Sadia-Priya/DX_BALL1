#include <GL/glut.h>       // Include OpenGL GLUT library for window and drawing
#include <math.h>           // Include math library for sin, cos, sqrt functions
#include <stdlib.h>         // Include standard library for rand, exit functions
#include <time.h>           // Include time library to get current time (for random seed)
#include <string.h>         // Include string library for memset, memcpy, strncpy
#include <stdio.h>          // Include standard input/output for printf, snprintf
#include <pthread.h>        // Include POSIX thread library for sound background thread

#ifndef M_PI
#define M_PI 3.14159265358979323846  // Define PI value if not already defined
#endif

/* ── Window ─────────────────────────────────────────────────────────────── */
#define WIN_W 800   // Window width = 800 pixels
#define WIN_H 650   // Window height = 650 pixels

/* ── Play area ──────────────────────────────────────────────────────────── */
#define PLAY_LEFT   10.f    // Left boundary of the game area
#define PLAY_RIGHT  790.f   // Right boundary of the game area
#define PLAY_TOP    640.f   // Top boundary of the game area
#define PLAY_BOTTOM 10.f    // Bottom boundary of the game area

/* ── Paddle ─────────────────────────────────────────────────────────────── */
#define PAD_H      14.f     // Paddle height in pixels
#define PAD_Y      38.f     // Paddle vertical position from bottom
#define PAD_WIDE   175.f    // Wide paddle size (when wide perk is active)
#define PAD_NARROW 65.f     // Narrow paddle size (when shrink perk is active)
#define PAD_SPEED  420.f    // How fast the paddle moves left/right

/* ── Ball ───────────────────────────────────────────────────────────────── */
#define BALL_R 8.f  // Radius of the ball in pixels

/* ── Blocks ─────────────────────────────────────────────────────────────── */
#define COLS     12                                    // Number of block columns
#define ROWS     7                                     // Number of block rows
#define BLK_W    ((PLAY_RIGHT - PLAY_LEFT) / COLS)    // Width of each block (auto calculated)
#define BLK_H    34.f                                  // Height of each block
#define BLK_TOP_Y (PLAY_TOP - 10.f)                   // Y position where blocks start

/* ── Perk item ──────────────────────────────────────────────────────────── */
#define PERK_W    22.f      // Width of falling perk item
#define PERK_H    14.f      // Height of falling perk item
#define PERK_FALL 170.f     // Speed at which perk falls downward
#define MAX_PERKS  128      // Maximum number of perks that can exist at once

/* ── Max array sizes (dynamic vector)*/
#define MAX_STARS  150      // Maximum number of background stars/particles
#define MAX_HS     10       // Maximum number of high score entries to store
#define MAX_PTS    32       // Maximum polygon vertices for drawing shapes

/* ── Flood fill stack size ──────────────────────────────────────────────── */
#define FILL_STACK 65536    // Size of the stack used for flood fill algorithm

/* ════════════════════════════════════════════════════════════════════════
   Enums
   ════════════════════════════════════════════════════════════════════════ */
// All possible game screens/states
typedef enum { STATE_MENU, STATE_DIFF_SELECT, STATE_PLAYING,
               STATE_PAUSED, STATE_HIGHSCORE, STATE_WIN, STATE_LOSE,
               STATE_WALLPAPER } GameState;

// All possible power-up types that can drop from blocks
typedef enum { PERK_NONE=0, PERK_EXTRA_LIFE, PERK_SPEED_UP,
               PERK_WIDE_PAD, PERK_FIREBALL, PERK_SHRINK_PAD,
               PERK_SLOW_BALL } PerkType;

// All possible sound effects in the game
typedef enum { SND_WALL, SND_PADDLE, SND_BLOCK,
               SND_PERK, SND_DEATH, SND_WIN } SoundType;

// Background theme options the player can choose
typedef enum { WALL_SPACE=0, WALL_OCEAN, WALL_FOREST,
               WALL_SUNSET, WALL_MATRIX, WALL_COUNT } WallTheme;

/* ════════════════════════════════════════════════════════════════════════
   Structs
   ════════════════════════════════════════════════════════════════════════ */
typedef struct { float x,y,r,g,b; int hits; PerkType perk; int alive; } Block;  // One block: position, color, hit count, perk type, alive flag
typedef struct { float x,y,vx,vy,speed,angle; int fireball,active; } Ball;      // Ball: position, velocity, speed, angle, fireball mode, active flag
typedef struct { float x,y,rot; PerkType type; int alive; } Perk;               // Falling perk item: position, rotation, type, alive flag
typedef struct { float x,y,br,spd; } Star;                                      // Background star/particle: position, brightness, speed
typedef struct { int score,diff; float time; char dname[8]; } HSEntry;          // High score entry: score, difficulty, time, difficulty name
typedef struct { float x,y; } Vec2;                                              // 2D float point (used for polygon vertices)
typedef struct { int x,y; } IVec2;                                               // 2D integer point (used for pixel-level drawing)

typedef struct { float initSpeed,maxSpeed,incRate,padW; const char* name; } DiffConfig;  // Difficulty settings: speeds, paddle width, name

/* ── RGBA pixel (for software pixel buffer) ─────────────────────────────── */
typedef struct { unsigned char r,g,b,a; } RGBA;  // One pixel with Red, Green, Blue, Alpha channels

/* ════════════════════════════════════════════════════════════════════════
   Difficulty presets
   ════════════════════════════════════════════════════════════════════════ */
// Three difficulty levels with different ball speed and paddle width
static const DiffConfig DIFF[3] = {
    { 240.f, 480.f, 3.5f, 130.f, "EASY"   },   // Easy: slow ball, wide paddle
    { 310.f, 620.f, 6.0f, 110.f, "MEDIUM" },   // Medium: balanced settings
    { 400.f, 780.f, 9.5f,  85.f, "HARD"   },   // Hard: fast ball, narrow paddle
};

// Names of all background themes shown in menus
static const char* WALL_NAMES[WALL_COUNT] = {
    "SPACE", "OCEAN", "FOREST", "SUNSET", "MATRIX"
};

/* ════════════════════════════════════════════════════════════════════════
   Software pixel buffer (for Flood Fill / Boundary Fill)
   ════════════════════════════════════════════════════════════════════════ */
static RGBA gPixBuf[WIN_H][WIN_W];  // 2D array of pixels, one for each screen pixel

// Fill the entire pixel buffer with black/transparent
static void pixbuf_clear(void) {
    memset(gPixBuf, 0, sizeof(gPixBuf));  // Set all bytes to zero (black transparent)
}

// Set one pixel in the buffer to a given color
static void pixbuf_set(int x, int y, RGBA c) {
    if(x<0||x>=WIN_W||y<0||y>=WIN_H) return;  // Skip if outside screen bounds
    gPixBuf[y][x] = c;                          // Set pixel at (x,y) to color c
}

// Get the color of one pixel from the buffer
static RGBA pixbuf_get(int x, int y) {
    RGBA z = {0,0,0,0};                          // Default: transparent black
    if(x<0||x>=WIN_W||y<0||y>=WIN_H) return z;  // Return default if out of bounds
    return gPixBuf[y][x];                        // Return actual pixel color
}

// Check if two RGBA colors are exactly equal
static int rgba_eq(RGBA a, RGBA b) {
    return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;  // Compare all 4 channels
}

// Draw all non-transparent pixels from the buffer onto the OpenGL screen
static void pixbuf_flush(void) {
    int x,y;
    glBegin(GL_POINTS);  // Start drawing individual points
    for(y=0;y<WIN_H;y++) for(x=0;x<WIN_W;x++) {  // Loop every pixel
        RGBA c = gPixBuf[y][x];
        if(c.a > 0) {                              // Only draw if not fully transparent
            glColor4ub(c.r, c.g, c.b, c.a);       // Set color with alpha
            glVertex2i(x, y);                      // Draw the pixel at (x,y)
        }
    }
    glEnd();  // Finish drawing points
}

/* ════════════════════════════════════════════════════════════════════════
   ACADEMIC GRAPHICS ALGORITHMS
   ════════════════════════════════════════════════════════════════════════ */

/* ── Pixel emitter ──────────────────────────────────────────────────────── */
// Draw a single pixel at position (x,y) using current OpenGL color
static void draw_pixel(int x, int y) {
    glBegin(GL_POINTS); glVertex2i(x,y); glEnd();  // Tell OpenGL to draw one point
}

/* ══════════════════════════════════════════════════
   1. BRESENHAM LINE ALGORITHM
   ══════════════════════════════════════════════════ */
// Draw a straight line from (x1,y1) to (x2,y2) using Bresenham's algorithm
static void draw_line(int x1,int y1,int x2,int y2) {
    int dx=x2-x1, dy=y2-y1;                        // Difference in x and y direction
    int ax=dx<0?-dx:dx, ay=dy<0?-dy:dy;            // Absolute values of dx and dy
    int sx=x2<x1?-1:1,  sy=y2<y1?-1:1;            // Step direction: +1 or -1
    int x=x1, y=y1;                                 // Start at the first point
    draw_pixel(x,y);                                 // Draw the starting pixel
    if(ax>ay) {                                      // Line is more horizontal than vertical
        int e=2*ay-ax, inc1=2*(ay-ax), inc2=2*ay;  // Error values for decision making
        int i;
        for(i=0;i<ax;i++){                           // Loop along x-axis
            if(e>=0){ y+=sy; e+=inc1; } else e+=inc2;  // Decide whether to step in y
            x+=sx; draw_pixel(x,y);                  // Step in x and draw pixel
        }
    } else {                                         // Line is more vertical than horizontal
        int e=2*ax-ay, inc1=2*(ax-ay), inc2=2*ax;  // Error values for decision making
        int i;
        for(i=0;i<ay;i++){                           // Loop along y-axis
            if(e>=0){ x+=sx; e+=inc1; } else e+=inc2;  // Decide whether to step in x
            y+=sy; draw_pixel(x,y);                  // Step in y and draw pixel
        }
    }
}

// Draw a thick line by drawing multiple parallel lines side by side
static void draw_thick_line(int x1,int y1,int x2,int y2,int thick) {
    int half=thick/2, t;                             // Half thickness for centering
    int dx=x2-x1, dy=y2-y1;                         // Direction of the line
    float len=sqrtf((float)(dx*dx+dy*dy));           // Total length of the line
    if(len<1.f){ draw_line(x1,y1,x2,y2); return; }  // If too short, draw single line
    float nx=-dy/len, ny=dx/len;                     // Normal vector (perpendicular direction)
    for(t=-half;t<=half;t++)                          // Draw lines offset by each thickness step
        draw_line((int)(x1+t*nx),(int)(y1+t*ny),
                  (int)(x2+t*nx),(int)(y2+t*ny));
}

/* ══════════════════════════════════════════════════
   2. MIDPOINT CIRCLE ALGORITHM
   ══════════════════════════════════════════════════ */
// Helper: plot 8 symmetric points of a circle at once (circle has 8-fold symmetry)
static void plot_circle_pts(int cx,int cy,int px,int py) {
    draw_pixel(cx+px,cy+py); draw_pixel(cx-px,cy+py);  // Top right and top left
    draw_pixel(cx+px,cy-py); draw_pixel(cx-px,cy-py);  // Bottom right and bottom left
    draw_pixel(cx+py,cy+px); draw_pixel(cx-py,cy+px);  // Right top and left top
    draw_pixel(cx+py,cy-px); draw_pixel(cx-py,cy-px);  // Right bottom and left bottom
}

// Draw outline of a circle using Midpoint Circle Algorithm
static void midpoint_circle(int cx,int cy,int r) {
    int x=0,y=r,p=1-r;       // Start at top of circle, p is the decision parameter
    while(x<=y){              // Loop until we reach 45 degrees
        plot_circle_pts(cx,cy,x,y);   // Draw 8 symmetric points
        if(p<0){ x++; p+=2*x+1; }    // Move right only
        else   { x++; y--; p+=2*x-2*y+1; }  // Move right and down
    }
}

// Draw a filled circle using Midpoint Circle Algorithm (draw horizontal lines between edges)
static void filled_midpoint_circle(int cx,int cy,int r) {
    int x=0,y=r,p=1-r;       // Start at top, p is decision parameter
    while(x<=y){              // Loop until 45 degrees
        draw_line(cx-y,cy+x,cx+y,cy+x);  // Fill horizontal line at +x offset
        draw_line(cx-y,cy-x,cx+y,cy-x);  // Fill horizontal line at -x offset
        draw_line(cx-x,cy+y,cx+x,cy+y);  // Fill horizontal line at +y offset
        draw_line(cx-x,cy-y,cx+x,cy-y);  // Fill horizontal line at -y offset
        if(p<0){ x++; p+=2*x+1; }        // Update decision parameter (right only)
        else   { x++; y--; p+=2*x-2*y+1; }  // Update decision parameter (right and down)
    }
}

/* ══════════════════════════════════════════════════
   3. SCANLINE POLYGON FILL
   ══════════════════════════════════════════════════ */
// Fill any polygon using scanline algorithm (scan each horizontal line and fill between edges)
static void scanline_fill_polygon(IVec2* pts, int n) {
    int i,j,k,y;
    float xs[MAX_PTS];   // X intersection points for current scanline
    int nx;              // Number of intersections found
    int yMin, yMax;      // Top and bottom of the polygon
    if(n<3) return;      // Need at least 3 points to make a polygon
    yMin=pts[0].y; yMax=pts[0].y;
    for(i=0;i<n;i++){    // Find the topmost and bottommost Y values
        if(pts[i].y<yMin) yMin=pts[i].y;
        if(pts[i].y>yMax) yMax=pts[i].y;
    }
    for(y=yMin;y<=yMax;y++){    // Scan every horizontal line from bottom to top
        nx=0;
        for(i=0;i<n;i++){       // Check every edge of the polygon
            j=(i+1)%n;          // Next vertex (wraps around to 0 at end)
            int y0=pts[i].y, y1=pts[j].y;
            float x0=(float)pts[i].x, x1=(float)pts[j].x;
            if((y0<=y&&y<y1)||(y1<=y&&y<y0)){  // Edge crosses this scanline?
                float t=(float)(y-y0)/(float)(y1-y0);
                xs[nx++]=x0+t*(x1-x0);          // Calculate X where edge crosses scanline
            }
        }
        /* Sort X intersections using bubble sort (polygon is small so fast enough) */
        for(i=0;i<nx-1;i++) for(k=0;k<nx-1-i;k++)
            if(xs[k]>xs[k+1]){ float tmp=xs[k]; xs[k]=xs[k+1]; xs[k+1]=tmp; }
        for(k=0;k+1<nx;k+=2)          // Draw lines between pairs of intersections
            draw_line((int)xs[k],y,(int)xs[k+1],y);
    }
}

// Convenience function: fill a rectangle using scanline polygon fill
static void scanline_fill_rect(int x,int y,int w,int h) {
    IVec2 pts[4]={{x,y},{x+w,y},{x+w,y+h},{x,y+h}};  // 4 corners of rectangle
    scanline_fill_polygon(pts,4);                        // Fill as a 4-vertex polygon
}

/* ══════════════════════════════════════════════════
   4. 2D TRANSFORMATION HELPERS
   ══════════════════════════════════════════════════ */
// Apply scale, rotation, and translation to a single point
static Vec2 transform_point(float px,float py,        // Local point to transform
                             float tx,float ty,        // Translation (move to this position)
                             float angle,float sx,float sy) {  // Rotation angle and scale
    float qx=px*sx, qy=py*sy;                         // Apply scale first
    float c=(float)cos(angle), s=(float)sin(angle);   // Cos and sin of rotation angle
    Vec2 r;
    r.x = c*qx - s*qy + tx;   // Rotate X and then translate
    r.y = s*qx + c*qy + ty;   // Rotate Y and then translate
    return r;
}

// Draw a filled polygon after applying 2D transformations (scale, rotate, translate)
static void draw_transformed_poly(Vec2* local,int n,
                                   float tx,float ty,float angle,
                                   float sx,float sy) {
    IVec2 world[MAX_PTS];   // Store transformed integer pixel positions
    int i;
    for(i=0;i<n;i++){       // Transform every vertex from local to world space
        Vec2 p=transform_point(local[i].x,local[i].y,tx,ty,angle,sx,sy);
        world[i].x=(int)p.x; world[i].y=(int)p.y;
    }
    scanline_fill_polygon(world,n);  // Fill the transformed polygon
}

// Draw only the outline of a polygon after applying 2D transformations
static void draw_transformed_poly_outline(Vec2* local,int n,
                                           float tx,float ty,float angle,
                                           float sx,float sy) {
    IVec2 world[MAX_PTS];   // Store transformed integer pixel positions
    int i;
    for(i=0;i<n;i++){       // Transform every vertex
        Vec2 p=transform_point(local[i].x,local[i].y,tx,ty,angle,sx,sy);
        world[i].x=(int)p.x; world[i].y=(int)p.y;
    }
    for(i=0;i<n;i++){       // Draw lines connecting each vertex to the next
        int j=(i+1)%n;
        draw_line(world[i].x,world[i].y,world[j].x,world[j].y);
    }
}

/* ══════════════════════════════════════════════════
   5. FLOOD FILL (4-connected scanline)
      Works on the software pixel buffer
   ══════════════════════════════════════════════════ */

/* Draw a rectangle outline in the pixel buffer */
static void pixbuf_rect_outline(int x,int y,int w,int h,RGBA c) {
    int i;
    for(i=x;i<=x+w;i++){ pixbuf_set(i,y,c); pixbuf_set(i,y+h,c); }  // Top and bottom edges
    for(i=y;i<=y+h;i++){ pixbuf_set(x,i,c); pixbuf_set(x+w,i,c); }  // Left and right edges
}

/* Stack-based 4-connected flood fill: fills an area starting from (sx,sy) */
typedef struct { short x,y; } FillPt;           // Compact point for the fill stack
static FillPt gFillStack[FILL_STACK];            // Stack holding pixels to process

static void flood_fill_4(int sx,int sy, RGBA fill, RGBA target) {
    int head=0, tail=0;                          // Stack head and tail pointers
    if(rgba_eq(fill,target)) return;             // Fill color same as target? Nothing to do
    if(!rgba_eq(pixbuf_get(sx,sy),target)) return;  // Start pixel is not target color? Skip
    gFillStack[tail].x=(short)sx; gFillStack[tail].y=(short)sy; tail++;  // Push start point
    while(head!=tail){                           // Keep going until stack is empty
        int x=gFillStack[head].x, y=gFillStack[head].y;
        head=(head+1)%FILL_STACK;               // Pop from stack (circular buffer)
        if(!rgba_eq(pixbuf_get(x,y),target)) continue;  // Already filled? Skip
        pixbuf_set(x,y,fill);                   // Fill this pixel with new color
        /* Push all 4 neighbors (up, down, left, right) onto stack */
        if(x+1<WIN_W&&rgba_eq(pixbuf_get(x+1,y),target)){
            gFillStack[tail].x=(short)(x+1); gFillStack[tail].y=(short)y;
            tail=(tail+1)%FILL_STACK;
        }
        if(x-1>=0&&rgba_eq(pixbuf_get(x-1,y),target)){
            gFillStack[tail].x=(short)(x-1); gFillStack[tail].y=(short)y;
            tail=(tail+1)%FILL_STACK;
        }
        if(y+1<WIN_H&&rgba_eq(pixbuf_get(x,y+1),target)){
            gFillStack[tail].x=(short)x; gFillStack[tail].y=(short)(y+1);
            tail=(tail+1)%FILL_STACK;
        }
        if(y-1>=0&&rgba_eq(pixbuf_get(x,y-1),target)){
            gFillStack[tail].x=(short)x; gFillStack[tail].y=(short)(y-1);
            tail=(tail+1)%FILL_STACK;
        }
    }
}

/* ══════════════════════════════════════════════════
   6. BOUNDARY FILL (4-connected)
   ══════════════════════════════════════════════════ */
// Fill an area until hitting a boundary color (stops at edges of specific color)
static void boundary_fill_4(int sx,int sy, RGBA fill, RGBA boundary) {
    int head=0, tail=0;                           // Stack pointers
    RGBA cur = pixbuf_get(sx,sy);
    if(rgba_eq(cur,boundary)||rgba_eq(cur,fill)) return;  // Skip if already at boundary or filled
    gFillStack[tail].x=(short)sx; gFillStack[tail].y=(short)sy; tail++;  // Push start point
    while(head!=tail){                            // Keep going until stack is empty
        int x=gFillStack[head].x, y=gFillStack[head].y;
        head=(head+1)%FILL_STACK;                // Pop from stack
        cur=pixbuf_get(x,y);
        if(rgba_eq(cur,boundary)||rgba_eq(cur,fill)) continue;  // Stop if boundary or already filled
        pixbuf_set(x,y,fill);                    // Fill this pixel
        /* Push all 4 neighbors onto stack */
        if(x+1<WIN_W){ gFillStack[tail].x=(short)(x+1); gFillStack[tail].y=(short)y; tail=(tail+1)%FILL_STACK; }
        if(x-1>=0)   { gFillStack[tail].x=(short)(x-1); gFillStack[tail].y=(short)y; tail=(tail+1)%FILL_STACK; }
        if(y+1<WIN_H){ gFillStack[tail].x=(short)x; gFillStack[tail].y=(short)(y+1); tail=(tail+1)%FILL_STACK; }
        if(y-1>=0)   { gFillStack[tail].x=(short)x; gFillStack[tail].y=(short)(y-1); tail=(tail+1)%FILL_STACK; }
    }
}

/* ════════════════════════════════════════════════════════════════════════
   SOUND SYSTEM (pthread background thread)
   ════════════════════════════════════════════════════════════════════════ */
static pthread_mutex_t g_sndMtx  = PTHREAD_MUTEX_INITIALIZER;  // Mutex to protect sound queue from race conditions
static pthread_cond_t  g_sndCond = PTHREAD_COND_INITIALIZER;   // Condition variable to wake up sound thread
static SoundType       g_sndQ[16];                              // Circular queue holding pending sounds (max 16)
static int             g_sndHead=0, g_sndTail=0;               // Head and tail pointers for circular queue
static volatile int    g_sndRun=1;                             // Flag: 1 = keep running, 0 = stop thread

// Return the frequency (pitch) in Hz for each sound type
static float sndFreq(SoundType t) {
    switch(t){
        case SND_WALL:   return 220.f;   // Wall hit: low frequency
        case SND_PADDLE: return 330.f;   // Paddle hit: medium-low
        case SND_BLOCK:  return 440.f;   // Block hit: middle A note
        case SND_PERK:   return 660.f;   // Perk collected: higher pitch
        case SND_DEATH:  return 110.f;   // Player death: very low
        default:         return 880.f;   // Win: highest pitch
    }
}

// Return the duration in milliseconds for each sound type
static int sndMs(SoundType t) {
    switch(t){
        case SND_DEATH: return 280;   // Death sound plays longer
        case SND_WIN:   return 480;   // Win sound plays longest
        case SND_PERK:  return 140;   // Perk sound is medium
        default:        return 55;    // Normal sounds are short beeps
    }
}

/*
 * emitBeep — generates a beep sound
 * Linux  : uses aplay (ALSA) or paplay (PulseAudio) or /dev/audio (OSS)
 * macOS  : uses afplay or osascript beep
 * Windows: uses Beep() API
 */
static void emitBeep(float freq, int ms) {
    Beep((DWORD)freq,(DWORD)ms);  // Windows Beep function: plays a tone at given frequency for given ms
    (void)freq; (void)ms;         // Suppress unused variable warning on non-Windows
}

// Sound thread function: runs in background, plays sounds from the queue
static void* sndThread(void* arg) {
    (void)arg;                                         // Unused parameter
    while(g_sndRun){                                   // Keep running until told to stop
        pthread_mutex_lock(&g_sndMtx);                 // Lock the mutex before checking queue
        while(g_sndHead==g_sndTail && g_sndRun)       // Queue empty? Wait for signal
            pthread_cond_wait(&g_sndCond,&g_sndMtx);  // Sleep until main thread signals a new sound
        if(!g_sndRun){ pthread_mutex_unlock(&g_sndMtx); break; }  // If stopping, exit loop
        SoundType t=g_sndQ[g_sndHead]; g_sndHead=(g_sndHead+1)%16;  // Dequeue one sound
        pthread_mutex_unlock(&g_sndMtx);               // Unlock mutex before playing (playing takes time)
        emitBeep(sndFreq(t), sndMs(t));                // Actually play the beep sound
    }
    return NULL;
}

// Queue a sound to be played by the background thread
static void playSound(SoundType t) {
    pthread_mutex_lock(&g_sndMtx);       // Lock before modifying queue
    int next=(g_sndTail+1)%16;           // Calculate next tail position
    if(next!=g_sndHead){                 // Only add if queue is not full
        g_sndQ[g_sndTail]=t; g_sndTail=next;   // Add sound to queue
        pthread_cond_signal(&g_sndCond); // Wake up the sound thread
    }
    pthread_mutex_unlock(&g_sndMtx);     // Unlock after done
}

/* ════════════════════════════════════════════════════════════════════════
   GLOBALS
   ════════════════════════════════════════════════════════════════════════ */
static GameState gState    = STATE_MENU;  // Current game screen (starts at main menu)
static int  gMenuItem = 0;   // Currently selected menu item (0=Start, 1=HighScore, 2=Background, 3=Exit)
static int  gDiffSel  = 1;   // Currently selected difficulty in diff select screen
static int  gCurDiff  = 1;   // Difficulty being used in current game
static WallTheme gWall = WALL_SPACE;  // Current background theme

static float padX=WIN_W/2.f, padWidth=110.f;  // Paddle X position and width
static float padSquash=1.f;                    // Paddle squash effect (1=normal, <1=squashed)
static Ball  ball;                             // The game ball
static Block blocks[ROWS][COLS];               // All blocks in the game grid
static int   blocksLeft=0;                     // How many blocks are still alive

static Perk    perks[MAX_PERKS];  int  perkCount=0;   // Array of falling perk items
static Star    stars[MAX_STARS];                       // Background decoration particles
static HSEntry highScores[MAX_HS]; int hsCount=0;     // High score table

static int   lives=3, score=0;                // Player lives and score
static float elapsed=0.f, wideTimer=0.f, narrowTimer=0.f, fireTimer=0.f;  // Game timers
static int   keyLeft=0, keyRight=0;           // Keyboard state: is left/right arrow held?
static float gMenuTime=0.f;                   // Time counter for menu animations
static float lastTime=0.f;                    // Timestamp of last frame (for delta time)

/* ════════════════════════════════════════════════════════════════════════
   HELPER MACROS / FUNCTIONS
   ════════════════════════════════════════════════════════════════════════ */
#define SC(r,g,b)     glColor4f(r,g,b,1.f)    // Set opaque color (no transparency)
#define SCA(r,g,b,a)  glColor4f(r,g,b,a)      // Set color with alpha (transparency)

static float fmaxf2(float a,float b){ return a>b?a:b; }  // Return the larger of two floats
static float fminf2(float a,float b){ return a<b?a:b; }  // Return the smaller of two floats
static float fabsf2(float a){ return a<0?-a:a; }          // Return absolute value of float
static int   iabs2(int a)   { return a<0?-a:a; }          // Return absolute value of int

// Draw a text string at position (x,y) using specified bitmap font
static void drawText(float x,float y,const char* s, void* font){
    if(!font) font=GLUT_BITMAP_HELVETICA_18;   // Default font if none given
    glRasterPos2f(x,y);                         // Set where to draw text
    while(*s) glutBitmapCharacter(font,*s++);  // Draw each character one by one
}

// Format a time in seconds as "MM:SS" string
static void fmtTime(char* buf,int sz,float t){
    int m=(int)t/60, s=(int)t%60;   // Split into minutes and seconds
    snprintf(buf,sz,"%02d:%02d",m,s);  // Format with leading zeros
}

/* ════════════════════════════════════════════════════════════════════════
   STARS / BACKGROUND INIT
   ════════════════════════════════════════════════════════════════════════ */
// Initialize all background stars/particles with random positions and speeds
static void initStars(void) {
    int i;
    for(i=0;i<MAX_STARS;i++){
        stars[i].x=(float)(rand()%WIN_W);       // Random X position across window
        stars[i].y=(float)(rand()%WIN_H);       // Random Y position across window
        stars[i].br=(float)(rand()%100)/100.f;  // Random brightness 0.0 to 1.0
        stars[i].spd=(float)(rand()%30+5)/10.f; // Random speed 0.5 to 3.5
    }
}

// Update Matrix rain animation: move characters downward
static void updateMatrix(float dt){
    int i;
    if(gWall!=WALL_MATRIX) return;  // Only run in Matrix theme
    for(i=0;i<MAX_STARS;i++){
        stars[i].y -= stars[i].spd*dt*60.f;  // Move character downward
        if(stars[i].y<0){                     // If character goes off bottom...
            stars[i].y=(float)WIN_H;          // Reset to top
            stars[i].x=(float)(rand()%WIN_W); // Random X column
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════
   DRAW BACKGROUND
   ════════════════════════════════════════════════════════════════════════ */
static void drawBG(void) {
    int i;
    /* Set base background color depending on current theme */
    switch(gWall){
        case WALL_OCEAN:  SC(0.02f,0.05f,0.18f); break;  // Dark blue for ocean
        case WALL_FOREST: SC(0.02f,0.08f,0.03f); break;  // Dark green for forest
        case WALL_SUNSET: SC(0.08f,0.02f,0.12f); break;  // Dark purple for sunset
        case WALL_MATRIX: SC(0.00f,0.03f,0.00f); break;  // Near-black green for matrix
        default:          SC(0.03f,0.03f,0.10f); break;  // Dark blue-black for space
    }
    glBegin(GL_QUADS);  // Draw filled rectangle covering whole window
    glVertex2f(0,0); glVertex2f(WIN_W,0);
    glVertex2f(WIN_W,WIN_H); glVertex2f(0,WIN_H);
    glEnd();

    /* Draw theme-specific decorations on top of base color */
    if(gWall==WALL_SPACE){
        for(i=0;i<MAX_STARS;i++){                    // Draw twinkling star dots
            float b=stars[i].br;
            SCA(b,b,b+.1f,1.f);                     // Slightly blue-tinted white
            glPointSize(b<.5f?1.f:2.f);              // Dim stars are smaller
            glBegin(GL_POINTS); glVertex2f(stars[i].x,stars[i].y); glEnd();
        }
    } else if(gWall==WALL_OCEAN){
        /* Draw bubble circles scattered around */
        for(i=0;i<MAX_STARS;i+=3){
            float b=stars[i].br;
            SCA(b*.3f,b*.5f+.3f,b*.8f+.1f, b*.4f);  // Translucent blue-green
            midpoint_circle((int)stars[i].x,(int)stars[i].y,(int)(b*5+2));  // Small circles
        }
        /* Draw animated wave lines across the screen */
        SCA(.1f,.3f,.6f,.2f);
        {
            int wx,wy;
            for(wy=50;wy<WIN_H;wy+=60)       // Draw rows of waves
                for(wx=0;wx<WIN_W-20;wx+=20){
                    int dy1=(int)(sinf((float)wx*.05f+gMenuTime)*6.f);       // Sine wave offset
                    int dy2=(int)(sinf((float)(wx+20)*.05f+gMenuTime)*6.f);
                    draw_line(wx,wy+dy1,wx+20,wy+dy2);  // Draw one wave segment
                }
        }
    } else if(gWall==WALL_FOREST){
        /* Draw pulsing firefly dots */
        for(i=0;i<MAX_STARS;i++){
            float pulse=sinf(gMenuTime*stars[i].spd+stars[i].x*.05f)*.5f+.5f;  // Pulsing brightness
            SCA(.6f,1.f,.3f,pulse*.5f);       // Green-yellow, semi-transparent
            filled_midpoint_circle((int)stars[i].x,(int)stars[i].y,2);  // Tiny filled circle
        }
        /* Draw dark tree silhouettes at bottom */
        SC(.01f,.04f,.01f);
        {
            int ti;
            for(ti=0;ti<6;ti++){              // Draw 6 trees across the screen
                int tx=60+ti*130;             // X position of each tree
                IVec2 tree[7]={{tx,80},{tx-35,160},{tx-15,160},
                               {tx-20,230},{tx+20,230},{tx+15,160},{tx+35,160}};
                scanline_fill_polygon(tree,7);  // Fill tree shape with scanline fill
            }
        }
    } else if(gWall==WALL_SUNSET){
        /* Draw gradient sky using colored horizontal bands */
        float bands[5][3]={{.08f,.02f,.15f},{.18f,.04f,.12f},{.35f,.1f,.05f},{.6f,.22f,.02f},{.9f,.45f,.05f}};
        {
            int bi;
            for(bi=0;bi<5;bi++){              // Draw each gradient band
                SC(bands[bi][0],bands[bi][1],bands[bi][2]);
                scanline_fill_rect(0,bi*130,WIN_W,130);  // Each band is 130px tall
            }
        }
        /* Draw sun as filled circle with animated rays */
        SC(1.f,.85f,.1f); filled_midpoint_circle(400,80,45);  // Yellow sun
        SCA(1.f,.95f,.4f,.5f);
        {
            int ki;
            for(ki=0;ki<12;ki++){             // Draw 12 sun rays
                float ang=ki*(float)M_PI/6.f+gMenuTime*.3f;  // Rotating angle
                draw_thick_line(400,80,(int)(400+cosf(ang)*65),(int)(80+sinf(ang)*65),2);
            }
        }
        /* Draw twinkling stars in the sky */
        for(i=0;i<MAX_STARS;i++){
            float pulse=sinf(gMenuTime*stars[i].spd+stars[i].y*.02f)*.3f+.2f;
            SCA(1.f,.95f,.7f,pulse); draw_pixel((int)stars[i].x,(int)stars[i].y);
        }
    } else if(gWall==WALL_MATRIX){
        /* Draw falling digit characters (matrix rain effect) */
        char cbuf[2]={0,0};
        for(i=0;i<MAX_STARS;i++){
            float b=stars[i].br*.8f+.2f;
            int ji;
            for(ji=0;ji<4;ji++){              // Draw 4 trailing characters per column
                float fade=1.f-(float)ji*.25f;  // Fade older characters
                SCA(0.f,b*fade,0.f,b*fade*.6f);  // Green with fading brightness
                cbuf[0]=(char)('0'+rand()%10);   // Random digit 0-9
                int cy2=(int)(stars[i].y)+ji*12;  // Stack characters vertically
                if(cy2>=0&&cy2<WIN_H) drawText(stars[i].x,(float)cy2,cbuf,GLUT_BITMAP_HELVETICA_10);
            }
        }
    }
    glPointSize(1.f);  // Reset point size to default

    /* Draw colored border lines around the play area */
    switch(gWall){
        case WALL_OCEAN:  SCA(.2f,.6f,.9f,1); break;   // Blue border for ocean
        case WALL_FOREST: SCA(.1f,.6f,.1f,1); break;   // Green border for forest
        case WALL_SUNSET: SCA(.9f,.4f,.1f,1); break;   // Orange border for sunset
        case WALL_MATRIX: SCA(.0f,.7f,.0f,1); break;   // Green border for matrix
        default:          SCA(.25f,.25f,.45f,1);         // Purple border for space
    }
    draw_thick_line((int)PLAY_LEFT,0,(int)PLAY_LEFT,WIN_H,2);    // Left wall
    draw_thick_line((int)PLAY_RIGHT,0,(int)PLAY_RIGHT,WIN_H,2);  // Right wall
    draw_thick_line((int)PLAY_LEFT,WIN_H-2,(int)PLAY_RIGHT,WIN_H-2,3);  // Top wall
}

/* ════════════════════════════════════════════════════════════════════════
   PERK / BLOCK COLOR HELPERS
   ════════════════════════════════════════════════════════════════════════ */
// Set OpenGL color based on the type of perk
static void perkColor(PerkType t){
    switch(t){
        case PERK_EXTRA_LIFE: SC(0.1f,0.9f,0.2f); break;  // Green for extra life
        case PERK_SPEED_UP:   SC(1.f,0.9f,0.f);   break;  // Yellow for speed up
        case PERK_WIDE_PAD:   SC(0.2f,0.6f,1.f);  break;  // Blue for wide paddle
        case PERK_FIREBALL:   SC(1.f,0.4f,0.f);   break;  // Orange for fireball
        case PERK_SHRINK_PAD: SC(0.7f,0.1f,0.9f); break;  // Purple for shrink
        case PERK_SLOW_BALL:  SC(0.9f,0.9f,0.9f); break;  // White for slow ball
        default:              SC(1,1,1);                    // White for unknown
    }
}

// Return the short 2-character label shown on falling perk items
static const char* perkLbl(PerkType t){
    switch(t){
        case PERK_EXTRA_LIFE: return "+L";   // +Life
        case PERK_SPEED_UP:   return "+S";   // +Speed
        case PERK_WIDE_PAD:   return "+W";   // +Wide
        case PERK_FIREBALL:   return "FB";   // FireBall
        case PERK_SHRINK_PAD: return "-W";   // -Wide
        case PERK_SLOW_BALL:  return "SL";   // SLow
        default: return "  ";                 // Empty for no perk
    }
}

// Set OpenGL color for a block based on how many hits it has left
static void blockColor(Block* b){
    if(b->hits==3)      SC(b->r,b->g,b->b);                          // Full brightness for 3 hits
    else if(b->hits==2) SC(b->r*.75f,b->g*.75f,b->b*.75f);          // 75% brightness for 2 hits
    else                SC(b->r*.45f,b->g*.45f,b->b*.45f);           // 45% brightness for 1 hit
}

/* ════════════════════════════════════════════════════════════════════════
   GAME INIT
   ════════════════════════════════════════════════════════════════════════ */
// Return a random perk type (most blocks have no perk)
static PerkType randPerk(void){
    int r=rand()%12;    // Random number 0 to 11
    if(r==0) return PERK_EXTRA_LIFE;  // 1 in 12 chance
    if(r==1) return PERK_SPEED_UP;
    if(r==2) return PERK_WIDE_PAD;
    if(r==3) return PERK_FIREBALL;
    if(r==4) return PERK_SHRINK_PAD;
    if(r==5) return PERK_SLOW_BALL;
    return PERK_NONE;  // 50% chance of no perk
}

// Initialize all blocks for a new game level
static void initLevel(void){
    int r,c;
    float rc[ROWS][3]={{1.f,.2f,.2f},{1.f,.6f,0.f},{1.f,1.f,.1f},   // Row colors: red, orange, yellow
                       {.2f,.9f,.2f},{.2f,.7f,1.f},{.6f,.2f,1.f},{.9f,.3f,.7f}}; // green, blue, purple, pink
    blocksLeft=0;
    for(r=0;r<ROWS;r++) for(c=0;c<COLS;c++){
        Block* b=&blocks[r][c];
        b->x=PLAY_LEFT+c*BLK_W; b->y=BLK_TOP_Y-r*(BLK_H+4.f);  // Set block position
        b->alive=1;                          // Block starts alive
        b->hits=(r<2)?3:(r<4)?2:1;          // Top rows need 3 hits, middle 2, bottom 1
        b->perk=randPerk();                  // Assign random perk
        b->r=rc[r][0]; b->g=rc[r][1]; b->b=rc[r][2];  // Assign row color
        blocksLeft++;                        // Count this block
    }
}

// Reset the ball to start position (stuck on paddle or free)
static void resetBall(int stuck){
    float ang=(float)M_PI/2.f+((float)(rand()%40)-20.f)*(float)M_PI/180.f;  // Mostly upward angle with small random variation
    ball.x=padX; ball.y=PAD_Y+PAD_H/2.f+BALL_R+1.f;  // Start just above paddle
    ball.speed=DIFF[gCurDiff].initSpeed;               // Initial speed from difficulty
    ball.angle=0.f;
    ball.vx=cosf(ang)*ball.speed; ball.vy=sinf(ang)*ball.speed;  // Set velocity from angle
    ball.fireball=0; ball.active=stuck;  // If stuck=1, ball stays on paddle until player launches
}

// Save the current score to the high score table
static void saveHS(void){
    HSEntry e;
    int i,j;
    e.score=score; e.time=elapsed; e.diff=gCurDiff;
    strncpy(e.dname,DIFF[gCurDiff].name,7); e.dname[7]='\0';  // Copy difficulty name safely
    if(hsCount<MAX_HS) highScores[hsCount++]=e;                // Add new entry if table not full
    else if(e.score>highScores[MAX_HS-1].score) highScores[MAX_HS-1]=e;  // Replace worst score
    /* Sort high scores by score (highest first) using insertion sort */
    for(i=1;i<hsCount;i++){
        HSEntry key=highScores[i];
        for(j=i-1;j>=0&&highScores[j].score<key.score;j--)
            highScores[j+1]=highScores[j];
        highScores[j+1]=key;
    }
}

// Start a new game with selected difficulty
static void startGame(void){
    gCurDiff=gDiffSel;                          // Use currently selected difficulty
    lives=3; score=0; elapsed=0.f;              // Reset lives, score, and time
    wideTimer=narrowTimer=fireTimer=0.f; padSquash=1.f;  // Reset all active effects
    padWidth=DIFF[gCurDiff].padW;               // Set paddle width from difficulty
    perkCount=0; initLevel(); resetBall(1);     // Initialize blocks and ball
    gState=STATE_PLAYING;                       // Switch to playing state
}

/* ════════════════════════════════════════════════════════════════════════
   APPLY PERK
   ════════════════════════════════════════════════════════════════════════ */
// Apply a collected perk effect to the game
static void applyPerk(PerkType t){
    float l;                                    // Used to recalculate ball velocity length
    playSound(SND_PERK);                        // Play perk collect sound
    switch(t){
        case PERK_EXTRA_LIFE:
            lives++; score+=50;                 // Gain one life and 50 bonus points
            break;
        case PERK_SPEED_UP:
            ball.speed=fminf2(ball.speed*1.3f,DIFF[gCurDiff].maxSpeed);  // Speed up by 30%, capped at max
            l=sqrtf(ball.vx*ball.vx+ball.vy*ball.vy);
            if(l>0){ball.vx=ball.vx/l*ball.speed; ball.vy=ball.vy/l*ball.speed;}  // Rescale velocity
            score+=20; break;
        case PERK_WIDE_PAD:
            wideTimer=15.f;narrowTimer=0.f;padWidth=PAD_WIDE;   // Wide paddle for 15 seconds
            break;
        case PERK_FIREBALL:
            fireTimer=12.f; ball.fireball=1;    // Fireball mode for 12 seconds
            break;
        case PERK_SHRINK_PAD:
            narrowTimer=10.f;wideTimer=0.f;padWidth=PAD_NARROW;  // Narrow paddle for 10 seconds (bad!)
            break;
        case PERK_SLOW_BALL:
            ball.speed=fmaxf2(ball.speed*.7f,DIFF[gCurDiff].initSpeed*.6f);  // Slow down by 30%
            l=sqrtf(ball.vx*ball.vx+ball.vy*ball.vy);
            if(l>0){ball.vx=ball.vx/l*ball.speed; ball.vy=ball.vy/l*ball.speed;}  // Rescale velocity
            break;
        default: break;
    }
}

/* ════════════════════════════════════════════════════════════════════════
   DRAW GAME OBJECTS
   ════════════════════════════════════════════════════════════════════════ */

/* ── Paddle ──────────────────────────────────────────────────────────────── */
// Draw the player's paddle with a squash animation effect
static void drawPaddle(void){
    float px=padX, py=PAD_Y;                   // Paddle center position
    float hw=padWidth/2.f, hh=PAD_H/2.f;       // Half-width and half-height
    float sy2=padSquash;                        // Vertical squash scale (1=normal)
    float sx2=1.f+(1.f-padSquash)*.5f;         // Horizontal stretch when squashed
    Vec2 body[4]={{-hw,-hh},{hw,-hh},{hw,hh},{-hw,hh}};    // Paddle body rectangle
    Vec2 hi[4]={{-hw,hh*.1f},{hw,hh*.1f},{hw,hh},{-hw,hh}}; // Highlight strip on top
    SC(.2f,.65f,.95f);                          // Blue color for paddle body
    draw_transformed_poly(body,4,px,py,0.f,sx2,sy2);  // Draw squashed paddle body
    SC(.5f,.85f,1.f);                           // Lighter blue for highlight
    draw_transformed_poly(hi,4,px,py,0.f,sx2,sy2);    // Draw highlight on top of paddle
    SC(.8f,.95f,1.f);                           // Even lighter for outline
    draw_transformed_poly_outline(body,4,px,py,0.f,sx2,sy2);  // Draw paddle outline
}

/* ── Ball ────────────────────────────────────────────────────────────────── */
// Draw the ball (normal white ball or glowing fireball)
static void drawBall(void){
    int cx=(int)ball.x, cy=(int)ball.y, r=(int)BALL_R;  // Ball center and radius
    int k;
    glPointSize(1.f);
    if(ball.fireball){
        /* Draw fireball effect: glowing rings and rays */
        SCA(1.f,.3f,0.f,.4f); midpoint_circle(cx,cy,r*2);         // Outer orange glow ring
        SCA(1.f,.6f,0.f,.7f); midpoint_circle(cx,cy,(int)(r*1.5f));  // Inner orange ring
        SC(1.f,.8f,.1f);
        for(k=0;k<4;k++){                                            // Draw 4 flame rays
            float ang=ball.angle+k*(float)M_PI/2.f;                 // 90 degrees apart, rotating
            int x2=cx+(int)(cosf(ang)*(r+5));
            int y2=cy+(int)(sinf(ang)*(r+5));
            draw_line(cx,cy,x2,y2);                                  // Ray from center
        }
        SC(1.f,.95f,.2f); filled_midpoint_circle(cx,cy,r);         // Yellow filled center
    } else {
        /* Draw normal ball: shadow, white sphere, highlight */
        SCA(.1f,.1f,.2f,.5f); filled_midpoint_circle(cx-2,cy-2,r);  // Dark shadow slightly offset
        SC(.9f,.9f,.95f);     filled_midpoint_circle(cx,cy,r);       // Main white ball
        SC(1,1,1); filled_midpoint_circle(cx+(int)(r*.3f),cy+(int)(r*.3f),(int)(r*.3f));  // White highlight dot
    }
}

/* ── Blocks (interior drawn using Flood Fill) ────────────────────────────── */
// Draw all blocks using software pixel buffer with flood fill for interior
static void drawBlocks(void){
    int row,col;
    pixbuf_clear();  // Clear the pixel buffer before drawing

    /* ── Flood Fill pass: fill each block in the software pixel buffer ── */
    for(row=0;row<ROWS;row++) for(col=0;col<COLS;col++){
        Block* b=&blocks[row][col]; if(!b->alive) continue;  // Skip destroyed blocks
        int bx=(int)(b->x+1), by=(int)(b->y-BLK_H+1);      // Block top-left corner
        int bw=(int)(BLK_W-2), bh=(int)(BLK_H-2);          // Block size (slightly inset)
        float fr,fg,fb2;
        RGBA boundary, fill_c, empty={0,0,0,0};

        /* Choose color based on remaining hits */
        if(b->hits==3)      { fr=b->r;      fg=b->g;      fb2=b->b; }       // Full color
        else if(b->hits==2) { fr=b->r*.75f; fg=b->g*.75f; fb2=b->b*.75f; }  // 75% color
        else                { fr=b->r*.45f; fg=b->g*.45f; fb2=b->b*.45f; }  // 45% color

        boundary.r=20; boundary.g=20; boundary.b=20; boundary.a=255;   // Dark gray border
        fill_c.r=(unsigned char)(fr*255);   // Convert float color to byte
        fill_c.g=(unsigned char)(fg*255);
        fill_c.b=(unsigned char)(fb2*255);
        fill_c.a=255;                       // Fully opaque

        /* Step 1: Draw border outline in pixel buffer */
        pixbuf_rect_outline(bx,by,bw,bh,boundary);

        /* Step 2: Flood Fill from center of block outward to fill interior */
        flood_fill_4(bx+bw/2, by+bh/2, fill_c, empty);

        /* Step 3: Add highlight strip at top using Boundary Fill */
        if(bh>6){
            RGBA hl;
            hl.r=(unsigned char)(fminf2(fr*255+60,255));  // Brighter version of block color
            hl.g=(unsigned char)(fminf2(fg*255+60,255));
            hl.b=(unsigned char)(fminf2(fb2*255+60,255));
            hl.a=180;  // Semi-transparent highlight
            boundary_fill_4(bx+bw/2, by+bh-2, hl, boundary);  // Fill highlight at bottom of block
        }
    }

    /* Send software pixel buffer to OpenGL screen */
    glPointSize(1.f);
    pixbuf_flush();

    /* ── Normal OpenGL pass: draw black outline and perk indicator dot ── */
    for(row=0;row<ROWS;row++) for(col=0;col<COLS;col++){
        Block* b=&blocks[row][col]; if(!b->alive) continue;
        int bx=(int)(b->x+1), by=(int)(b->y-BLK_H+1);
        int bw=(int)(BLK_W-2), bh=(int)(BLK_H-2);
        SCA(0,0,0,.9f);                        // Almost black, semi-transparent
        draw_line(bx,by,bx+bw,by);            // Top edge
        draw_line(bx+bw,by,bx+bw,by+bh);     // Right edge
        draw_line(bx+bw,by+bh,bx,by+bh);     // Bottom edge
        draw_line(bx,by+bh,bx,by);            // Left edge
        if(b->perk!=PERK_NONE){               // If block contains a perk, show colored dot
            perkColor(b->perk);
            filled_midpoint_circle((int)(b->x+BLK_W/2.f),(int)(b->y-BLK_H/2.f),4);
        }
    }
}

/* ── Perk items ─────────────────────────────────────────────────────────── */
// Draw all falling perk items as rotating diamonds
static void drawPerkItems(void){
    int i;
    for(i=0;i<perkCount;i++){
        Perk* p=&perks[i]; if(!p->alive) continue;   // Skip collected or missed perks
        float hw=PERK_W/2.f, hh=PERK_H/2.f;
        Vec2 diamond[4]={{0,hh},{hw,0},{0,-hh},{-hw,0}};  // Diamond shape vertices
        perkColor(p->type);                               // Set color for this perk type
        glPointSize(1.f);
        draw_transformed_poly(diamond,4,p->x,p->y,p->rot,1.f,1.f);          // Filled rotating diamond
        SCA(1,1,1,.6f);
        draw_transformed_poly_outline(diamond,4,p->x,p->y,p->rot,1.f,1.f);  // White outline
        SC(0,0,0);
        drawText(p->x-7,p->y-4,perkLbl(p->type),GLUT_BITMAP_HELVETICA_10);  // Label like "+L" or "FB"
    }
}

/* ── HUD (Heads-Up Display) ──────────────────────────────────────────────── */
// Draw the game info bar at the bottom: score, time, lives, active perks
static void drawHUD(void){
    char buf[64]; int i;
    float tx;
    SC(.05f,.05f,.12f);                         // Very dark background color
    glBegin(GL_QUADS);                          // Draw HUD background bar
    glVertex2f(PLAY_LEFT,PLAY_BOTTOM); glVertex2f(PLAY_RIGHT,PLAY_BOTTOM);
    glVertex2f(PLAY_RIGHT,PLAY_BOTTOM+26.f); glVertex2f(PLAY_LEFT,PLAY_BOTTOM+26.f);
    glEnd();
    SC(.3f,.5f,.8f);
    draw_thick_line((int)PLAY_LEFT,(int)(PLAY_BOTTOM+27),(int)PLAY_RIGHT,(int)(PLAY_BOTTOM+27),1);  // Divider line above HUD

    SC(.6f,.85f,1.f);                           // Light blue text color
    snprintf(buf,sizeof(buf),"Score:%d",score);
    drawText(15,18,buf,GLUT_BITMAP_HELVETICA_12);   // Show score on left

    {char tb[16]; fmtTime(tb,sizeof(tb),elapsed);
     snprintf(buf,sizeof(buf),"Time:%s",tb);}
    drawText(150,18,buf,GLUT_BITMAP_HELVETICA_12);  // Show elapsed time

    /* Show difficulty with color-coded text */
    if(gCurDiff==0) SC(.2f,.9f,.2f);           // Green for easy
    else if(gCurDiff==1) SC(1.f,.85f,0.f);     // Yellow for medium
    else SC(1.f,.2f,.2f);                       // Red for hard
    snprintf(buf,sizeof(buf),"[%s]",DIFF[gCurDiff].name);
    drawText(285,18,buf,GLUT_BITMAP_HELVETICA_12);

    /* Draw lives as small red circles */
    SC(.6f,.85f,1.f); drawText(370,18,"Lives:",GLUT_BITMAP_HELVETICA_12);
    for(i=0;i<lives;i++){
        SC(1.f,.3f,.3f); filled_midpoint_circle(415+i*20,18,6);  // Red filled circle per life
        SC(1.f,.6f,.6f); midpoint_circle(415+i*20,18,6);          // Lighter outline
    }

    /* Show active perk timers */
    tx=495.f;
    if(wideTimer>0){
        SC(.2f,.6f,1.f);
        snprintf(buf,sizeof(buf),"W:%ds",(int)wideTimer+1);     // Wide paddle countdown
        drawText(tx,18,buf,GLUT_BITMAP_HELVETICA_12); tx+=68.f;
    }
    if(narrowTimer>0){
        SC(.7f,.1f,.9f);
        snprintf(buf,sizeof(buf),"N:%ds",(int)narrowTimer+1);   // Narrow paddle countdown
        drawText(tx,18,buf,GLUT_BITMAP_HELVETICA_12); tx+=68.f;
    }
    if(fireTimer>0){
        SC(1.f,.5f,0.f);
        snprintf(buf,sizeof(buf),"F:%ds",(int)fireTimer+1);     // Fireball countdown
        drawText(tx,18,buf,GLUT_BITMAP_HELVETICA_12);
    }

    /* Show current background theme in corner */
    SC(.4f,.4f,.6f);
    snprintf(buf,sizeof(buf),"BG:%s",WALL_NAMES[gWall]);
    drawText(680,18,buf,GLUT_BITMAP_HELVETICA_10);
}

/* ════════════════════════════════════════════════════════════════════════
   MENU SCREENS
   ════════════════════════════════════════════════════════════════════════ */
// Draw the animated ball that bounces around on the main menu
static void drawMenuBall(float t){
    float mx=400+cosf(t*.8f)*60.f, my=425+sinf(t*1.2f)*20.f;  // Oval orbit path
    SCA(.2f,.2f,.4f,.5f); filled_midpoint_circle((int)mx,(int)my,(int)(BALL_R*2.2f));  // Shadow
    SC(.9f,.9f,.95f);     filled_midpoint_circle((int)mx,(int)my,(int)BALL_R);          // Ball
    SC(1,1,1); filled_midpoint_circle((int)(mx+BALL_R*.3f),(int)(my+BALL_R*.3f),(int)(BALL_R*.3f));  // Highlight
}

// Draw the main menu screen
static void drawMenu(void){
    char buf[32];
    drawBG();                                   // Draw background theme
    SC(.3f,.8f,1.f);  drawText(235,535,"DX  BALL",GLUT_BITMAP_TIMES_ROMAN_24);  // Title
    SC(.6f,.9f,1.f);  drawText(185,510,"Prepared by Sadia Islam Khan & Sumaya Akhter",GLUT_BITMAP_HELVETICA_18);  // Credits
    drawMenuBall(gMenuTime);                    // Draw animated demo ball

    {
        const char* items[4]={"  START GAME  ","  HIGH SCORE  "," BACKGROUND  ","     EXIT     "};
        int i;
        for(i=0;i<4;i++){
            float iy=365.f-i*57.f;             // Vertical position of each menu item
            if(i==gMenuItem){                   // Highlight currently selected item
                SC(.2f,.5f,.9f); scanline_fill_rect(228,(int)(iy-8),344,36);  // Blue background box
                SC(.7f,.9f,1.f);
                draw_thick_line(228,(int)(iy-8),572,(int)(iy-8),2);   // Top border
                draw_thick_line(572,(int)(iy-8),572,(int)(iy+28),2);  // Right border
                draw_thick_line(572,(int)(iy+28),228,(int)(iy+28),2); // Bottom border
                draw_thick_line(228,(int)(iy+28),228,(int)(iy-8),2);  // Left border
                SC(1,1,1);                       // White text for selected item
            } else SC(.4f,.6f,.85f);             // Gray-blue for unselected items
            drawText(268,iy+8,items[i],GLUT_BITMAP_HELVETICA_18);
        }
    }
    /* Draw keyboard control hints at bottom */
    SC(.4f,.5f,.7f);
    drawText(110,95,"Up/Down: Navigate    Enter/Space: Select    B: Change BG    ESC: Exit",GLUT_BITMAP_HELVETICA_12);
    SC(.3f,.3f,.5f);
    drawText(70,75,"Algorithms: Bresenham | Midpoint Circle | Scanline Fill | Flood Fill | Boundary Fill | 2D Transform",GLUT_BITMAP_HELVETICA_10);
    SC(.4f,.7f,.4f);
    snprintf(buf,sizeof(buf),"Current BG: %s",WALL_NAMES[gWall]);
    drawText(300,55,buf,GLUT_BITMAP_HELVETICA_12);  // Show current background name
}

// Draw the difficulty selection screen
static void drawDiffSelect(void){
    int i;
    const char* desc[3]={   // Description text for each difficulty
        "Slow ball, wide paddle. Great for beginners!",
        "Balanced speed & paddle. The classic experience.",
        "Fast ball, narrow paddle. For hardcore players!"
    };
    float dc[3][3]={{.2f,.9f,.2f},{1.f,.85f,0.f},{1.f,.25f,.25f}};  // Colors: green, yellow, red
    drawBG();
    SC(1.f,.85f,.2f); drawText(235,548,"SELECT  DIFFICULTY",GLUT_BITMAP_TIMES_ROMAN_24);
    for(i=0;i<3;i++){
        char st[80]; float iy=440.f-i*130.f;   // Vertical position for each option
        int sel=(i==gDiffSel);                  // Is this the selected difficulty?
        SC(dc[i][0]*.25f,dc[i][1]*.25f,dc[i][2]*.25f);
        scanline_fill_rect(170,(int)(iy-68),460,98);   // Dim background box
        SC(dc[i][0],dc[i][1],dc[i][2]);
        if(sel) draw_thick_line(170,(int)(iy-68),630,(int)(iy-68),3);  // Thick border if selected
        else    draw_line(170,(int)(iy-68),630,(int)(iy-68));           // Thin border otherwise
        draw_line(630,(int)(iy-68),630,(int)(iy+30));
        if(sel) draw_thick_line(630,(int)(iy+30),170,(int)(iy+30),3);
        else    draw_line(630,(int)(iy+30),170,(int)(iy+30));
        draw_line(170,(int)(iy+30),170,(int)(iy-68));
        SC(dc[i][0],dc[i][1],dc[i][2]);
        drawText(205,iy+12,DIFF[i].name,GLUT_BITMAP_TIMES_ROMAN_24);   // Difficulty name
        SC(.75f,.75f,.9f);
        snprintf(st,sizeof(st),"Init speed: %.0f px/s    Paddle: %.0f px",DIFF[i].initSpeed,DIFF[i].padW);
        drawText(205,iy-14,st,GLUT_BITMAP_HELVETICA_12);  // Speed and paddle stats
        SC(.55f,.55f,.75f); drawText(205,iy-36,desc[i],GLUT_BITMAP_HELVETICA_12);  // Description
        if(sel){ SC(1,1,1); drawText(138,iy+12,">>>",GLUT_BITMAP_HELVETICA_18); }  // Arrow for selected
    }
    SC(.5f,.6f,.8f);
    drawText(155,40,"Up/Down: Choose    Enter/Space: Start Game    ESC: Back",GLUT_BITMAP_HELVETICA_12);
}

// Draw the high scores page
static void drawHighScorePage(void){
    int i;
    drawBG();
    SC(1.f,.85f,.2f); drawText(250,592,"HIGH  SCORES",GLUT_BITMAP_TIMES_ROMAN_24);  // Title
    /* Draw a trophy icon using thick lines */
    SC(1.f,.8f,0.f);
    draw_thick_line(382,570,418,570,6);   // Trophy top rim
    draw_thick_line(388,558,412,558,8);   // Trophy body top
    draw_thick_line(388,542,412,542,8);   // Trophy body bottom
    draw_thick_line(395,542,395,556,2);   // Trophy left leg
    draw_thick_line(405,542,405,556,2);   // Trophy right leg
    if(hsCount==0){
        SC(.55f,.55f,.8f);
        drawText(215,390,"No scores yet - play a game first!",GLUT_BITMAP_HELVETICA_18);
    } else {
        /* Draw table header */
        SC(.4f,.7f,1.f);
        drawText(55,528,"RANK",GLUT_BITMAP_HELVETICA_12);
        drawText(145,528,"SCORE",GLUT_BITMAP_HELVETICA_12);
        drawText(270,528,"TIME",GLUT_BITMAP_HELVETICA_12);
        drawText(380,528,"DIFFICULTY",GLUT_BITMAP_HELVETICA_12);
        SC(.25f,.35f,.55f); draw_thick_line(48,520,752,520,1);  // Header underline
        int n=hsCount<10?hsCount:10;  // Show at most 10 scores
        for(i=0;i<n;i++){
            char buf[32]; float ry=495.f-i*46.f;   // Y position for this row
            HSEntry* e=&highScores[i];
            /* Color-coded background for top 3 ranks */
            if(i==0){ SCA(1.f,.85f,0.f,.09f); scanline_fill_rect(48,(int)(ry-10),704,40); }   // Gold for 1st
            else if(i==1){ SCA(.7f,.7f,.8f,.06f); scanline_fill_rect(48,(int)(ry-10),704,40); }  // Silver for 2nd
            else if(i==2){ SCA(.8f,.5f,.2f,.06f); scanline_fill_rect(48,(int)(ry-10),704,40); }  // Bronze for 3rd
            float mr=.8f,mg=.8f,mb=.8f;
            if(i==0){mr=1.f;mg=.85f;mb=0.f;}      // Gold text for rank 1
            else if(i==1){mr=.78f;mg=.78f;mb=.82f;}  // Silver for rank 2
            else if(i==2){mr=.82f;mg=.52f;mb=.22f;}  // Bronze for rank 3
            SC(mr,mg,mb);
            snprintf(buf,sizeof(buf),"#%d",i+1);
            drawText(58,ry+8,buf,GLUT_BITMAP_HELVETICA_18);   // Rank number
            SC(1,1,1); snprintf(buf,sizeof(buf),"%d",e->score);
            drawText(145,ry+8,buf,GLUT_BITMAP_HELVETICA_18);  // Score
            SC(.7f,.9f,1.f);
            {char tb[16]; fmtTime(tb,sizeof(tb),e->time); drawText(270,ry+8,tb,GLUT_BITMAP_HELVETICA_18);}  // Time
            float br=1,bg2=1,bb2=1;
            if(e->diff==0){br=.2f;bg2=.9f;bb2=.2f;}    // Green badge for easy
            else if(e->diff==1){br=1.f;bg2=.85f;bb2=0.f;}  // Yellow badge for medium
            else{br=1.f;bg2=.25f;bb2=.25f;}               // Red badge for hard
            SC(br*.2f,bg2*.2f,bb2*.2f); scanline_fill_rect(374,(int)(ry-2),110,28);  // Difficulty badge background
            SC(br,bg2,bb2);
            draw_line(374,(int)(ry-2),484,(int)(ry-2)); draw_line(484,(int)(ry-2),484,(int)(ry+26));
            draw_line(484,(int)(ry+26),374,(int)(ry+26)); draw_line(374,(int)(ry+26),374,(int)(ry-2));
            drawText(382,ry+8,e->dname,GLUT_BITMAP_HELVETICA_12);  // Difficulty name in badge
        }
    }
    SC(.4f,.5f,.7f); drawText(285,22,"ESC - Back to Menu",GLUT_BITMAP_HELVETICA_12);
}

/* ── Wallpaper selection screen ─────────────────────────────────────────── */
// Draw the background theme selection screen
static void drawWallpaperSelect(void){
    int i;
    float previewBG[WALL_COUNT][3]={   // Preview color for each theme
        {.03f,.03f,.10f},{.02f,.05f,.18f},{.02f,.08f,.03f},{.25f,.05f,.08f},{.00f,.08f,.00f}
    };
    float accent[WALL_COUNT][3]={      // Accent color for each theme border
        {.4f,.6f,1.f},{.2f,.6f,.9f},{.1f,.8f,.1f},{.9f,.4f,.1f},{.0f,.9f,.0f}
    };
    const char* desc[WALL_COUNT]={     // Description shown for each theme
        "Classic dark space with twinkling stars",
        "Deep ocean with bubbles and wave lines",
        "Dark forest with glowing fireflies & trees",
        "Vivid sunset gradient with sun rays",
        "Iconic matrix digital rain effect",
    };
    drawBG();
    SC(1.f,.85f,.2f); drawText(220,575,"SELECT  BACKGROUND",GLUT_BITMAP_TIMES_ROMAN_24);
    for(i=0;i<WALL_COUNT;i++){
        float iy=490.f-i*95.f;         // Vertical position for each option
        int sel=(i==(int)gWall);       // Is this the currently active theme?
        SC(previewBG[i][0],previewBG[i][1],previewBG[i][2]);
        scanline_fill_rect(50,(int)(iy-42),120,72);    // Color preview rectangle
        if(sel){
            SC(accent[i][0],accent[i][1],accent[i][2]);
            draw_thick_line(50,(int)(iy-42),170,(int)(iy-42),3);   // Thick border for selected
            draw_thick_line(170,(int)(iy-42),170,(int)(iy+30),3);
            draw_thick_line(170,(int)(iy+30),50,(int)(iy+30),3);
            draw_thick_line(50,(int)(iy+30),50,(int)(iy-42),3);
            SC(1,1,1); drawText(30,iy+5,">>",GLUT_BITMAP_HELVETICA_18);  // Arrow indicator
        } else {
            SC(accent[i][0]*.5f,accent[i][1]*.5f,accent[i][2]*.5f);  // Dim border for unselected
            draw_line(50,(int)(iy-42),170,(int)(iy-42));
            draw_line(170,(int)(iy-42),170,(int)(iy+30));
            draw_line(170,(int)(iy+30),50,(int)(iy+30));
            draw_line(50,(int)(iy+30),50,(int)(iy-42));
        }
        SC(accent[i][0],accent[i][1],accent[i][2]);
        drawText(185,iy+8,WALL_NAMES[i],GLUT_BITMAP_TIMES_ROMAN_24);    // Theme name
        SC(.7f,.7f,.9f); drawText(185,iy-14,desc[i],GLUT_BITMAP_HELVETICA_12);  // Theme description
        if(sel){ SC(.2f,1.f,.5f); drawText(185,iy-32,"[ACTIVE - Live Preview!]",GLUT_BITMAP_HELVETICA_10); }
    }
    SC(.5f,.6f,.8f);
    drawText(100,22,"Up/Down: Choose    Enter/Space: Confirm & Back    ESC: Back",GLUT_BITMAP_HELVETICA_12);
}

// Draw overlay screen for paused, win, or lose states
static void drawOverlay(void){
    int k;
    SCA(0,0,0,.58f); scanline_fill_rect(0,0,WIN_W,WIN_H);  // Semi-transparent black overlay
    if(gState==STATE_PAUSED){
        SC(1.f,.9f,.2f); drawText(310,395,"PAUSED",GLUT_BITMAP_TIMES_ROMAN_24);  // Paused title
        SC(.8f,.8f,1.f);
        drawText(100,345,"P - Resume    ESC - Main Menu    B - Change Background",GLUT_BITMAP_HELVETICA_18);
    } else if(gState==STATE_WIN){
        SCA(.2f,1.f,.3f,.6f);
        for(k=0;k<8;k++){                                 // Draw 8 rotating victory rays
            float ang=k*(float)M_PI/4.f+gMenuTime;
            draw_thick_line(400,380,(int)(400+cosf(ang)*55.f),(int)(380+sinf(ang)*55.f),2);
        }
        SC(.2f,1.f,.4f); drawText(278,425,"YOU  WIN!!!",GLUT_BITMAP_TIMES_ROMAN_24);
        SC(.9f,.9f,1.f);
        {char tb[16],buf[64]; fmtTime(tb,sizeof(tb),elapsed);
         snprintf(buf,sizeof(buf),"Score: %d     Time: %s",score,tb);
         drawText(185,378,buf,GLUT_BITMAP_HELVETICA_18);}  // Final score and time
        drawText(175,332,"Enter/Space - New Game    ESC - Menu",GLUT_BITMAP_HELVETICA_18);
    } else if(gState==STATE_LOSE){
        SCA(1.f,.1f,.1f,.5f);
        draw_thick_line(350,395,450,455,4);  // Draw red X symbol (line 1)
        draw_thick_line(450,395,350,455,4);  // Draw red X symbol (line 2)
        SC(1.f,.2f,.2f); drawText(268,425,"GAME  OVER",GLUT_BITMAP_TIMES_ROMAN_24);
        SC(.9f,.9f,1.f);
        {char tb[16],buf[64]; fmtTime(tb,sizeof(tb),elapsed);
         snprintf(buf,sizeof(buf),"Score: %d     Time: %s",score,tb);
         drawText(190,378,buf,GLUT_BITMAP_HELVETICA_18);}  // Final score and time
        drawText(175,332,"Enter/Space - New Game    ESC - Menu",GLUT_BITMAP_HELVETICA_18);
    }
}

/* ════════════════════════════════════════════════════════════════════════
   PHYSICS UPDATE
   ════════════════════════════════════════════════════════════════════════ */
// Update all game logic each frame (dt = time since last frame in seconds)
static void update(float dt){
    int i,r,c;
    gMenuTime+=dt;       // Always advance menu/animation timer
    updateMatrix(dt);    // Update matrix rain if that theme is active
    if(padSquash<1.f) padSquash=fminf2(1.f,padSquash+dt*4.f);  // Slowly restore paddle from squash
    for(i=0;i<perkCount;i++) perks[i].rot+=dt*2.f;  // Rotate all falling perk items
    ball.angle+=dt*3.f;  // Spin the fireball animation
    if(gState!=STATE_PLAYING) return;  // Only continue if game is active
    elapsed+=dt;         // Count game time

    /* Update perk timers, reset paddle width when they expire */
    if(wideTimer>0){   wideTimer-=dt;   if(wideTimer<=0){   wideTimer=0; padWidth=(narrowTimer>0)?PAD_NARROW:DIFF[gCurDiff].padW;} }
    if(narrowTimer>0){ narrowTimer-=dt; if(narrowTimer<=0){ narrowTimer=0; padWidth=(wideTimer>0)?PAD_WIDE:DIFF[gCurDiff].padW;} }
    if(fireTimer>0){   fireTimer-=dt;   if(fireTimer<=0){   fireTimer=0; ball.fireball=0;} }  // Turn off fireball

    /* Gradually increase ball speed over time */
    ball.speed=fminf2(ball.speed+DIFF[gCurDiff].incRate*dt,DIFF[gCurDiff].maxSpeed);

    /* Move paddle based on keyboard input */
    if(keyLeft)  padX-=PAD_SPEED*dt;
    if(keyRight) padX+=PAD_SPEED*dt;
    {float hw=padWidth/2.f; padX=fmaxf2(PLAY_LEFT+hw,fminf2(PLAY_RIGHT-hw,padX));}  // Clamp to play area

    /* If ball is stuck on paddle, keep it centered and don't move it */
    if(ball.active){ ball.x=padX; ball.y=PAD_Y+PAD_H/2.f+BALL_R+1.f; return; }

    /* Move ball according to velocity */
    ball.x+=ball.vx*dt; ball.y+=ball.vy*dt;

    /* Wall bounce: left, right, and top walls */
    if(ball.x-BALL_R<PLAY_LEFT){  ball.x=PLAY_LEFT+BALL_R;  ball.vx= fabsf2(ball.vx); playSound(SND_WALL); }  // Left wall
    if(ball.x+BALL_R>PLAY_RIGHT){ ball.x=PLAY_RIGHT-BALL_R; ball.vx=-fabsf2(ball.vx); playSound(SND_WALL); }  // Right wall
    if(ball.y+BALL_R>PLAY_TOP){   ball.y=PLAY_TOP-BALL_R;   ball.vy=-fabsf2(ball.vy); playSound(SND_WALL); }  // Top wall

    /* Check collision with paddle */
    {
        float px=padX-padWidth/2.f, py=PAD_Y-PAD_H/2.f;   // Paddle top-left corner
        if(ball.y-BALL_R<py+PAD_H && ball.y+BALL_R>py &&   // Ball overlaps paddle vertically
           ball.x+BALL_R>px && ball.x-BALL_R<px+padWidth && ball.vy<0){  // Ball overlaps horizontally and moving down
            float rel=(ball.x-padX)/(padWidth/2.f);         // Relative hit position (-1=left edge, +1=right edge)
            float ang=rel*65.f*(float)M_PI/180.f;           // Convert to angle (max 65 degrees)
            float spd=ball.speed;
            ball.vx=sinf(ang)*spd; ball.vy=cosf(ang)*spd;  // New velocity based on hit position
            ball.y=py+PAD_H+BALL_R+1.f;                    // Push ball above paddle to avoid sticking
            padSquash=0.55f; playSound(SND_PADDLE);          // Squash paddle and play sound
        }
    }

    /* Ball fell below screen = lost a life */
    if(ball.y-BALL_R<PLAY_BOTTOM){
        playSound(SND_DEATH); lives--;   // Play death sound, subtract a life
        if(lives<=0){ saveHS(); gState=STATE_LOSE; }  // No lives left: save score and show game over
        else resetBall(1);              // Still have lives: reset ball on paddle
        return;
    }

    /* Check ball collision with each block */
    for(r=0;r<ROWS;r++) for(c=0;c<COLS;c++){
        Block* b=&blocks[r][c]; if(!b->alive) continue;  // Skip dead blocks
        float bx=b->x, by=b->y-BLK_H, bw=BLK_W-2.f, bh=BLK_H-2.f;
        /* Find closest point on block to ball center */
        float cx2=fmaxf2(bx+1.f,fminf2(ball.x,bx+1.f+bw));
        float cy2=fmaxf2(by+1.f,fminf2(ball.y,by+1.f+bh));
        float dx=ball.x-cx2, dy=ball.y-cy2;
        if(dx*dx+dy*dy>BALL_R*BALL_R) continue;  // No collision if distance > radius

        b->hits=ball.fireball?0:b->hits-1;        // Fireball destroys instantly, else reduce hits
        score+=10*(4-(b->hits<0?0:b->hits));       // Score more for hitting harder blocks
        playSound(SND_BLOCK);

        if(b->hits<=0){          // Block is destroyed
            b->alive=0; blocksLeft--; score+=5;   // Remove block, gain bonus points
            if(b->perk!=PERK_NONE && perkCount<MAX_PERKS){  // Drop perk if it had one
                Perk np;
                np.x=b->x+BLK_W/2.f; np.y=b->y-BLK_H/2.f;
                np.rot=0.f; np.type=b->perk; np.alive=1;
                perks[perkCount++]=np;             // Add perk to falling items list
            }
        }
        if(!ball.fireball){      // Normal ball bounces off blocks
            /* Calculate overlap on each axis to determine bounce direction */
            float ox=(dx>0)?(bx+1+bw-(ball.x-BALL_R)):((ball.x+BALL_R)-(bx+1));
            float oy=(dy>0)?(by+1+bh-(ball.y-BALL_R)):((ball.y+BALL_R)-(by+1));
            if(fabsf2(ox)<fabsf2(oy)) ball.vx=-ball.vx; else ball.vy=-ball.vy;  // Bounce off smaller overlap axis
        }
        /* Normalize velocity to keep constant speed after bounce */
        {float l=sqrtf(ball.vx*ball.vx+ball.vy*ball.vy);
         if(l>0){ball.vx=ball.vx/l*ball.speed; ball.vy=ball.vy/l*ball.speed;}}
        if(!ball.fireball) goto done;  // Normal ball only hits one block per frame
    }
    done:;  // Fireball continues through all blocks

    /* Check perk item collection by paddle */
    for(i=0;i<perkCount;i++){
        Perk* p=&perks[i]; if(!p->alive) continue;
        p->y-=PERK_FALL*dt;   // Move perk downward
        /* Check if perk overlaps with paddle */
        if(p->y-PERK_H/2<PAD_Y+PAD_H/2 && p->y+PERK_H/2>PAD_Y-PAD_H/2 &&
           p->x+PERK_W/2>padX-padWidth/2.f && p->x-PERK_W/2<padX+padWidth/2.f){
            p->alive=0; applyPerk(p->type);  // Collect and apply the perk
        }
        if(p->y<PLAY_BOTTOM) p->alive=0;  // Perk missed (fell off screen)
    }

    if(blocksLeft<=0){ saveHS(); playSound(SND_WIN); gState=STATE_WIN; }  // All blocks gone = win!
}

/* ════════════════════════════════════════════════════════════════════════
   GLUT CALLBACKS
   ════════════════════════════════════════════════════════════════════════ */
// Called every frame to draw everything on screen
void display(void){
    glClear(GL_COLOR_BUFFER_BIT);   // Clear screen to background color
    glLoadIdentity();               // Reset transformation matrix
    glEnable(GL_BLEND);            // Enable transparency blending
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);  // Standard alpha blending
    glPointSize(1.f);              // Default pixel size
    switch(gState){
        case STATE_MENU:        drawMenu();           break;   // Draw main menu
        case STATE_DIFF_SELECT: drawDiffSelect();     break;   // Draw difficulty screen
        case STATE_HIGHSCORE:   drawHighScorePage();  break;   // Draw high scores
        case STATE_WALLPAPER:   drawWallpaperSelect();break;   // Draw theme select
        default:
            drawBG();                        // Draw background
            drawBlocks(); drawPerkItems(); drawPaddle(); drawBall(); drawHUD();  // Draw game elements
            if(gState==STATE_PAUSED||gState==STATE_WIN||gState==STATE_LOSE)
                drawOverlay();               // Draw overlay for paused/end states
    }
    glutSwapBuffers();  // Swap front and back buffer to show rendered frame
}

// Called when window is resized: adjust viewport and projection
void reshape(int w,int h){
    glViewport(0,0,w,h);                    // Use whole window as viewport
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluOrtho2D(0,WIN_W,0,WIN_H);           // 2D orthographic projection matching game coordinates
    glMatrixMode(GL_MODELVIEW);
}

// Called continuously when idle: calculate delta time and update game
void idle(void){
    float t=glutGet(GLUT_ELAPSED_TIME)/1000.f;  // Current time in seconds
    float dt=t-lastTime; lastTime=t;            // Time since last frame
    if(dt>.05f)dt=.05f;   // Cap delta time to prevent huge jumps if window was frozen
    update(dt);           // Update all game logic
    glutPostRedisplay();  // Request screen redraw
}

// Called when a regular keyboard key is pressed
void keyboard(unsigned char key,int x,int y){
    (void)x;(void)y;
    /* B key: cycle background theme from any screen */
    if(key=='b'||key=='B'){
        gWall=(WallTheme)((gWall+1)%WALL_COUNT);  // Go to next theme, wrap around
        return;
    }
    if(key==27){ /* ESC key: go back to previous screen */
        if(gState==STATE_PLAYING||gState==STATE_PAUSED) gState=STATE_MENU;
        else if(gState==STATE_DIFF_SELECT)  gState=STATE_MENU;
        else if(gState==STATE_HIGHSCORE)    gState=STATE_MENU;
        else if(gState==STATE_WALLPAPER)    gState=STATE_MENU;
        else if(gState==STATE_WIN||gState==STATE_LOSE) gState=STATE_MENU;
        return;
    }
    if(key==' '||key==13){ /* Space or Enter: confirm/select */
        if(gState==STATE_MENU){
            if(gMenuItem==0)      gState=STATE_DIFF_SELECT;  // Start game
            else if(gMenuItem==1) gState=STATE_HIGHSCORE;    // View scores
            else if(gMenuItem==2) gState=STATE_WALLPAPER;    // Change background
            else                  exit(0);                   // Exit game
        } else if(gState==STATE_DIFF_SELECT){
            startGame();   // Begin game with selected difficulty
        } else if(gState==STATE_WALLPAPER){
            gState=STATE_MENU;  // Confirm selection and go back to menu
        } else if(gState==STATE_PLAYING && ball.active){
            ball.active=0;      // Launch ball from paddle
        } else if(gState==STATE_WIN||gState==STATE_LOSE){
            gState=STATE_DIFF_SELECT;  // Go back to difficulty select after game ends
        }
        return;
    }
    if(key=='p'||key=='P'){
        if(gState==STATE_PLAYING)       gState=STATE_PAUSED;   // Pause the game
        else if(gState==STATE_PAUSED)   gState=STATE_PLAYING;  // Resume the game
    }
}

// Called when arrow keys or function keys are pressed
void specialKey(int key,int x,int y){
    (void)x;(void)y;
    if(gState==STATE_MENU){
        if(key==GLUT_KEY_DOWN) gMenuItem=(gMenuItem+1)%4;   // Move selection down
        if(key==GLUT_KEY_UP)   gMenuItem=(gMenuItem+3)%4;   // Move selection up (wraps around)
        return;
    }
    if(gState==STATE_DIFF_SELECT){
        if(key==GLUT_KEY_UP)   gDiffSel=(gDiffSel+2)%3;    // Move difficulty selection up
        if(key==GLUT_KEY_DOWN) gDiffSel=(gDiffSel+1)%3;    // Move difficulty selection down
        return;
    }
    if(gState==STATE_WALLPAPER){
        if(key==GLUT_KEY_UP)   gWall=(WallTheme)((gWall+WALL_COUNT-1)%WALL_COUNT);  // Previous theme
        if(key==GLUT_KEY_DOWN) gWall=(WallTheme)((gWall+1)%WALL_COUNT);             // Next theme
        return;
    }
    if(key==GLUT_KEY_LEFT)  keyLeft=1;   // Start moving paddle left
    if(key==GLUT_KEY_RIGHT) keyRight=1;  // Start moving paddle right
}

// Called when arrow keys are released
void specialKeyUp(int key,int x,int y){
    (void)x;(void)y;
    if(key==GLUT_KEY_LEFT)  keyLeft=0;   // Stop moving paddle left
    if(key==GLUT_KEY_RIGHT) keyRight=0;  // Stop moving paddle right
}

// Called when mouse moves (without clicking): control paddle with mouse
void mouseMove(int x,int y){
    (void)y;
    if(gState==STATE_PLAYING){
        float hw=padWidth/2.f;
        padX=(float)x;   // Set paddle center to mouse X position
        padX=fmaxf2(PLAY_LEFT+hw,fminf2(PLAY_RIGHT-hw,padX));  // Clamp to play area
    }
}

// Called when mouse button is clicked
void mouseClick(int btn,int state,int x,int y){
    (void)x;(void)y;
    if(btn==GLUT_LEFT_BUTTON && state==GLUT_DOWN)  // Left click pressed
        if(gState==STATE_PLAYING && ball.active) ball.active=0;  // Launch ball with click
}

/* ════════════════════════════════════════════════════════════════════════
   MAIN
   ════════════════════════════════════════════════════════════════════════ */
int main(int argc,char** argv){
    pthread_t sndT;                            // Sound thread handle
    srand((unsigned int)time(NULL));           // Seed random number generator with current time
    initStars();                               // Create random background stars
    pthread_create(&sndT,NULL,sndThread,NULL); // Start sound in a background thread

    glutInit(&argc,argv);                      // Initialize GLUT with command line args
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGB); // Double buffering + RGB color mode
    glutInitWindowSize(WIN_W,WIN_H);           // Set window size
    glutInitWindowPosition(100,80);            // Set window position on screen
    glutCreateWindow("DX Ball v4  [Bresenham | Midpoint Circle | Scanline | Flood Fill | Boundary Fill | 2D Transform]");
    glClearColor(.03f,.03f,.10f,1.f);         // Set background clear color (dark blue)

    /* Register all callback functions */
    glutDisplayFunc(display);                  // Called to draw each frame
    glutReshapeFunc(reshape);                  // Called when window is resized
    glutIdleFunc(idle);                        // Called when nothing else is happening
    glutKeyboardFunc(keyboard);                // Called for normal key presses
    glutSpecialFunc(specialKey);               // Called for special keys (arrows, F-keys)
    glutSpecialUpFunc(specialKeyUp);           // Called when special keys are released
    glutPassiveMotionFunc(mouseMove);          // Called when mouse moves without clicking
    glutMotionFunc(mouseMove);                 // Called when mouse moves while clicking
    glutMouseFunc(mouseClick);                 // Called for mouse button events

    lastTime=glutGet(GLUT_ELAPSED_TIME)/1000.f;  // Record start time
    glutMainLoop();                            // Enter the GLUT event loop (runs forever)

    /* Cleanup when loop exits (usually doesn't reach here) */
    g_sndRun=0;                                // Signal sound thread to stop
    pthread_cond_signal(&g_sndCond);           // Wake up sleeping sound thread
    pthread_join(sndT,NULL);                   // Wait for sound thread to finish
    return 0;
}
