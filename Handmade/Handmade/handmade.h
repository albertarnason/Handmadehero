#if !defined(HANDMADE_H)
#include <stdint.h>

// implement sine ourselves
#include <math.h>
#define internal static
#define local_persist static
#define global_variable static 
#define ArrayCount(Array) (sizeof(Array)/sizeof((Array)[0]))
//be careful with macros, put extra parenthesis if some inputs will mess with functionality
//such as passing foo+bar next to [0] for bar[0]

/*
HANDMADE_INTERNAL:
0 - build for public
1 - build for dev

HANDMADE_SLOW:
0 - build for fast
1 - build for slow
*/


#if HANDMADE_SLOW
#define Assert(Expression) if(!(Expression)){*(int*)0 = 0;}
#else
#define Assert(Expression)
#endif


//should these all be 64bit?
#define Kilobytes(Value) (Value * 1024LL)
#define Megabytes(Value) (Value * 1024LL * 1024)
#define Gigabytes(Value) (Value * 1024LL * 1024 * 1024)
#define Terabytes(Value) (Value * 1024LL * 1024 * 1024 * 1024)



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

inline uint32 SafeTruncateUInt64(uint64 Value){
	//todo defines for max values UInt32Max
	Assert(Value <= 0xFFFFFFFF);
	uint32 Result = (uint32)Value;
	return(Result);
}
//services that the game provides to the platform layer below

// needs 4 things - user inputs, bitmap buffer to use, sound buffer to use, timing

//services that the platform layer provides to the game below



//NOT for shipping! Blocking and write doesnt protect against lost data!
struct debug_read_file_result
{   
    uint32 ContentsSize;
    void *Contents;
};

#if HANDMADE_INTERNAL
//NOT for shipping! Blocking and write doesnt protect against lost data!
internal debug_read_file_result DEBUGPlatformReadEntireFile(char *Filename);
internal void DEBUGPlatformFreeFileMemory(void *Memory);
internal bool32 DEBUGPlatformWriteEntireFile(char *Filename, uint32 MemorySize, void *Memory);
#endif




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

struct game_button_state{
    int HalfTransitionCount;
    bool32 EndedDown;
};

struct game_controller_input{

    bool32 IsConnected;
    bool32 Analog;
    real32 StickAverageX;
    real32 StickAverageY;
    
    union
    {
        game_button_state Buttons[12];
        struct
         {
            game_button_state MoveUp;
            game_button_state MoveDown;
            game_button_state MoveLeft;
            game_button_state MoveRight;

            game_button_state ActionUp;
            game_button_state ActionDown;
            game_button_state ActionLeft;
            game_button_state ActionRight;

            game_button_state LeftShoulder;
            game_button_state RightShoulder;

            game_button_state Start;
            game_button_state Back;

            //weird button for assertions to check if Buttons[] is equal to count of game_button_states, for workaround due to anonymous struct
            game_button_state Error;


            
        };
    };
};
struct game_input{
    //insert clock values here.
    game_controller_input Controllers[5];
};
inline game_controller_input *GetController(game_input *Input, int unsigned ControllerIndex){
    Assert(ControllerIndex < ArrayCount(Input->Controllers));
    game_controller_input *Result = &Input->Controllers[ControllerIndex];
    return(Result);
}

struct game_state{
    int XOffset;
    int YOffset;
    int ToneHz;
};

struct game_memory{
    bool32 IsInitialized;
    uint64 PermanentStorageSize;
    void *PermanentStorage; //REQUIRED to be cleared to 0
    uint64 TransientStorageSize;
    void *TransientStorage;  //REQUIRED to be cleared to 0
};

struct game_clocks{
    real32 SecondsElapsed; //todo
};

#define HANDMADE_H
#endif