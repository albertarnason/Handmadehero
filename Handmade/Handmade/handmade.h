#if !defined(HANDMADE_H)
#include <stdint.h>

// implement sine ourselves
#include <math.h>
#define internal static
#define internal static
#define local_persist static
#define global_variable static 

#define Pi32 3.14159265359f
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;


typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32 bool32;

typedef float real32;
typedef double real64;
//services that the game provides to the platform layer below

// needs 4 things - user inputs, bitmap buffer to use, sound buffer to use, timing


struct game_offscreen_buffer
{
    void *Memory;
    int Width;
    int Height;
    int Pitch;
};

struct game_sound_output_buffer{
    int SamplesPerSecond;
    int SampleCount;
    int16 *Samples;
};
internal void GameUpdateAndRender(game_offscreen_buffer *Buffer, int XOffset, int YOffset, game_sound_output_buffer *SoundBuffer, int ToneHz);

//services that the platform layer provides to the game below

#define HANDMADE_H
#endif