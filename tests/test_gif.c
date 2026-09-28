// The GIF writer behind the clip recorder, checked by decoding what it wrote.
//
// LZW is where a GIF encoder goes wrong, and it goes wrong quietly: the code
// width has to step up at exactly the moment the decoder's does, one entry
// behind the encoder's own table, and a mistake there produces a file that
// opens fine and shows garbage from some pixel onwards - or only in some
// viewers. So this carries a decoder written from the specification rather
// than from the encoder, and every image goes through the round trip: noise
// that fills the table and forces the clear codes, a flat field that makes
// strings as long as they get, and the edge cases at one pixel.

#include "test.h"

#include "../src/globals.h"
#include "../src/lib/gif.h"

#include <stdlib.h>

// ---------------------------------------------------------------------------
// A growable in-memory stream, through SDL_OpenIO so that nothing here needs
// SDL initialized.
// ---------------------------------------------------------------------------

typedef struct Sink
{
  Uint8 *data;
  size_t size;
  size_t capacity;
} Sink;

static size_t SDLCALL sinkWrite(void *userdata, const void *ptr, size_t size,
                                SDL_IOStatus *status)
{
  Sink *sink = (Sink *)userdata;

  if (sink->size + size > sink->capacity)
  {
    size_t capacity = sink->capacity > 0 ? sink->capacity : 4096;

    while (capacity < sink->size + size)
    {
      capacity *= 2;
    }

    Uint8 *grown = (Uint8 *)realloc(sink->data, capacity);

    if (grown == NULL)
    {
      *status = SDL_IO_STATUS_ERROR;
      return 0;
    }

    sink->data = grown;
    sink->capacity = capacity;
  }

  memcpy(sink->data + sink->size, ptr, size);
  sink->size += size;

  return size;
}

static bool SDLCALL sinkClose(UNUSED void *userdata)
{
  return true;
}

static SDL_IOStream *openSink(Sink *sink)
{
  SDL_IOStreamInterface iface;

  SDL_INIT_INTERFACE(&iface);
  iface.write = sinkWrite;
  iface.close = sinkClose;

  return SDL_OpenIO(&iface, sink);
}

// ---------------------------------------------------------------------------
// The decoder
// ---------------------------------------------------------------------------

typedef struct Reader
{
  const Uint8 *data;
  size_t size;
  size_t pos;
  bool ok;
} Reader;

static int readByte(Reader *r)
{
  if (r->pos >= r->size)
  {
    r->ok = false;
    return 0;
  }

  return r->data[r->pos++];
}

static int readLe16(Reader *r)
{
  int lo = readByte(r);
  return lo | (readByte(r) << 8);
}

// Concatenates a run of sub-blocks, up to the empty one that ends them.
static Uint8 *readSubBlocks(Reader *r, size_t *outSize)
{
  size_t size = 0, capacity = 1024;
  Uint8 *out = (Uint8 *)malloc(capacity);

  for (;;)
  {
    int len = readByte(r);

    if (len == 0 || !r->ok)
    {
      break;
    }

    if (size + (size_t)len > capacity)
    {
      capacity = (size + (size_t)len) * 2;
      out = (Uint8 *)realloc(out, capacity);
    }

    for (int i = 0; i < len; i++)
    {
      out[size++] = (Uint8)readByte(r);
    }
  }

  *outSize = size;

  return out;
}

// LZW as the GIF specification describes decoding it. Returns how many pixels
// it produced, or -1 on a code that cannot occur in a valid stream.
static int lzwDecode(const Uint8 *data, size_t size, int minCodeSize, Uint8 *out, int maxOut)
{
  int clear = 1 << minCodeSize;
  int end = clear + 1;
  int codeSize = minCodeSize + 1;
  int next = end + 1;
  int prev = -1;

  static int prefix[4096];
  static Uint8 suffix[4096];
  static Uint8 stack[4096];

  for (int i = 0; i < clear; i++)
  {
    prefix[i] = -1;
    suffix[i] = (Uint8)i;
  }

  size_t bitPos = 0;
  int produced = 0;

  for (;;)
  {
    if (bitPos + (size_t)codeSize > size * 8)
    {
      return -1; // ran out before the end code
    }

    int code = 0;

    for (int b = 0; b < codeSize; b++, bitPos++)
    {
      if (data[bitPos / 8] & (1 << (bitPos % 8)))
      {
        code |= 1 << b;
      }
    }

    if (code == clear)
    {
      codeSize = minCodeSize + 1;
      next = end + 1;
      prev = -1;
      continue;
    }

    if (code == end)
    {
      return produced;
    }

    if (prev < 0)
    {
      if (code >= clear || produced >= maxOut)
      {
        return -1;
      }

      out[produced++] = (Uint8)code;
      prev = code;
      continue;
    }

    int emit;

    if (code < next)
    {
      emit = code;
    }
    else if (code == next)
    {
      emit = -1; // the KwKwK case: prev's string plus its own first byte
    }
    else
    {
      return -1;
    }

    // First byte of prev's string, for the KwKwK case and for the new entry.
    int first;
    int walk = emit >= 0 ? emit : prev;

    while (prefix[walk] >= 0)
    {
      walk = prefix[walk];
    }

    first = suffix[walk];

    if (next < 4096)
    {
      prefix[next] = prev;
      suffix[next] = (Uint8)first;
      next++;
    }

    int depth = 0;

    for (int c = emit >= 0 ? emit : next - 1; c >= 0; c = prefix[c])
    {
      stack[depth++] = suffix[c];
    }

    while (depth > 0)
    {
      if (produced >= maxOut)
      {
        return -1;
      }

      out[produced++] = stack[--depth];
    }

    if (next == (1 << codeSize) && codeSize < 12)
    {
      codeSize++;
    }

    prev = code;
  }
}

typedef struct Decoded
{
  bool ok;
  int width, height;
  int frames;
  Uint8 palette[768];
  Uint8 *pixels[8];
  int delays[8];
  bool loops;
} Decoded;

static Decoded decode(const Uint8 *data, size_t size)
{
  Decoded d;
  memset(&d, 0, sizeof(d));

  Reader r = {data, size, 0, true};

  if (size < 13 || memcmp(data, "GIF89a", 6) != 0)
  {
    return d;
  }

  r.pos = 6;
  d.width = readLe16(&r);
  d.height = readLe16(&r);
  int packed = readByte(&r);
  readByte(&r);
  readByte(&r);

  if (packed & 0x80)
  {
    int entries = 1 << ((packed & 7) + 1);

    for (int i = 0; i < entries * 3; i++)
    {
      int v = readByte(&r);

      if (i < 768)
      {
        d.palette[i] = (Uint8)v;
      }
    }
  }

  int delay = 0;

  while (r.ok)
  {
    int block = readByte(&r);

    if (block == 0x3B)
    {
      d.ok = r.ok;
      return d;
    }

    if (block == 0x21)
    {
      int label = readByte(&r);
      size_t extSize;
      Uint8 *ext = readSubBlocks(&r, &extSize);

      if (label == 0xF9 && extSize >= 3)
      {
        delay = ext[1] | (ext[2] << 8);
      }
      else if (label == 0xFF && extSize >= 11 && memcmp(ext, "NETSCAPE2.0", 11) == 0)
      {
        d.loops = true;
      }

      free(ext);
      continue;
    }

    if (block != 0x2C || d.frames >= 8)
    {
      return d;
    }

    readLe16(&r);
    readLe16(&r);
    int w = readLe16(&r);
    int h = readLe16(&r);
    int imagePacked = readByte(&r);

    if (imagePacked & 0x80 || w != d.width || h != d.height)
    {
      return d;
    }

    int minCodeSize = readByte(&r);
    size_t lzwSize;
    Uint8 *lzw = readSubBlocks(&r, &lzwSize);
    Uint8 *pixels = (Uint8 *)malloc((size_t)w * h);
    int produced = lzwDecode(lzw, lzwSize, minCodeSize, pixels, w * h);

    free(lzw);

    if (produced != w * h)
    {
      free(pixels);
      return d;
    }

    d.pixels[d.frames] = pixels;
    d.delays[d.frames] = delay;
    d.frames++;
  }

  return d;
}

static void freeDecoded(Decoded *d)
{
  for (int i = 0; i < d->frames; i++)
  {
    free(d->pixels[i]);
  }
}

// Encodes `frames` and decodes the result; true if every pixel came back.
static bool roundTrip(int w, int h, Uint8 *const *frames, const int *delays, int count)
{
  Uint8 palette[768];

  for (int i = 0; i < 768; i++)
  {
    palette[i] = (Uint8)(i * 7);
  }

  GifFrame gifFrames[8];

  for (int i = 0; i < count; i++)
  {
    gifFrames[i] = (GifFrame){frames[i], delays[i]};
  }

  Sink sink = {NULL, 0, 0};
  SDL_IOStream *io = openSink(&sink);
  bool written = gifWrite(io, w, h, palette, gifFrames, count);
  SDL_CloseIO(io);

  if (!written)
  {
    free(sink.data);
    return false;
  }

  Decoded d = decode(sink.data, sink.size);
  bool same = d.ok && d.width == w && d.height == h && d.frames == count && d.loops &&
              memcmp(d.palette, palette, 768) == 0;

  for (int i = 0; same && i < count; i++)
  {
    same = memcmp(d.pixels[i], frames[i], (size_t)w * h) == 0 &&
           d.delays[i] == SDL_max(delays[i], 2);
  }

  freeDecoded(&d);
  free(sink.data);

  return same;
}

static void testLzw(void)
{
  TEST_GROUP("gif: what the encoder writes, a decoder reads back exactly");

  // One pixel, and two.
  Uint8 one[1] = {200};
  Uint8 *oneFrames[1] = {one};
  int delay[8] = {7, 7, 6, 7, 7, 6, 1, 250};
  CHECK(roundTrip(1, 1, oneFrames, delay, 1));

  Uint8 two[2] = {3, 3};
  Uint8 *twoFrames[1] = {two};
  CHECK(roundTrip(2, 1, twoFrames, delay, 1));

  // A flat field: strings grow as long as they can, and KwKwK every time.
  Uint8 *flat = (Uint8 *)calloc(400 * 300, 1);
  Uint8 *flatFrames[1] = {flat};
  CHECK(roundTrip(400, 300, flatFrames, delay, 1));

  // Noise over all 256 values: the table fills in a few thousand pixels, so
  // this goes through every code width and a great many clear codes.
  Uint8 *noise = (Uint8 *)malloc(400 * 300);
  Uint32 x = 2463534242u;

  for (int i = 0; i < 400 * 300; i++)
  {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    noise[i] = (Uint8)(x >> 24);
  }

  Uint8 *noiseFrames[1] = {noise};
  CHECK(roundTrip(400, 300, noiseFrames, delay, 1));

  // Something like a frame of the game: bands and gradients with a little
  // noise, over several frames with their own delays. The table crosses each
  // width boundary at a different pixel in each.
  Uint8 *frames[4];

  for (int f = 0; f < 4; f++)
  {
    frames[f] = (Uint8 *)malloc(123 * 77);

    for (int i = 0; i < 123 * 77; i++)
    {
      int px = i % 123, py = i / 123;
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      frames[f][i] = (Uint8)((px / 9 + py / 5 + f * 3) % 40 + ((x >> 28) == 0 ? 100 : 0));
    }
  }

  CHECK(roundTrip(123, 77, frames, delay, 4));

  for (int f = 0; f < 4; f++)
  {
    free(frames[f]);
  }

  free(flat);
  free(noise);
}

static void testPalette(void)
{
  TEST_GROUP("gif: the palette lands on the colours the clip is made of");

  // Three colours, exactly: each maps to an entry that is that colour.
  Uint16 few[6] = {0xF800, 0x07E0, 0x001F, 0xF800, 0xF800, 0x07E0};
  const Uint16 *fewFrames[1] = {few};
  Uint8 palette[768];
  Uint8 *lookup = (Uint8 *)calloc(65536, 1);

  gifBuildPalette(fewFrames, 1, 6, palette, lookup);

  const Uint8 *red = &palette[lookup[0xF800] * 3];
  const Uint8 *green = &palette[lookup[0x07E0] * 3];
  const Uint8 *blue = &palette[lookup[0x001F] * 3];

  CHECK(red[0] == 255 && red[1] == 0 && red[2] == 0);
  CHECK(green[0] == 0 && green[1] == 255 && green[2] == 0);
  CHECK(blue[0] == 0 && blue[1] == 0 && blue[2] == 255);

  // A smooth gradient of far more than 256 values: every pixel maps to an
  // entry, and never to one far from its own colour.
  enum { N = 40000 };
  Uint16 *ramp = (Uint16 *)malloc(sizeof(Uint16) * N);

  for (int i = 0; i < N; i++)
  {
    int r = (i / 7) % 32, g = (i / 3) % 64, b = (i * 5 / N) % 32;
    ramp[i] = (Uint16)((r << 11) | (g << 5) | b);
  }

  const Uint16 *rampFrames[1] = {ramp};
  gifBuildPalette(rampFrames, 1, N, palette, lookup);

  int worst = 0;

  for (int i = 0; i < N; i++)
  {
    int v = ramp[i];
    const Uint8 *entry = &palette[lookup[v] * 3];
    int dr = abs(((v >> 11) << 3) - entry[0]);
    int dg = abs((((v >> 5) & 63) << 2) - entry[1]);
    int db = abs(((v & 31) << 3) - entry[2]);
    worst = SDL_max(worst, SDL_max(dr, SDL_max(dg, db)));
  }

  CHECK(worst <= 48);

  free(ramp);
  free(lookup);
}

void testGif(void)
{
  testLzw();
  testPalette();
}
