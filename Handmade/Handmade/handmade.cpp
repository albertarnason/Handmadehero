internal void GameUpdateAndRender(game_offscreen_buffer *Buffer, game_sound_output_buffer *SoundBuffer);

internal void GameOutputSound(game_sound_output_buffer *SoundBuffer, int ToneHz){

	local_persist real32 tSine;	
	int16 ToneVolume = 3000;
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
			uint8 Blue = (uint8)(X + BlueOffset);
			uint8 Green= (uint8)(Y + GreenOffset);
			
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

internal void GameUpdateAndRender (game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer,  game_sound_output_buffer *SoundBuffer){
	
	//pointer arithmetic to make sure game_button_sate Buttons[] == game_button_state
	Assert((&Input->Controllers[0].Error - &Input->Controllers[0].Buttons[0]) == (ArrayCount(Input->Controllers[0].Buttons)));
	//game breaks right here in debugger if false
	Assert(sizeof(game_state) <= Memory->PermanentStorageSize); 
	
	game_state *GameState= (game_state *)Memory->PermanentStorage;
	if(!Memory->IsInitialized){

	
		
		//for debug purposes always reading whole files at once until multithreading
		//roundtrip system for training wheels version
		char *Filename = __FILE__;
/*
		uint64 FileSize = GetFileSize(FileName);
		void *BitmapMemory = ReserveMemory(Memory, FileSize);
		ReadEntireFileIntoMemory(FileName, BitmapMemory);
*/

		debug_read_file_result File = DEBUGPlatformReadEntireFile(Filename);
		if(File.Contents)
		{
			//works
			DEBUGPlatformWriteEntireFile("C:/Users/walla/src/Handmadehero/Handmade/Handmade/Debug/test.out", File.ContentsSize, File.Contents);
			DEBUGPlatformFreeFileMemory(File.Contents);
		}

		



		GameState->ToneHz = 256;
		Memory->IsInitialized = true;
	
	};

	//For loop for multiple controller inputs hmm
	for(int ControllerIndex = 0; ControllerIndex <ArrayCount(Input->Controllers); ++ControllerIndex){
		game_controller_input *Controller = GetController(Input, ControllerIndex);
		if(Controller->Analog){
			GameState->YOffset += 	    (int)(	4.0f * Controller->StickAverageX);
			GameState->ToneHz 	= 256 + (int)(128.0f * Controller->StickAverageY);
		} 
		else {
			//Keyboard movement
			if(Controller->ActionLeft .EndedDown){GameState->XOffset -= 1;}
			if(Controller->ActionRight.EndedDown){GameState->XOffset += 1;}
			if(Controller->ActionUp   .EndedDown){GameState->YOffset -= 1;}
			if(Controller->ActionDown .EndedDown){GameState->YOffset += 1;}
			
			if(Controller->MoveLeft   .EndedDown){GameState->XOffset -= 1;}
			if(Controller->MoveRight  .EndedDown){GameState->XOffset += 1;}
			if(Controller->MoveUp     .EndedDown){GameState->YOffset -= 1;}
			if(Controller->MoveDown   .EndedDown){GameState->YOffset += 1;}
		}

	}
	//Todo allow sample offsets here for more robust platform options
	GameOutputSound(SoundBuffer, GameState->ToneHz);
    RenderWeirdGradient(Buffer, GameState->XOffset, GameState->YOffset);





}


	/*
	Old file system style, "any read CAN fail, thus handling fails explicitly"
	File handles very stateful, every thread would need their own file handle
	Also very sequential code that depends on the slowest part of the machine, disk io, thus stalling entire game
	Especially multiple reads in a row
	"Nothing good about streaming based file io (for "our" purposes)"

	char *Filename = "test.bmp";
	file_handle *File = OpenFile(Filename);
	uint8 Buffer[128];
	if(Read(File, sizeof(Buffer), Buffer))
		{use
		uint8 Buffer2[128];
			if(Read(File, sizeof(Buffer2), Buffer2))
				{use} 
			else 
				{error}} 
	else 
		{error}
	CloseFile(File);*/
