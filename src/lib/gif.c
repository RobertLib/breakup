#include "gif.h"

// ---------------------------------------------------------------------------
// Median cut, in RGB565 space.
//
// A fixed palette was the obvious first answer and the wrong one here: the
// field is mostly dark blue gradient with neon on top, and a 6x7x6 cube spends
// most of its 256 entries on colours the game never draws while banding the
// ones it draws all the time. Cutting the clip's own histogram puts the
// entries where the pixels are.
// ---------------------------------------------------------------------------

typedef struct Box
{
  int lo[3], hi[3]; // inclusive, in 5/6/5-bit units
  Uint64 count;
} Box;

static int index565(int r, int g, int b)
{
  return (r << 11) | (g << 5) | b;
}

// Pulls the box in to the cells that actually hold pixels, and counts them.
static void shrinkBox(Box *box, const Uint32 *hist)
{
  int lo[3] = {31, 63, 31};
  int hi[3] = {0, 0, 0};
  Uint64 count = 0;

  for (int r = box->lo[0]; r <= box->hi[0]; r++)
  {
    for (int g = box->lo[1]; g <= box->hi[1]; g++)
    {
      for (int b = box->lo[2]; b <= box->hi[2]; b++)
      {
        Uint32 n = hist[index565(r, g, b)];

        if (n == 0)
        {
          continue;
        }

        count += n;
        lo[0] = SDL_min(lo[0], r);
        hi[0] = SDL_max(hi[0], r);
        lo[1] = SDL_min(lo[1], g);
        hi[1] = SDL_max(hi[1], g);
        lo[2] = SDL_min(lo[2], b);
        hi[2] = SDL_max(hi[2], b);
      }
    }
  }

  box->count = count;

  if (count > 0)
  {
    for (int i = 0; i < 3; i++)
    {
      box->lo[i] = lo[i];
      box->hi[i] = hi[i];
    }
  }
}

// How far the box spans along `axis`, measured in 8-bit steps so that the
// green channel's extra bit does not make it look twice as long.
static int spanOf(const Box *box, int axis)
{
  static const int scale[3] = {8, 4, 8};

  return (box->hi[axis] - box->lo[axis]) * scale[axis];
}

static int longestAxis(const Box *box)
{
  int axis = 0;

  for (int i = 1; i < 3; i++)
  {
    if (spanOf(box, i) > spanOf(box, axis))
    {
      axis = i;
    }
  }

  return axis;
}

// Splits `box` at the median of its pixels along its longest side, writing the
// upper half to `upper`. False when there is nothing left to split.
static bool splitBox(Box *box, Box *upper, const Uint32 *hist)
{
  int axis = longestAxis(box);

  if (box->hi[axis] <= box->lo[axis])
  {
    return false;
  }

  Uint64 slices[64] = {0};

  for (int r = box->lo[0]; r <= box->hi[0]; r++)
  {
    for (int g = box->lo[1]; g <= box->hi[1]; g++)
    {
      for (int b = box->lo[2]; b <= box->hi[2]; b++)
      {
        int at[3] = {r, g, b};
        slices[at[axis] - box->lo[axis]] += hist[index565(r, g, b)];
      }
    }
  }

  // The last slice that keeps the lower half at or under half the pixels, but
  // never the box's own top - both halves have to hold something.
  Uint64 half = box->count / 2;
  Uint64 running = 0;
  int cut = box->lo[axis];

  for (int v = box->lo[axis]; v < box->hi[axis]; v++)
  {
    running += slices[v - box->lo[axis]];
    cut = v;

    if (running >= half)
    {
      break;
    }
  }

  *upper = *box;
  box->hi[axis] = cut;
  upper->lo[axis] = cut + 1;

  shrinkBox(box, hist);
  shrinkBox(upper, hist);

  return box->count > 0 && upper->count > 0;
}

void gifBuildPalette(const Uint16 *const *frames, int frameCount, int pixelsPerFrame,
                     Uint8 palette[GIF_PALETTE_SIZE * 3], Uint8 lookup[65536])
{
  Uint32 *hist = (Uint32 *)SDL_calloc(65536, sizeof(Uint32));

  SDL_memset(palette, 0, GIF_PALETTE_SIZE * 3);

  if (hist == NULL)
  {
    return;
  }

  for (int f = 0; f < frameCount; f++)
  {
    for (int i = 0; i < pixelsPerFrame; i++)
    {
      hist[frames[f][i]]++;
    }
  }

  Box boxes[GIF_PALETTE_SIZE];
  int boxCount = 1;

  boxes[0] = (Box){{0, 0, 0}, {31, 63, 31}, 0};
  shrinkBox(&boxes[0], hist);

  // Splits the box that most needs it - the one whose pixels are both many and
  // spread out - until the palette is full or nothing will split any further.
  while (boxCount < GIF_PALETTE_SIZE)
  {
    int best = -1;
    Uint64 bestScore = 0;

    for (int i = 0; i < boxCount; i++)
    {
      int span = spanOf(&boxes[i], longestAxis(&boxes[i]));
      Uint64 score = boxes[i].count * (Uint64)span;

      if (span > 0 && score > bestScore)
      {
        best = i;
        bestScore = score;
      }
    }

    if (best < 0 || !splitBox(&boxes[best], &boxes[boxCount], hist))
    {
      break;
    }

    boxCount++;
  }

  // Each entry is the pixel-weighted mean of its box, in 8-bit channels.
  for (int i = 0; i < boxCount; i++)
  {
    const Box *box = &boxes[i];
    Uint64 sum[3] = {0, 0, 0};
    Uint64 total = 0;

    for (int r = box->lo[0]; r <= box->hi[0]; r++)
    {
      for (int g = box->lo[1]; g <= box->hi[1]; g++)
      {
        for (int b = box->lo[2]; b <= box->hi[2]; b++)
        {
          Uint64 n = hist[index565(r, g, b)];

          sum[0] += n * (Uint64)((r << 3) | (r >> 2));
          sum[1] += n * (Uint64)((g << 2) | (g >> 4));
          sum[2] += n * (Uint64)((b << 3) | (b >> 2));
          total += n;
        }
      }
    }

    for (int c = 0; c < 3; c++)
    {
      palette[i * 3 + c] = total > 0 ? (Uint8)(sum[c] / total) : 0;
    }
  }

  // Nearest entry for every value that occurs. A value inside a box is
  // usually nearest that box's mean, but not always - a long thin box's mean
  // can sit closer to a neighbour's pixels - so this measures rather than
  // assumes.
  for (int v = 0; v < 65536; v++)
  {
    if (hist[v] == 0)
    {
      continue;
    }

    int r = ((v >> 11) & 31) << 3;
    int g = ((v >> 5) & 63) << 2;
    int b = (v & 31) << 3;
    int bestIndex = 0;
    int bestDist = 1 << 30;

    for (int i = 0; i < boxCount; i++)
    {
      int dr = r - palette[i * 3];
      int dg = g - palette[i * 3 + 1];
      int db = b - palette[i * 3 + 2];
      int dist = 2 * dr * dr + 4 * dg * dg + 3 * db * db;

      if (dist < bestDist)
      {
        bestDist = dist;
        bestIndex = i;
      }
    }

    lookup[v] = (Uint8)bestIndex;
  }

  SDL_free(hist);
}

// ---------------------------------------------------------------------------
// LZW, as GIF wants it: variable-width codes from 9 to 12 bits, packed
// least-significant bit first into sub-blocks of at most 255 bytes.
// ---------------------------------------------------------------------------

#define MIN_CODE_SIZE 8
#define CLEAR_CODE 256
#define END_CODE 257
#define FIRST_CODE 258
#define MAX_CODES 4096

// Twice the table, so that linear probing stays short.
#define HASH_SIZE 8192

typedef struct BitWriter
{
  SDL_IOStream *out;
  Uint8 block[255];
  int blockLen;
  Uint32 bits;
  int bitCount;
  bool ok;
} BitWriter;

static void writeBytes(BitWriter *w, const void *data, size_t size)
{
  if (w->ok && SDL_WriteIO(w->out, data, size) != size)
  {
    w->ok = false;
  }
}

static void flushBlock(BitWriter *w)
{
  if (w->blockLen == 0)
  {
    return;
  }

  Uint8 len = (Uint8)w->blockLen;

  writeBytes(w, &len, 1);
  writeBytes(w, w->block, (size_t)w->blockLen);
  w->blockLen = 0;
}

static void pushByte(BitWriter *w, Uint8 byte)
{
  w->block[w->blockLen++] = byte;

  if (w->blockLen == 255)
  {
    flushBlock(w);
  }
}

static void putCode(BitWriter *w, int code, int size)
{
  w->bits |= (Uint32)code << w->bitCount;
  w->bitCount += size;

  while (w->bitCount >= 8)
  {
    pushByte(w, (Uint8)(w->bits & 0xFF));
    w->bits >>= 8;
    w->bitCount -= 8;
  }
}

typedef struct LzwTable
{
  Sint32 keys[HASH_SIZE];
  Uint16 codes[HASH_SIZE];
} LzwTable;

static void resetTable(LzwTable *table)
{
  for (int i = 0; i < HASH_SIZE; i++)
  {
    table->keys[i] = -1;
  }
}

static bool encodeImage(SDL_IOStream *out, const Uint8 *pixels, int count, LzwTable *table)
{
  BitWriter w = {out, {0}, 0, 0, 0, true};
  Uint8 minCodeSize = MIN_CODE_SIZE;

  writeBytes(&w, &minCodeSize, 1);

  resetTable(table);

  int codeSize = MIN_CODE_SIZE + 1;
  int next = FIRST_CODE;

  putCode(&w, CLEAR_CODE, codeSize);

  int prefix = pixels[0];

  for (int i = 1; i < count; i++)
  {
    int c = pixels[i];
    Sint32 key = (prefix << 8) | c;
    Uint32 h = ((Uint32)key * 2654435761u) >> 19; // 13 bits: HASH_SIZE

    while (table->keys[h] != -1 && table->keys[h] != key)
    {
      h = (h + 1) & (HASH_SIZE - 1);
    }

    if (table->keys[h] == key)
    {
      prefix = table->codes[h];
      continue;
    }

    putCode(&w, prefix, codeSize);

    // The encoder's table runs one entry ahead of the decoder's, which only
    // adds an entry once it has seen the code after this one. So the width
    // goes up when `next` has passed the edge rather than reached it - the
    // decoder will reach it on reading the next code.
    table->keys[h] = key;
    table->codes[h] = (Uint16)next++;

    if (next > (1 << codeSize) && codeSize < 12)
    {
      codeSize++;
    }

    if (next == MAX_CODES)
    {
      putCode(&w, CLEAR_CODE, codeSize);
      resetTable(table);
      codeSize = MIN_CODE_SIZE + 1;
      next = FIRST_CODE;
    }

    prefix = c;
  }

  putCode(&w, prefix, codeSize);

  // The decoder adds an entry for that last code before it reads the end code,
  // and may widen for it; the end code has to be written at the width it will
  // be read at.
  next++;

  if (next > (1 << codeSize) && codeSize < 12)
  {
    codeSize++;
  }

  putCode(&w, END_CODE, codeSize);

  if (w.bitCount > 0)
  {
    pushByte(&w, (Uint8)(w.bits & 0xFF));
  }

  flushBlock(&w);

  Uint8 terminator = 0;
  writeBytes(&w, &terminator, 1);

  return w.ok;
}

static void le16(Uint8 *out, int value)
{
  out[0] = (Uint8)(value & 0xFF);
  out[1] = (Uint8)((value >> 8) & 0xFF);
}

bool gifWrite(SDL_IOStream *out, int width, int height,
              const Uint8 palette[GIF_PALETTE_SIZE * 3],
              const GifFrame *frames, int count)
{
  if (out == NULL || width <= 0 || height <= 0 || count <= 0)
  {
    return false;
  }

  LzwTable *table = (LzwTable *)SDL_malloc(sizeof(LzwTable));

  if (table == NULL)
  {
    return false;
  }

  bool ok = true;

  // Header and logical screen: a global colour table of 256 entries, 8 bits a
  // channel, no background to speak of.
  Uint8 header[13] = {'G', 'I', 'F', '8', '9', 'a'};
  le16(&header[6], width);
  le16(&header[8], height);
  header[10] = 0xF7;
  header[11] = 0;
  header[12] = 0;

  ok = ok && SDL_WriteIO(out, header, sizeof(header)) == sizeof(header);
  ok = ok && SDL_WriteIO(out, palette, GIF_PALETTE_SIZE * 3) == GIF_PALETTE_SIZE * 3;

  // NETSCAPE2.0: loop forever.
  static const Uint8 loop[19] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P',
                                 'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00};
  ok = ok && SDL_WriteIO(out, loop, sizeof(loop)) == sizeof(loop);

  for (int f = 0; f < count && ok; f++)
  {
    // Graphic control: leave the frame in place, no transparency.
    Uint8 control[8] = {0x21, 0xF9, 0x04, 0x04, 0, 0, 0, 0};
    le16(&control[4], SDL_max(frames[f].delayCs, 2));

    // Image descriptor: the whole screen, no local palette, not interlaced.
    Uint8 image[10] = {0x2C, 0, 0, 0, 0};
    le16(&image[5], width);
    le16(&image[7], height);
    image[9] = 0;

    ok = ok && SDL_WriteIO(out, control, sizeof(control)) == sizeof(control);
    ok = ok && SDL_WriteIO(out, image, sizeof(image)) == sizeof(image);
    ok = ok && encodeImage(out, frames[f].pixels, width * height, table);
  }

  Uint8 trailer = 0x3B;
  ok = ok && SDL_WriteIO(out, &trailer, 1) == 1;

  SDL_free(table);

  return ok;
}
