#include "handmade.h"

internal void GameOutputSound(game_sound_output_buffer *SoundBuffer){

	local_persist real32 tSine;	
	int16 ToneVolume = 3000;
	int ToneHz = 256;
	int WavePeriod = SoundBuffer->SamplesPerSecond/ToneHz;
	int16 *SampleOut = SoundBuffer->Samples;

	for (int SampleIndex = 0; SampleIndex < SoundBuffer->SampleCount; ++SampleIndex)
		{
			real32 SineValue = sinf(tSine);
			int16 SampleValue = (int16)(SineValue * ToneVolume);
			*SampleOut++ = SampleValue;
			*SampleOut++ = SampleValue;
			
			tSine += 2.0f*Pi32*1.0f/(real32)WavePeriod;
		}

}

internal void RenderWeirdGradient(game_offscreen_buffer *Buffer, int BlueOffset, int GreenOffset)
{
	//todo lets see what the optimizer does
	// Casting to make sure the pointer arithmetic doesnt get multiplied by C 
	uint8 *Row = (uint8 *)Buffer->Memory;

	for(int Y = 0; Y < Buffer->Height;++Y)
	{
		uint32 *Pixel = (uint32 *)Row;
		for(int X = 0; X <Buffer->Width;++X)
		{
			/*
				
				LITTLE ENDIAN ARCHITECTURE--------------V
				Bytes           =  0  1  2  3			V
				Pixel in memory = RR GG BB xx, -> 0x xxBBGGRR
				Bunch of windows order swapping and whatnot
				So it ends up being this:
				Pixel in memory = BB GG RR xx
			*/
			//Blue
			uint8 Blue = (X + BlueOffset);
			uint8 Green= (Y + GreenOffset);
			
			// *Pixel = ;, writes value to left of = into Pixel by dereferencing with *
			// *Pixel++, the ++ is post increment operator, so after expression add 1
			//Also C is doing a 1*(sizeof uint32) aka = 4 so the expression adds 4
			//Shifting green value 8bits (2 bytes) left and OR'ing with blue
			*Pixel++ = ((Green << 8) | Blue);
			//memory = BB GG RR xx
			//Register = xx RR GG BB 
		}
		Row += Buffer->Pitch;
	}
}

internal void GameUpdateAndRender (game_offscreen_buffer *Buffer, int XOffset, int YOffset, game_sound_output_buffer *SoundBuffer){
	//Todo allow sample offsets here for more robust platform options
	GameOutputSound(SoundBuffer);
    RenderWeirdGradient(Buffer, XOffset, YOffset);
}