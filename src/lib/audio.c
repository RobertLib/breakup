#include "audio.h"
#include "../embedded_assets.h"
#include "../level-manager.h"

#define SAMPLE_RATE 44100
#define SFX_TRACK_POOL 10

static MIX_Audio *sfxAudio[SFX_COUNT];
static MIX_Audio *musicAudio[MUSIC_COUNT];

static MIX_Track *musicTrack;
static MIX_Track *sfxTracks[SFX_TRACK_POOL];
static int nextSfxTrack;

static MusicTrack currentMusic = MUSIC_NONE;

static float musicVolume = 0.7f;
static float sfxVolume = 0.8f;

// ---------------------------------------------------------------------------
// Synth core
//
// Sound effects only. The music is nineteen Ogg Vorbis files embedded in the
// binary (see the Music section below); what is synthesized here is the
// hundred-odd milliseconds of a brick breaking, which no file could carry at
// the pitch a combo asks for.
// ---------------------------------------------------------------------------

typedef enum Wave
{
  W_SINE,
  W_SQUARE,
  W_SAW,
  W_TRI,
  W_NOISE
} Wave;

static unsigned int noiseState = 0x12345678;

static float noiseSample(void)
{
  noiseState ^= noiseState << 13;
  noiseState ^= noiseState >> 17;
  noiseState ^= noiseState << 5;
  return ((noiseState & 0xffffff) / 8388608.0f) - 1.0f;
}

// Render one tone into the float buffer.
// f0 slides to f1 over the duration. Envelope: linear attack, exponential decay.
static void tone(float *buf, int bufLen, double start, double dur,
                 float f0, float f1, Wave wave, float vol,
                 float attack, float decay, float vibrato)
{
  int s0 = (int)(start * SAMPLE_RATE);
  int n = (int)(dur * SAMPLE_RATE);
  double phase = 0;

  for (int i = 0; i < n; i++)
  {
    int idx = s0 + i;
    if (idx < 0 || idx >= bufLen)
    {
      continue;
    }

    float t = i / (float)SAMPLE_RATE;
    float progress = i / (float)n;
    float freq = f0 + (f1 - f0) * progress;

    if (vibrato > 0)
    {
      freq *= 1.0f + vibrato * sinf(t * 6.0f * 2.0f * SDL_PI_F);
    }

    phase += 2.0 * SDL_PI_D * freq / SAMPLE_RATE;

    float sample = 0;
    switch (wave)
    {
    case W_SINE:
      sample = sinf((float)phase);
      break;
    case W_SQUARE:
      // Soft square: rounded edges, less harsh than a hard clip
      sample = tanhf(3.0f * sinf((float)phase));
      break;
    case W_SAW:
    {
      float p = (float)(phase / (2.0 * SDL_PI_D));
      sample = 2.0f * (p - floorf(p)) - 1.0f;
      sample = tanhf(1.5f * sample);
      break;
    }
    case W_TRI:
      sample = (2.0f / SDL_PI_F) * asinf(sinf((float)phase));
      break;
    case W_NOISE:
      sample = noiseSample();
      break;
    }

    float env = 1.0f;
    if (attack > 0 && t < attack)
    {
      env = t / attack;
    }
    env *= expf(-decay * t);

    // Short fade-out at the very end to avoid clicks
    int left = n - i;
    if (left < 220)
    {
      env *= left / 220.0f;
    }

    buf[idx] += sample * vol * env;
  }
}

static float midiFreq(int midi)
{
  return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

// Simple feedback delay — instant depth for melodic content
static void applyEcho(float *buf, int n, double delaySec, float feedback)
{
  int d = (int)(delaySec * SAMPLE_RATE);

  for (int i = d; i < n; i++)
  {
    buf[i] += buf[i - d] * feedback;
  }
}

// Convert float buffer to a MIX_Audio (16-bit mono), with soft limiting
static MIX_Audio *finishBuffer(float *buf, int n)
{
#ifdef DEBUG
  {
    float peak = 0, sum = 0;
    for (int i = 0; i < n; i++)
    {
      float a = fabsf(buf[i]);
      if (a > peak) peak = a;
      sum += buf[i] * buf[i];
    }
    printf("audio buffer: %.2fs peak=%.2f rms=%.3f\n",
           n / (float)SAMPLE_RATE, peak, sqrtf(sum / n));
  }
#endif

  Sint16 *pcm = (Sint16 *)SDL_malloc((size_t)n * sizeof(Sint16));

  if (pcm == NULL)
  {
    SDL_free(buf);
    return NULL;
  }

  for (int i = 0; i < n; i++)
  {
    float v = tanhf(buf[i] * 1.2f);
    pcm[i] = (Sint16)(v * 32000.0f);
  }

  SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, SAMPLE_RATE};
  MIX_Audio *audio = MIX_LoadRawAudio(mixer, pcm, (size_t)n * sizeof(Sint16), &spec);

  if (audio == NULL)
  {
    fprintf(stderr, "Unable to load synthesized sound! SDL_mixer Error: %s\n", SDL_GetError());
  }

  SDL_free(pcm);
  SDL_free(buf);

  return audio;
}

static float *newBuffer(double seconds, int *outLen)
{
  int n = (int)(seconds * SAMPLE_RATE);
  float *buf = (float *)SDL_calloc((size_t)n, sizeof(float));

  if (buf == NULL)
  {
    fprintf(stderr, "Audio buffer allocation failed.\n");
    exit(1);
  }

  *outLen = n;
  return buf;
}

// ---------------------------------------------------------------------------
// Sound effects
// ---------------------------------------------------------------------------

static MIX_Audio *makeSfx(Sfx which)
{
  float *buf;
  int n;

  switch (which)
  {
  case SFX_BALL_PADDLE:
    buf = newBuffer(0.09, &n);
    tone(buf, n, 0, 0.07, 320, 210, W_TRI, 0.55f, 0.001f, 26.0f, 0);
    tone(buf, n, 0, 0.04, 640, 500, W_SINE, 0.2f, 0.001f, 40.0f, 0);
    break;

  case SFX_BALL_WALL:
    buf = newBuffer(0.06, &n);
    tone(buf, n, 0, 0.05, 190, 150, W_SINE, 0.4f, 0.001f, 40.0f, 0);
    break;

  case SFX_BRICK_HIT:
    buf = newBuffer(0.07, &n);
    tone(buf, n, 0, 0.05, 480, 430, W_SQUARE, 0.3f, 0.001f, 45.0f, 0);
    tone(buf, n, 0, 0.02, 0, 0, W_NOISE, 0.12f, 0.001f, 90.0f, 0);
    break;

  case SFX_BRICK_BREAK:
    buf = newBuffer(0.12, &n);
    tone(buf, n, 0, 0.09, 720, 380, W_SQUARE, 0.35f, 0.001f, 30.0f, 0);
    tone(buf, n, 0, 0.05, 0, 0, W_NOISE, 0.22f, 0.001f, 50.0f, 0);
    break;

  case SFX_EXPLOSION:
    buf = newBuffer(0.55, &n);
    tone(buf, n, 0, 0.5, 0, 0, W_NOISE, 0.55f, 0.002f, 9.0f, 0);
    tone(buf, n, 0, 0.4, 100, 34, W_SINE, 0.7f, 0.002f, 9.0f, 0);
    break;

  case SFX_GOLD:
    buf = newBuffer(0.3, &n);
    tone(buf, n, 0.0, 0.12, 880, 880, W_SINE, 0.3f, 0.002f, 14.0f, 0);
    tone(buf, n, 0.07, 0.18, 1318, 1318, W_SINE, 0.3f, 0.002f, 12.0f, 0);
    tone(buf, n, 0.13, 0.15, 1760, 1760, W_SINE, 0.2f, 0.002f, 12.0f, 0);
    break;

  case SFX_CRYSTAL:
    buf = newBuffer(0.7, &n);
    tone(buf, n, 0.0, 0.6, 1046, 1046, W_SINE, 0.3f, 0.003f, 5.0f, 0.004f);
    tone(buf, n, 0.05, 0.55, 1568, 1568, W_SINE, 0.22f, 0.003f, 5.0f, 0.004f);
    tone(buf, n, 0.1, 0.5, 2093, 2093, W_SINE, 0.13f, 0.003f, 6.0f, 0.004f);
    break;

  case SFX_POWERUP_GOOD:
  {
    buf = newBuffer(0.4, &n);
    const int notes[4] = {72, 76, 79, 84}; // C5 E5 G5 C6
    for (int i = 0; i < 4; i++)
    {
      tone(buf, n, i * 0.07, 0.12, midiFreq(notes[i]), midiFreq(notes[i]),
           W_SQUARE, 0.22f, 0.002f, 16.0f, 0);
    }
    break;
  }

  case SFX_POWERUP_BAD:
    buf = newBuffer(0.35, &n);
    tone(buf, n, 0.0, 0.15, 420, 280, W_SAW, 0.3f, 0.002f, 10.0f, 0);
    tone(buf, n, 0.14, 0.2, 280, 150, W_SAW, 0.3f, 0.002f, 10.0f, 0);
    break;

  case SFX_SHOOT:
    buf = newBuffer(0.14, &n);
    tone(buf, n, 0, 0.12, 950, 180, W_SAW, 0.3f, 0.001f, 16.0f, 0);
    break;

  case SFX_ENEMY_HIT:
    buf = newBuffer(0.28, &n);
    tone(buf, n, 0, 0.22, 340, 70, W_SQUARE, 0.35f, 0.001f, 12.0f, 0);
    tone(buf, n, 0, 0.12, 0, 0, W_NOISE, 0.2f, 0.001f, 22.0f, 0);
    break;

  case SFX_LIFE_LOST:
  {
    buf = newBuffer(0.9, &n);
    const int notes[3] = {69, 65, 62}; // A4 F4 D4
    for (int i = 0; i < 3; i++)
    {
      tone(buf, n, i * 0.18, 0.3, midiFreq(notes[i]), midiFreq(notes[i]),
           W_TRI, 0.35f, 0.004f, 7.0f, 0.006f);
    }
    tone(buf, n, 0.5, 0.35, 80, 45, W_SINE, 0.4f, 0.01f, 8.0f, 0);
    break;
  }

  case SFX_LEVEL_COMPLETE:
  {
    buf = newBuffer(1.4, &n);
    const int notes[4] = {72, 76, 79, 84};
    for (int i = 0; i < 4; i++)
    {
      tone(buf, n, i * 0.11, 0.16, midiFreq(notes[i]), midiFreq(notes[i]),
           W_SQUARE, 0.25f, 0.002f, 10.0f, 0);
    }
    // Closing chord
    tone(buf, n, 0.5, 0.8, midiFreq(72), midiFreq(72), W_TRI, 0.22f, 0.01f, 4.0f, 0.004f);
    tone(buf, n, 0.5, 0.8, midiFreq(76), midiFreq(76), W_TRI, 0.18f, 0.01f, 4.0f, 0.004f);
    tone(buf, n, 0.5, 0.8, midiFreq(79), midiFreq(79), W_TRI, 0.18f, 0.01f, 4.0f, 0.004f);
    tone(buf, n, 0.5, 0.8, midiFreq(88), midiFreq(88), W_SINE, 0.14f, 0.01f, 4.0f, 0.004f);
    break;
  }

  case SFX_MENU_MOVE:
    buf = newBuffer(0.05, &n);
    tone(buf, n, 0, 0.04, 700, 700, W_SINE, 0.3f, 0.001f, 50.0f, 0);
    break;

  case SFX_MENU_SELECT:
    buf = newBuffer(0.15, &n);
    tone(buf, n, 0, 0.06, 520, 520, W_SQUARE, 0.25f, 0.001f, 20.0f, 0);
    tone(buf, n, 0.05, 0.09, 780, 780, W_SQUARE, 0.25f, 0.001f, 18.0f, 0);
    break;

  case SFX_SHIELD:
    buf = newBuffer(0.16, &n);
    tone(buf, n, 0, 0.14, 240, 520, W_TRI, 0.4f, 0.002f, 14.0f, 0);
    break;

  case SFX_CATCH:
    buf = newBuffer(0.1, &n);
    tone(buf, n, 0, 0.08, 620, 300, W_TRI, 0.35f, 0.001f, 20.0f, 0);
    break;

  case SFX_GAME_OVER:
  {
    buf = newBuffer(2.4, &n);
    const int melody[5] = {76, 72, 69, 64, 57}; // E5 C5 A4 E4 A3
    for (int i = 0; i < 5; i++)
    {
      tone(buf, n, i * 0.28, 0.5, midiFreq(melody[i]), midiFreq(melody[i]),
           W_TRI, 0.3f, 0.006f, 4.5f, 0.005f);
    }
    // Final minor chord
    tone(buf, n, 1.4, 0.9, midiFreq(45), midiFreq(45), W_SAW, 0.16f, 0.02f, 3.0f, 0);
    tone(buf, n, 1.4, 0.9, midiFreq(57), midiFreq(57), W_TRI, 0.2f, 0.02f, 3.0f, 0.004f);
    tone(buf, n, 1.4, 0.9, midiFreq(60), midiFreq(60), W_TRI, 0.18f, 0.02f, 3.0f, 0.004f);
    tone(buf, n, 1.4, 0.9, midiFreq(64), midiFreq(64), W_TRI, 0.18f, 0.02f, 3.0f, 0.004f);
    applyEcho(buf, n, 0.22, 0.25f);
    break;
  }

  case SFX_WIN:
  {
    buf = newBuffer(2.8, &n);
    const int melody[6] = {72, 76, 79, 84, 79, 84}; // triumphant C major runs
    for (int i = 0; i < 6; i++)
    {
      tone(buf, n, i * 0.16, 0.24, midiFreq(melody[i]), midiFreq(melody[i]),
           W_SQUARE, 0.22f, 0.003f, 8.0f, 0);
    }
    tone(buf, n, 1.1, 1.2, midiFreq(72), midiFreq(72), W_TRI, 0.22f, 0.02f, 2.5f, 0.005f);
    tone(buf, n, 1.1, 1.2, midiFreq(76), midiFreq(76), W_TRI, 0.2f, 0.02f, 2.5f, 0.005f);
    tone(buf, n, 1.1, 1.2, midiFreq(79), midiFreq(79), W_TRI, 0.2f, 0.02f, 2.5f, 0.005f);
    tone(buf, n, 1.1, 1.2, midiFreq(88), midiFreq(88), W_SINE, 0.16f, 0.02f, 2.5f, 0.005f);
    applyEcho(buf, n, 0.19, 0.28f);
    break;
  }

  default:
    buf = newBuffer(0.05, &n);
    break;
  }

  return finishBuffer(buf, n);
}

// ---------------------------------------------------------------------------
// Music
// ---------------------------------------------------------------------------

// One Ogg Vorbis file per slot, in MusicTrack order: the menu theme, a set of
// GAME_TRACKS_PER_WORLD per world for its levels to walk through, then the boss
// themes. embed_assets.sh puts every one of them inside the binary, so these
// strings are lookup keys first and paths on disk only in the fallback below.
//
// All of them are from the one album, which is what keeps nineteen tracks
// sounding like one soundtrack rather than a playlist. They are grouped by how
// they sit next to each other as much as by mood: world 3 has the loudest,
// most martial of them and world 2 the quietest, so that the volume does not
// step between levels of the same world.
static const char *const musicFiles[] = {
    "assets/music/menu.ogg",
    "assets/music/world1-a.ogg",
    "assets/music/world1-b.ogg",
    "assets/music/world1-c.ogg",
    "assets/music/world1-d.ogg",
    "assets/music/world2-a.ogg",
    "assets/music/world2-b.ogg",
    "assets/music/world2-c.ogg",
    "assets/music/world2-d.ogg",
    "assets/music/world3-a.ogg",
    "assets/music/world3-b.ogg",
    "assets/music/world3-c.ogg",
    "assets/music/world3-d.ogg",
    "assets/music/world4-a.ogg",
    "assets/music/world4-b.ogg",
    "assets/music/world4-c.ogg",
    "assets/music/world4-d.ogg",
    "assets/music/boss-a.ogg",
    "assets/music/boss-b.ogg",
};

// The table is indexed by MusicTrack. Without this, a fifth world added to
// WORLD_COUNT would compile, run, and play nothing at all on its levels - the
// kind of thing that is found by ear, a world later, by somebody else.
_Static_assert(SDL_arraysize(musicFiles) == MUSIC_COUNT,
               "musicFiles must hold exactly one file per MusicTrack");

static MIX_Audio *loadMusic(const char *path)
{
  const EmbeddedAsset *asset = findEmbeddedAsset(path);
  MIX_Audio *audio;

  if (asset != NULL)
  {
    // NoCopy: the track is already in the binary's read-only data and stays
    // there for the life of the process, so SDL_mixer decodes it where it lies,
    // a buffer at a time while mixing. The alternatives are both worse - a
    // second copy of eighteen megabytes on the heap, or predecoding the lot to
    // PCM, which is over a third of a gigabyte and a wait before the title
    // screen for music the player may never let finish its first bar.
    //
    // The `false` is `free_when_done`, not a predecode flag - this function has
    // no predecode option, precisely because it is for data that is already in
    // memory and staying there. It has to be false: `asset->data` points into
    // the executable's own read-only data, and SDL_free() on that is a crash on
    // the way out rather than a leak.
    audio = MIX_LoadAudioNoCopy(mixer, asset->data, asset->size, false);
  }
  else
  {
    // The same fallback the font and the levels have, for a tree whose assets
    // are beside the binary rather than inside it. `false` is the predecode
    // flag, and it stays false here for the reason above.
    audio = MIX_LoadAudio(mixer, path, false);
  }

  if (audio == NULL)
  {
    fprintf(stderr, "Unable to load music %s! SDL_mixer Error: %s\n",
            path, SDL_GetError());
  }

  return audio;
}

MusicTrack musicForLevel(int level)
{
  if (isBossLevel(level))
  {
    // Which theme a fight gets comes from its own `#boss N` number rather than
    // from where the level sits, so a given boss always sounds like itself -
    // inserting a level in front of it must not change its music. N is 1-based
    // and only a non-boss level answers 0, which isBossLevel() has ruled out.
    int boss = getLevel(level)->boss;

    return (MusicTrack)(MUSIC_BOSS_FIRST + (boss - 1) % BOSS_TRACK_COUNT);
  }

  int world = worldForLevel(level);

  // Counted inside the world, not across the whole game. The raw level index
  // looks like it cycles and does not: the boss fights sit between the
  // ordinary levels and shift the phase every time one goes by, which left
  // world two playing its first track on four of its five levels and its
  // second on one. levelInWorld() skips the bosses and is 1-based, so it is
  // already the number that should pick the track. It answers 0 only for a
  // level index that does not exist, which is not this function's to complain
  // about - it picks the first track of world 0 and lets the caller be wrong
  // somewhere louder.
  int inWorld = levelInWorld(level);
  int variant = (inWorld > 0 ? inWorld - 1 : 0) % GAME_TRACKS_PER_WORLD;

  return (MusicTrack)(MUSIC_GAME_FIRST + world * GAME_TRACKS_PER_WORLD + variant);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void initializeAudio(void)
{
  if (mixer == NULL)
  {
    return;
  }

  for (int i = 0; i < SFX_COUNT; i++)
  {
    sfxAudio[i] = makeSfx((Sfx)i);
  }

  for (int i = 0; i < MUSIC_COUNT; i++)
  {
    musicAudio[i] = loadMusic(musicFiles[i]);
  }

  musicTrack = MIX_CreateTrack(mixer);

  for (int i = 0; i < SFX_TRACK_POOL; i++)
  {
    sfxTracks[i] = MIX_CreateTrack(mixer);
  }
}

void destroyAudio(void)
{
  if (mixer == NULL)
  {
    return;
  }

  stopMusic();

  if (musicTrack != NULL)
  {
    MIX_DestroyTrack(musicTrack);
    musicTrack = NULL;
  }

  for (int i = 0; i < SFX_TRACK_POOL; i++)
  {
    if (sfxTracks[i] != NULL)
    {
      MIX_DestroyTrack(sfxTracks[i]);
      sfxTracks[i] = NULL;
    }
  }

  for (int i = 0; i < SFX_COUNT; i++)
  {
    if (sfxAudio[i] != NULL)
    {
      MIX_DestroyAudio(sfxAudio[i]);
      sfxAudio[i] = NULL;
    }
  }

  for (int i = 0; i < MUSIC_COUNT; i++)
  {
    if (musicAudio[i] != NULL)
    {
      MIX_DestroyAudio(musicAudio[i]);
      musicAudio[i] = NULL;
    }
  }
}

void playSfx(Sfx sfx)
{
  playSfxAt(sfx, 0);
}

void playSfxAt(Sfx sfx, float semitones)
{
  if (mixer == NULL || sfxAudio[sfx] == NULL || sfxVolume <= 0.001f)
  {
    return;
  }

  MIX_Track *track = sfxTracks[nextSfxTrack];
  nextSfxTrack = (nextSfxTrack + 1) % SFX_TRACK_POOL;

  // A mixer is not a promise of a track: MIX_CreateTrack() can fail on its own
  // in initializeAudio(), and every check up to here only asked about the mixer.
  if (track == NULL)
  {
    return;
  }

  MIX_SetTrackAudio(track, sfxAudio[sfx]);
  MIX_SetTrackGain(track, sfxVolume);

  // Set on every play, not only on the pitched ones: the pool hands the same
  // few tracks round and round, so a ratio left behind by a combo would go on
  // detuning whatever sound landed on that track next.
  MIX_SetTrackFrequencyRatio(track, powf(2.0f, semitones / 12.0f));

  MIX_PlayTrack(track, 0);
}

void playMusic(MusicTrack track)
{
  // musicTrack for the same reason playSfx() checks its own: it is a separate
  // MIX_CreateTrack() that can fail while the mixer itself is fine.
  if (mixer == NULL || musicTrack == NULL || track == MUSIC_NONE ||
      musicAudio[track] == NULL)
  {
    return;
  }

  if (currentMusic == track && MIX_TrackPlaying(musicTrack))
  {
    return;
  }

  MIX_StopTrack(musicTrack, 0);
  MIX_SetTrackAudio(musicTrack, musicAudio[track]);
  MIX_SetTrackGain(musicTrack, musicVolume);

  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
  MIX_PlayTrack(musicTrack, props);
  SDL_DestroyProperties(props);

  currentMusic = track;
}

void stopMusic(void)
{
  if (mixer == NULL || musicTrack == NULL)
  {
    return;
  }

  MIX_StopTrack(musicTrack, MIX_TrackMSToFrames(musicTrack, 300));
  currentMusic = MUSIC_NONE;
}

void setMusicVolume(float v)
{
  musicVolume = clamp(v, 0.0f, 1.0f);

  if (mixer != NULL && musicTrack != NULL)
  {
    MIX_SetTrackGain(musicTrack, musicVolume);
  }
}

void setSfxVolume(float v)
{
  sfxVolume = clamp(v, 0.0f, 1.0f);
}
