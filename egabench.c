/* Copyright (c) 2022, Nikolai Wuttke. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Parts of this program are based on code from the Cosmore project
 * (https://github.com/smitelli/cosmore), which is licensed under the same
 * license conditions.
 *
 * Cosmore's original Copyright notice:
 *
 * Copyright (c) 2020-2022 Scott Smitelli
 * Based on COSMO{1..3}.EXE distributed with "Cosmo's Cosmic Adventure"
 * Copyright (c) 1992 Apogee Software, Ltd.
 */

#include <conio.h>
#include <dos.h>
#include <mem.h>

#include <stdio.h>
#include <stdlib.h>


typedef unsigned char byte;
typedef unsigned int  word;
typedef unsigned long dword;
typedef void interrupt (far *InterruptHandler)(void);


#define DEFAULT_ITERATIONS 1000
#define DEFAULT_TIMER_RATE 1000

#define VMEM_SEG       0xa000
#define VMEM_TILES_SEG 0xa400

static byte far* VMEM = MK_FP(VMEM_SEG, 0);


/*******************************************************************************

  Timing and video support code, most of this is taken from or based on
  Cosmore code

*******************************************************************************/

/*
Resets the EGA's bit mask to its default state. Allows writes to all eight pixel
positions in each written byte.
*/
#define EGA_SET_DEFAULT_BITMASK() asm { \
  mov dx, 0x3ce;  \
  mov ax, 0xff08; \
  out dx, ax;     \
}

/*
Resets the EGA's read and write modes to their default state. This allows for
direct (i.e. non-latched) writes from the CPU.
*/
#define EGA_SET_DEFAULT_MODE() asm { \
  mov dx, 0x3ce; \
  mov ax, 0x5;   \
  out dx, ax;    \
}

/*
Resets the EGA's map mask to its default state (allows writes to all four memory
planes), sets default read mode, and enables latched writes from the CPU.
*/
#define EGA_SETUP_LATCH_COPY() asm { \
  mov dx, 0x3c4; \
  mov ax, 0xf02; \
  out dx, ax;    \
  mov dx, 0x3ce; \
  mov ax, 0x105; \
  out dx, ax;    \
}

#define EGA_SELECT_PLANE(bitmask, readMap) asm { \
  mov dx, 0x3c4; \
  mov ax, bitmask; \
  out dx, ax; \
  mov dx, 0x3ce; \
  mov ax, readMap; \
  out dx, ax; \
}

#define EGA_SELECT_PLANE_0() EGA_SELECT_PLANE(0x102, 0x004)
#define EGA_SELECT_PLANE_1() EGA_SELECT_PLANE(0x202, 0x104)
#define EGA_SELECT_PLANE_2() EGA_SELECT_PLANE(0x402, 0x204)
#define EGA_SELECT_PLANE_3() EGA_SELECT_PLANE(0x802, 0x304)


static int timerRate = DEFAULT_TIMER_RATE;

static InterruptHandler savedInt8;
static dword pit0Value;
static dword timerTickCount;

static dword tickCounter;


static void SetPIT0Value(word value)
{
  /*
  Bit Pattern | Interpretation
  ------------|---------------
  00xxxxxx    | Select timer channel 0
  xx11xxxx    | Access Mode: "Low byte, followed by high byte"
  xxxx011x    | Mode 3: Square wave generator
  xxxxxxx0    | 16-bit binary counting mode
  */
  outportb(0x0043, 0x36);

  /* PIT counter 0 divisor (low, high byte) */
  outportb(0x0040, value);
  outportb(0x0040, value >> 8);

  pit0Value = value;
}


static void SetInterruptRate(word desiredRate)
{
  SetPIT0Value((word)(1193182L / desiredRate));
}


// This simply increments the tick counter, and invokes the original
// timer interrupt handler at (roughly) the original rate.
static void interrupt TimerInterruptService(void)
{
  tickCounter++;

  asm mov   ax,[WORD PTR timerTickCount]
  asm add   ax,[WORD PTR pit0Value]
  asm mov   [WORD PTR timerTickCount],ax
  asm jnc   acknowledge
  savedInt8();
  goto done;
acknowledge:
  outportb(0x0020, 0x20);
done:
  ;  /* VOID */
}


static void InstallTimer(int rate)
{
  disable();

  savedInt8 = getvect(8);
  setvect(8, TimerInterruptService);

  SetInterruptRate(rate);

  enable();
}


static void WaitTicks(int ticks)
{
  tickCounter = 0;
  while (tickCounter < ticks);
}


static void WaitMs(int ms)
{
  WaitTicks(ms / (DEFAULT_TIMER_RATE / timerRate));
}


static void RemoveTimer(void)
{
  disable();

  setvect(8, savedInt8);
  SetPIT0Value(0);

  enable();
}


static void InitVideo(void)
{
  // Set EGA mode 0xD: 320x200, 16 colors
  _AX = 0xD;
  geninterrupt(0x10);

  EGA_SET_DEFAULT_BITMASK();
  EGA_SET_DEFAULT_MODE();
}


static void SetDisplayPage(int page)
{
  _AX = page;
  _AH = 0x5;
  geninterrupt(0x10);
}


static void ExitVideo(void)
{
  // Go back to text mode
  textmode(C80);
}


/*******************************************************************************

  Drawing code

*******************************************************************************/

static int AdjustPaletteValue(int value)
{
  // See https://moddingwiki.shikadi.net/wiki/Duke_Nukem_II_Palette_Formats
  return (value * 15) >> 4;
}


static void SetDuke2Palette(void)
{
  // See https://moddingwiki.shikadi.net/wiki/Duke_Nukem_II_Palette_Formats
  static const char PALETTE[] = {
    0x00, 0x00, 0x00,  0x10, 0x10, 0x10,  0x20, 0x20, 0x20,  0x30, 0x30, 0x30,
    0x20, 0x00, 0x00,  0x30, 0x00, 0x00,  0x40, 0x1C, 0x10,  0x40, 0x40, 0x00,
    0x00, 0x10, 0x00,  0x00, 0x00, 0x20,  0x00, 0x00, 0x30,  0x00, 0x00, 0x40,
    0x00, 0x20, 0x00,  0x00, 0x30, 0x00,  0x20, 0x10, 0x00,  0x40, 0x40, 0x40
  };

  int i;

  for (i = 0; i < 16; ++i)
  {
    outportb(0x3c8, i <= 7 ? i : i + 8);
    outportb(0x3c9, AdjustPaletteValue(PALETTE[i * 3 + 0]));
    outportb(0x3c9, AdjustPaletteValue(PALETTE[i * 3 + 1]));
    outportb(0x3c9, AdjustPaletteValue(PALETTE[i * 3 + 2]));
  }
}


// This is basically CopyTilesToEga from Cosmore
static void CopyTilesToVram(byte *source, word size, word destOffset)
{
  word i;
  word mask;
  byte *src = source;
  byte far *dest = MK_FP(VMEM_SEG, destOffset);

  for (i = 0; i < size; i++)
  {
    for (mask = 0x0100; mask < 0x1000; mask = mask << 1)
    {
      outport(0x03c4, mask | 0x0002);

      *(dest + i) = *(src++);
    }
  }
}


// This is exactly the same code Duke Nukem II uses to draw solid tiles
// via latch copy. It's fairly similar to DrawSolidTile from Cosmore,
// but has been optimized a little further.
static void DrawSolidTile(word sourceOffset, word destOffset)
{
  asm push  ds

  asm mov   dx,VMEM_SEG
  asm mov   es,dx
  asm mov   dx,VMEM_TILES_SEG
  asm mov   ds,dx
  asm mov   si,[sourceOffset]
  asm mov   di,[destOffset]
  asm mov   bx, 39

  asm movsb
  asm add   di,bx
  asm movsb
  asm add   di,bx
  asm movsb
  asm add   di,bx
  asm movsb
  asm add   di,bx
  asm movsb
  asm add   di,bx
  asm movsb
  asm add   di,bx
  asm movsb
  asm add   di,bx
  asm movsb

  asm pop   ds
}


// This code is very similar to how Duke Nukem II draws masked tiles, except
// that it doesn't have to deal with the mask.
//
// Also see DrawSpriteTile in Cosmore's lowlevel.asm
static void DrawSolidTileSlow(char near* data, word destOffset)
{
  asm mov di, [destOffset]
  asm mov ax, VMEM_SEG
  asm mov si, [data]
  asm mov es, ax

  asm mov bx, 39
  asm mov cx, 3

// Copy 8 bytes from main memory to video ram, incrementing both pointers
// as needed to skip one row in the source (4 bytes) and one in the destination
// (40 bytes). movsb already increments both pointers by 1, so the values
// in BX and CX are the remainder that we need to add in order to get to
// +40 and +4, respectively.
#define COPY_BLOCK() asm { \
  movsb;      \
  add di,bx;  \
  add si,cx;  \
  movsb;      \
  add di,bx;  \
  add si,cx;  \
  movsb;      \
  add di,bx;  \
  add si,cx;  \
  movsb;      \
  add di,bx;  \
  add si,cx;  \
  movsb;      \
  add di,bx;  \
  add si,cx;  \
  movsb;      \
  add di,bx;  \
  add si,cx;  \
  movsb;      \
  add di,bx;  \
  add si,cx;  \
  movsb;      \
}

// Reset destination pointer back to the beginning, and source pointer to the
// beginning of the first row's next plane.
// The destination was incremented 8 times by the movsb instructions, and
// additionally 7 * BX (39), so in total, 7 * 39 + 8 = 281.
// The source was incremented 8 times by the movsb instructions, and
// additionally 7 * CX (3), so in total, 7 * 3 + 8 = 29. Since we want to
// advance the source to the next plane, we only subtract 28.
#define RESET() asm { \
  sub di,281; \
  sub si,28;  \
}

  EGA_SELECT_PLANE_0();
  COPY_BLOCK();
  RESET();

  EGA_SELECT_PLANE_1();
  COPY_BLOCK();
  RESET();

  EGA_SELECT_PLANE_2();
  COPY_BLOCK();
  RESET();

  EGA_SELECT_PLANE_3();
  COPY_BLOCK();
}


static void DrawFullscreen(char near* buffer)
{
  asm mov si, [buffer]
  asm xor di, di
  asm mov ax, VMEM_SEG
  asm mov es, ax
  asm mov bx, 4000

  EGA_SELECT_PLANE_0();
  asm mov cx, bx
  asm rep movsw

  EGA_SELECT_PLANE_1();
  asm xor di, di
  asm mov cx, bx
  asm rep movsw

  EGA_SELECT_PLANE_2();
  asm xor di, di
  asm mov cx, bx
  asm rep movsw

  EGA_SELECT_PLANE_3();
  asm xor di, di
  asm mov cx, bx
  asm rep movsw
}


static void DrawTiledFullscreen(void)
{
  int col;
  int row;
  int idx = 0;

  EGA_SETUP_LATCH_COPY();

  for (row = 0; row < 25 * 320; row += 320)
  {
    for (col = 0; col < 40; col++)
    {
      DrawSolidTile(idx, col + row);
      idx += 8;
    }
  }
}


static void DrawTiledFullscreenSlow(char near* buffer)
{
  int col;
  int row;

  EGA_SET_DEFAULT_MODE();

  for (row = 0; row < 25 * 320; row += 320)
  {
    for (col = 0; col < 40; col++)
    {
      DrawSolidTileSlow(buffer, col + row);
      buffer += 32;
    }
  }
}


static void ClearScreen(void)
{
  int i;

  outport(0x03c4, (0x0f << 8) | 0x02);  /* map mask: all planes active */

  for (i = 0; i < 8000; i++)
  {
    VMEM[i] = 0;
  }
}


static int numIterations = DEFAULT_ITERATIONS;


static float TicksToMsPerIteration(dword ticks)
{
  float factor = (float)DEFAULT_TIMER_RATE / (float)timerRate;
  return (float)ticks * factor / (float)numIterations;
}


int main(int argc, char** argv)
{
  FILE* fp;
  char near* buffer;
  dword plainTicks, tiledTicks, tiledTicksSlow;

  if (argc >= 2)
  {
    numIterations = atoi(argv[1]);
  }

  if (argc >= 3)
  {
    timerRate = atoi(argv[2]);
  }

  // Setup
  InitVideo();
  InstallTimer(timerRate);
  SetDuke2Palette();

  buffer = malloc(32000);

  /*****************************************************************************

    Benchmark 1: Measure drawing a fullscreen image, plane by plane

  *****************************************************************************/
  fp = fopen("BONUSSCN.MNI", "rb");
  fread(buffer, 32000, 1, fp);
  fclose(fp);

  // Show the image on screen once, to check that it works correctly (and so
  // that there's at least something to see)
  DrawFullscreen(buffer);
  SetDisplayPage(0);
  WaitMs(1500);

  // To simulate a game that does double-buffering, switch the active
  // (displayed) page to 1. This will cause the drawing code to write into
  // page 0, which is now off-screen.
  SetDisplayPage(1);

  {
    int i;

    tickCounter = 0;

    for (i = 0; i < numIterations; ++i)
    {
      DrawFullscreen(buffer);
    }

    plainTicks = tickCounter;
  }

  SetDisplayPage(0);
  ClearScreen();

  /*****************************************************************************

    Benchmark 2: Measure drawing a screen full of 8x8 tiles, using latch copies

  *****************************************************************************/
  fp = fopen("DROP12.MNI", "rb");
  fread(buffer, 32000, 1, fp);
  fclose(fp);

  // Copy the data to vram so that we can draw it via latch copy
  CopyTilesToVram(buffer, 8000, 0x4000);

  DrawTiledFullscreen();
  WaitMs(250);

  // Same as above
  SetDisplayPage(1);

  {
    int i;

    tickCounter = 0;

    for (i = 0; i < numIterations; ++i)
    {
      DrawTiledFullscreen();
    }

    tiledTicks = tickCounter;
  }

  SetDisplayPage(0);
  ClearScreen();

  /*****************************************************************************

    Benchmark 3: Measure drawing a screen full of 8x8 tiles, from main memory

  *****************************************************************************/

  DrawTiledFullscreenSlow(buffer);
  WaitMs(250);

  // Same as above
  SetDisplayPage(1);

  {
    int i;

    tickCounter = 0;

    // This benchmark is very slow, so only run half the iterations and then
    // multiply the result by 2
    for (i = 0; i < numIterations/2; ++i)
    {
      DrawTiledFullscreenSlow(buffer);
    }

    tiledTicksSlow = tickCounter*2;
  }

  SetDisplayPage(0);

  // Cleanup
  free(buffer);

  RemoveTimer();
  ExitVideo();

  // Report
  printf("Results for %d iterations:\n", numIterations);
  printf(
    "%f ms plain, %f ms tiled (fast), %f ms tiled (slow)\n",
    TicksToMsPerIteration(plainTicks),
    TicksToMsPerIteration(tiledTicks),
    TicksToMsPerIteration(tiledTicksSlow));

  return 0;
}
