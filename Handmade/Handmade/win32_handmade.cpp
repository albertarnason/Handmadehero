/*TODO this is not a final platform layer!!

	- saved game locations
	- handle to own executable file
	- asset loading path
	- threading (launch a thread)
	- raw input (support multiple keyboards)
	- sleep/timebeginperiod
	- clipCursor() (multimonitor support)
	- fullscreen support
	- WM_SETCURSOR (control cursor visibility)
	- querycancelautoplay (old stuff)
	- WM_ACTIVATEAPP (when we are not the active application)
	- Blit speed improvements (BitBit)
	- Hardware acceleration (OpenGL or Direct 3D or both)
	- GetKeyBoardLayout (french keyboards, international WASD support)

	partial list of stuff
*/

//Todo Swap, Min, Max macros

#include "handmade.h"

#include "handmade.cpp"

//Casey style c++, mostly C
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <malloc.h>
#include <Xinput.h>
#include <dsound.h>

//translates left value into right value


struct win32_offscreen_buffer
{
BITMAPINFO Info;
void *Memory;
int Width;
int Height;
int Pitch;
int BytesPerPixel;
};

struct win32_window_dimension 
{
	int Width;
	int Height;
};

struct win32_sound_output
{
			int SamplesPerSecond;
			uint32 RunningSampleIndex;
			int BytesPerSample;
			int SecondaryBufferSize;
			int LatencySampleCount;
};


global_variable bool GlobalRunning;
global_variable win32_offscreen_buffer GlobalBackBuffer;
global_variable LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;

//VirtualProtect great for free Catching use-after-free, Buffer overruns, accidental writes to read-only memory

//Cant return 2 values with a C function, so u bundle them into structs
//Dont want to bundle types if possible, only when the values HAVE to go together


//Calling windows function directly something something, getting around cases where users dont have gamepad specific software installed/available
//So the program can run without those and thus making Gamepad/xbox360controller support for users OPTIONAL instead of REQUIRED
//x_input_get_State *Foo is legal, can declare a pointer to these functions
//typedef DWORD WINAPI x_input_get_state(DWORD dwUserIndex, XINPUT_STATE* pState );
//typedef DWORD WINAPI x_input_set_state(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration );
//MACRO defines a function of this form, to make stubs
//XInputGetState

#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE* pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub)
{
	return(ERROR_DEVICE_NOT_CONNECTED);
}
global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub; //static global variable value is 0
#define XInputGetState XInputGetState_

//XInputSetState
#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub)
{
	return(ERROR_DEVICE_NOT_CONNECTED);
}
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub; //static global variable value is 0
#define XInputSetState XInputSetState_

#define DIRECT_SOUND_CREATE(name)HRESULT WINAPI name(LPCGUID pcGuidDevice,LPDIRECTSOUND *ppDS,LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

//only for debugging!
//NOT for shipping! Blocking and write doesnt protect against lost data!
internal debug_read_file_result DEBUGPlatformReadEntireFile(char *Filename){
debug_read_file_result Result = {};
HANDLE FileHandle = CreateFileA(Filename,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
if(FileHandle != INVALID_HANDLE_VALUE){LARGE_INTEGER FileSize;
	if(GetFileSizeEx(FileHandle, &FileSize)){uint32 FileSize32 = SafeTruncateUInt64(FileSize.QuadPart);Result.Contents = VirtualAlloc(0,FileSize32, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
		if(Result.Contents){DWORD BytesRead;
			if(ReadFile(FileHandle, Result.Contents, FileSize32, &BytesRead, 0) && (FileSize32 == BytesRead))
				{/*succes*/ Result.ContentsSize = FileSize32;}
			else{DEBUGPlatformFreeFileMemory(Result.Contents);Result.Contents = 0;}}
		else{}}
	else{}CloseHandle(FileHandle);}
else{}
return(Result);
}

internal bool32 DEBUGPlatformWriteEntireFile(char *Filename, uint32 MemorySize, void *Memory){
bool32 Result = false;
HANDLE FileHandle = CreateFileA(Filename,GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
if(FileHandle != INVALID_HANDLE_VALUE){DWORD BytesWritten;
	if(WriteFile(FileHandle, Memory,MemorySize, &BytesWritten, 0))
		{/*succes*/ Result =(BytesWritten == MemorySize);}
	else{}CloseHandle(FileHandle);}
else{}
return(Result);
}

internal void DEBUGPlatformFreeFileMemory(void *Memory){
if(Memory){
	VirtualFree(Memory, 0, MEM_RELEASE);}
}

internal void Win32LoadXInpuT()
{
	HMODULE XInputLibrary = LoadLibraryA("xinput1_4.dll");
	if(!XInputLibrary)
	{
		// todo logging
	//removed for compiler warning C4456
	//HMODULE XInputLibrary = LoadLibraryA("xinput1_3.dll");
	}
	if(XInputLibrary)
	{
		//no idea why this is duplicated
		XInputGetState = (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
		if(!XInputGetState){XInputGetState = XInputGetStateStub;}
		XInputSetState = (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
		if(!XInputSetState){XInputSetState = XInputSetStateStub;}
			else
		{
			//todo logging
		}
	}
		else
		{
			//todo logging
		}
}

internal void Win32InitDSound(HWND Window, int32 SamplesPerSecond, int32 BufferSize)
{
	//Load library
	//Get DirectSound object - cooperative mode (Component Object Model aaaaaa)
	//Create primary buffer
	//Create Secondary buffer (write to this)
	// 
	//start it playing

	HMODULE DSoundLibrary = LoadLibraryA("dsound.dll");
	if(DSoundLibrary)
	{
		direct_sound_create *DirectSoundCreate = (direct_sound_create *)GetProcAddress(DSoundLibrary, "DirectSoundCreate");
		
		// directsound 8 or 7
		LPDIRECTSOUND DirectSound;
		if(DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0)))
		{	
			//int16 [LEFT RIGHT] 4 bytes LEFT RIGHT SAMPLES IN BUFFER
			WAVEFORMATEX WaveFormat = {};
			WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
			WaveFormat.nChannels = 2;
			WaveFormat.nSamplesPerSec = SamplesPerSecond;
			WaveFormat.wBitsPerSample = 16;
			WaveFormat.nBlockAlign = (WaveFormat.nChannels*WaveFormat.wBitsPerSample) /8;
			WaveFormat.nAvgBytesPerSec = WaveFormat.nSamplesPerSec*WaveFormat.nBlockAlign;
			WaveFormat.cbSize;

			if(SUCCEEDED(DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY)))
			{
				DSBUFFERDESC BufferDescription ={};
				BufferDescription.dwSize =sizeof(BufferDescription);
				BufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

				//DSBCAPS_GLOBALFOCUS?
				LPDIRECTSOUNDBUFFER PrimaryBuffer;
				if(SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDescription, &PrimaryBuffer, 0)))
				{
					HRESULT Error = PrimaryBuffer->SetFormat(&WaveFormat);
					if(SUCCEEDED(Error))
					{
						//primary buffer set
						OutputDebugStringA("Primary buffer format was set\n");
					}
					else
					{
						//todo logging
					}
				}
				else
				{
					//todo logging
				}
			}
			else
			{
			//todo logging
			}
			//DSBCAPS_GETCURRENTPOSITION2
			DSBUFFERDESC BufferDescription ={};
			BufferDescription.dwSize =sizeof(BufferDescription);
			BufferDescription.dwFlags = 0;
			BufferDescription.dwBufferBytes = BufferSize;
			BufferDescription.lpwfxFormat = &WaveFormat;
			//DSBCAPS_GLOBALFOCUS?

			HRESULT Error = DirectSound->CreateSoundBuffer(&BufferDescription, &GlobalSecondaryBuffer, 0);
			if(SUCCEEDED(Error))
			{
				//Secondary buffer
				OutputDebugStringA("Secondary buffer format was set\n");
			}
			else
			{
					//todo logging
			}		
		}
		else
		{
			//todo logging
		}
	}
	else
	{
		//to do logging
	}



}



internal win32_window_dimension Win32GetWindowDimension(HWND Window)
{
	win32_window_dimension Result;

	RECT ClientRect;
	GetClientRect(Window,&ClientRect);
	Result.Width = ClientRect.right - ClientRect.left;
	Result.Height = ClientRect.bottom - ClientRect.top;

	return(Result);
};


internal void Win32ReSizeDIBSection(win32_offscreen_buffer *Buffer, int Width, int Height)
{
	if(Buffer->Memory)
	{
		VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
	}

	Buffer->Width = Width;
	Buffer->Height = Height;
	Buffer->BytesPerPixel = 4;

	Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
	Buffer->Info.bmiHeader.biWidth = Buffer->Width;
	Buffer->Info.bmiHeader.biHeight = -Buffer->Height; //negative height makes the rows go from topleft -> down, if it was positive it would be bottomleft -> up
	Buffer->Info.bmiHeader.biPlanes = 1;
	Buffer->Info.bmiHeader.biBitCount = 32;
	Buffer->Info.bmiHeader.biCompression = BI_RGB;
	//Buffer->Info.bmiHeader.biSizeImage = 0; //since it's declared static these are already set to 0, so obsolete
	//Buffer->Info.bmiHeader.biXPelsPerMeter = 0;
	//Buffer->Info.bmiHeader.biYPelsPerMeter = 0;
	//Buffer->Info.bmiHeader.biClrUsed = 0;
	//Buffer->Info.bmiHeader.biClrImportant = 0;

	int BitmapMemorySize =(Buffer->Width*Buffer->Height)*Buffer->BytesPerPixel;

	//Malloc() -> HeapAlloc() -> VirtualAlloc() So just call VirtualAlloc directly
	Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
	Buffer->Pitch = Width*Buffer->BytesPerPixel;

	//Probably want to clear to black on ?startup
}

//dirty rectangle update
//Small function, classic case for the compiler to inline
internal void Win32DisplayBufferInWindow(win32_offscreen_buffer *Buffer,HDC DeviceContext, int WindowWidth, int WindowHeight)
{
	//TODO aspect ratio correction
	StretchDIBits(
	DeviceContext,
	/*
	X,Y,Width,Height,
	X,Y,Width,Height,
	*/
	0,0, WindowWidth, WindowHeight,
	0,0, Buffer->Width, Buffer->Height,
	Buffer->Memory,
	&Buffer->Info,
	DIB_RGB_COLORS, SRCCOPY);
}



LRESULT CALLBACK Win32MainWindowCallback(
  HWND Window,
  UINT Message,
  WPARAM WParam,
  LPARAM LParam)
  {

	LRESULT Result = 0;

	switch(Message)
	{
		case WM_SIZE:
		{
			OutputDebugStringA("WM_SIZE\n");
		} break;
		case WM_DESTROY:
		{
			//handle this as an error - recreate window? 
			GlobalRunning = false;
			OutputDebugStringA("WM_DESTROY\n");
		} break;
		

		case WM_CLOSE:
		{
			//handle this with a message to the user?
			GlobalRunning = false;
			OutputDebugStringA("WM_CLOSE\n");
		} break;
		case WM_ACTIVATEAPP:
		{
			OutputDebugStringA("WM_ACTIVATEAPP\n");
		} break;
		case WM_PAINT:
		{
			PAINTSTRUCT Paint;
			HDC DeviceContext = BeginPaint(Window, &Paint);
			int X = Paint.rcPaint.left;
			int Y = Paint.rcPaint.top;
			int Width = Paint.rcPaint.right - Paint.rcPaint.left;
			int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;
			
			win32_window_dimension Dimension = Win32GetWindowDimension(Window);
			Win32DisplayBufferInWindow(&GlobalBackBuffer, DeviceContext, Dimension.Width, Dimension.Height);
			//beginpaint endpaint is important for windows so it knows when to start and stop painting, it keeps its own record
			//and if u dont have it itll flood message queue
			EndPaint(Window, &Paint);
		} break;
		default:
		{
//			OutputDebugStringA("default\n")
			Result = DefWindowProc(Window, Message, WParam, LParam); 
		} break;
	}

	return(Result);
}



internal void Win32ClearBuffer(win32_sound_output *SoundOutput){
	VOID *Region1;
	DWORD Region1Size;
	VOID *Region2;
	DWORD Region2Size;
	if(SUCCEEDED(GlobalSecondaryBuffer->Lock(0,
		SoundOutput->SecondaryBufferSize,
		&Region1, &Region1Size,
		&Region2, &Region2Size,
		0)))
		{
			uint8 *DestSample = (uint8 *)Region1;
			for (DWORD ByteIndex = 0; ByteIndex < Region1Size; ++ByteIndex)
			{
				*DestSample++ = 0;
			}
			DestSample = (uint8 *)Region2;
			for (DWORD ByteIndex = 0; ByteIndex < Region2Size; ++ByteIndex)
			{
				*DestSample++ = 0;
			}
			GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
		}
}

internal void Win32FillSoundBuffer(win32_sound_output *SoundOutput, DWORD ByteToLock, DWORD BytesToWrite, game_sound_output_buffer *SourceBuffer)
{

	// More testing
	VOID *Region1;
	DWORD Region1Size;
	VOID *Region2;
	DWORD Region2Size;

	if(SUCCEEDED(GlobalSecondaryBuffer->Lock(
		ByteToLock,
		BytesToWrite,
		&Region1, &Region1Size,
		&Region2, &Region2Size,
		0)))
	{
		// Assert that Region1Size/Region2Size is valid
		
		//Collapse these two loops
		DWORD Region1SampleCount = Region1Size/SoundOutput->BytesPerSample;
		int16 *DestSample = (int16 *)Region1;
		int16 *SourceSample = SourceBuffer->Samples;
		for (DWORD SampleIndex = 0; SampleIndex < Region1SampleCount; ++SampleIndex)
		{
			*DestSample++ = *SourceSample++;
			*DestSample++ = *SourceSample++;
			++SoundOutput->RunningSampleIndex;
		}

		DWORD Region2SampleCount = Region2Size/SoundOutput->BytesPerSample;
		DestSample = (int16 *)Region2;
		for(DWORD SampleIndex = 0; SampleIndex < Region2SampleCount; ++SampleIndex)
		{
			*DestSample++ = *SourceSample++;
			*DestSample++ = *SourceSample++;
			++SoundOutput->RunningSampleIndex;
		}

		GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
	}

}

//preprocessor expressions using #, such as #if (expression) #else
//if HANDMADE_WIN32
//oldschool way of cross platform compatability was filling the file completely up with #IF #ELSE statements
// horrible for code readability and worst of all control flow must now be equal on all platforms, which is horrible
// not all platforms work equally, so can't make control flow dependent on eachother
//20 years of casey cross platform experience trustmebro
//One instance where it is useable:


//win32 files are prefixed with win32_
//header file api interface, which all the platform NON specific code calls into
//The code STARTS in platform specific code and then it calls INTO platform NON specific code
//then if the platform NON specific code needs platform specific code it calls BACK into platformspecific code that then returns to platform NON specific code

//Two possible ways to do this platform specific header API stuff

//1. virtualise operation system
//
//2. Game provides building blocks for operating system
//
//
//

internal void Win32ProcessXInputDigitalButton(DWORD XInputButtonState, game_button_state *OldState, game_button_state *NewState, DWORD ButtonBit){
	NewState->EndedDown = ((XInputButtonState & ButtonBit) == ButtonBit);
	NewState->HalfTransitionCount = (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;

}

internal void Win32ProcessKeyboardMessage(game_button_state *NewState, bool32 IsDown){
	NewState->EndedDown = IsDown;
	++NewState->HalfTransitionCount;

}



int CALLBACK WinMain(
	HINSTANCE Instance,
	HINSTANCE PrevInstance,
	LPSTR     CommandLine,
	int       ShowCode)
{

	
	LARGE_INTEGER PerfCountFrequencyResult;
	QueryPerformanceFrequency(&PerfCountFrequencyResult);
	int64 PerfCountFrequency = PerfCountFrequencyResult.QuadPart;



	Win32LoadXInpuT();

	WNDCLASSA WindowClass = {};
 
	//win32_window_dimension Dimension = Win32GetWindowDimension(Window);
	Win32ReSizeDIBSection(&GlobalBackBuffer, 1280, 720);

	WindowClass.style = CS_HREDRAW|CS_VREDRAW;
	WindowClass.lpfnWndProc = Win32MainWindowCallback;
	WindowClass.hInstance = Instance;
 	// WindowClass.hIcon;
	WindowClass.lpszClassName = "HandmadeHeroWindowClass";
	
  	//idk if its registerclassA or W
  	if(RegisterClassA(&WindowClass))
  	{
		HWND Window = CreateWindowExA(
			0, //dwExStyle
			WindowClass.lpszClassName, //lpClassName
			"HandmadeHero", //lpWindowName
			WS_OVERLAPPEDWINDOW|WS_VISIBLE, //dwStyle
			CW_USEDEFAULT, //X 
			CW_USEDEFAULT, //Y 
			CW_USEDEFAULT, //X width
			CW_USEDEFAULT, //Y height
			0,
			0,
			Instance,
			0);
		if(Window != NULL)
		{
			HDC DeviceContext = GetDC(Window);


			win32_sound_output SoundOutput = {};

			//Make this like 60 seconds (so playcursor cant wrap on us)
			SoundOutput.SamplesPerSecond = 48000;
			SoundOutput.BytesPerSample = sizeof(int16)*2;
			SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond*SoundOutput.BytesPerSample;
			SoundOutput.LatencySampleCount = SoundOutput.SamplesPerSecond / 15;

			Win32InitDSound(Window, SoundOutput.SamplesPerSecond , SoundOutput.SecondaryBufferSize);
			Win32ClearBuffer(&SoundOutput);
			GlobalSecondaryBuffer->Play(0,0, DSBPLAY_LOOPING);
			int16 *Samples =(int16 *)VirtualAlloc(0, SoundOutput.SecondaryBufferSize,  MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

#if HANDMADE_INTERNAL 
LPVOID BaseAdress = (LPVOID)Terabytes((uint64)2); //error is simply wrong
#else
LPVOID BaseAdress = 0;
#endif

			game_memory GameMemory = {};
			GameMemory.PermanentStorageSize = Megabytes(64);
			GameMemory.TransientStorageSize = Gigabytes((uint64)1);

			uint64 TotalSize = GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;

			GameMemory.PermanentStorage = VirtualAlloc(BaseAdress, TotalSize,  MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
			GameMemory.TransientStorage = ((uint8 *)GameMemory.PermanentStorage + GameMemory.PermanentStorageSize);
			//virtualalloc initialises to 0!!!

			if(Samples && GameMemory.PermanentStorage && GameMemory.TransientStorage)
			{
				game_input Input [2] = {};
				game_input *NewInput = &Input[0];
				game_input *OldInput = &Input[1];

				LARGE_INTEGER LastCounter;
				QueryPerformanceCounter(&LastCounter);
				uint64 LastCycleCount = __rdtsc();

				//pool with bitmap virtualalloc
				GlobalRunning = true;
				while(GlobalRunning)
				{
					//Large integer is a windows union struct consisting of lowpart highpart, u.lowpart u.highpart, and quadpart, 32bit 32bit u32bit u32bit and 64bit integer values
					//struct(lowpart,highpart) is an anonymous struct/member can access by BeginCounter.u.LowPart
					
					//todo make zeroing macro
					//todo we cant zero everything because the up/down state will be wrong!!!!
					game_controller_input *KeyboardController = &NewInput->Controllers[0];
					game_controller_input ZeroController = {};
					*KeyboardController = ZeroController;
					MSG Message;
					//has to process the message queue from windows
					while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
					{
						if(Message.message == WM_QUIT)
						{
							GlobalRunning = false;
						}

						switch (Message.message)
						{
							case WM_SYSKEYDOWN:
							case WM_SYSKEYUP:
							case WM_KEYDOWN:
							case WM_KEYUP:
							{

								// VKCode tells which key it is
								// VKCode == 'W' = w key
								uint32 VKCode  = (uint32)Message.wParam;
								bool WasDown   = ((Message.lParam & (1 << 30)) != 0);
								bool IsDown    = ((Message.lParam & (1 << 31)) == 0);

								if (WasDown != IsDown)
								{
									if      (VKCode == 'W')        { OutputDebugStringA("W\n");     }
									else if (VKCode == 'A')        { OutputDebugStringA("A\n");     }
									else if (VKCode == 'S')        { OutputDebugStringA("S\n");     }
									else if (VKCode == 'D')        { OutputDebugStringA("D\n");     }
									else if (VKCode == 'Q')        { Win32ProcessKeyboardMessage(&KeyboardController->LeftShoulder,  IsDown);}
									else if (VKCode == 'E')        { Win32ProcessKeyboardMessage(&KeyboardController->RightShoulder, IsDown);}
									else if (VKCode == VK_UP)      { Win32ProcessKeyboardMessage(&KeyboardController->Up, 			 IsDown);}
									else if (VKCode == VK_DOWN)    { Win32ProcessKeyboardMessage(&KeyboardController->Down, 		 IsDown);}
									else if (VKCode == VK_LEFT)    { Win32ProcessKeyboardMessage(&KeyboardController->Left, 	     IsDown);}
									else if (VKCode == VK_RIGHT)   { Win32ProcessKeyboardMessage(&KeyboardController->Right, 	     IsDown);}
									else if (VKCode == VK_SPACE)   { OutputDebugStringA("space\n"); }
									else if (VKCode == VK_ESCAPE)  { GlobalRunning = false;			}
								}
								bool32 AltKeyWasDown = (Message.lParam & (1 << 29));
								if ((VKCode == VK_F4) && AltKeyWasDown)
								{
									GlobalRunning = false;
								}
							} break;
							default:
							{
							TranslateMessage(&Message);
							DispatchMessageA(&Message);
							}
							}
					}

					//Input devices 3 ways: Interrupt, Polling, network based
					//Polling: The code requests the state (when it feels like it)
					//Interrupt based schemed: Device sends YOU(thecode) when state changes, cpu interrupts, legacy
					//networked based
					//should we poll this more frequency
					DWORD MaxControllerCount = XUSER_MAX_COUNT;
					if(MaxControllerCount > ArrayCount(NewInput->Controllers)){MaxControllerCount = ArrayCount(NewInput->Controllers);}
					for(DWORD ControllerIndex = 0;ControllerIndex <XUSER_MAX_COUNT; ++ControllerIndex)
					{
						game_controller_input *OldController = &OldInput->Controllers[ControllerIndex];
						game_controller_input *NewController = &NewInput->Controllers[ControllerIndex];
						XINPUT_STATE ControllerState;
						if(XInputGetState(ControllerIndex,&ControllerState) == ERROR_SUCCESS)
						{
							//Todo we will handle deadzone later
							//XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
							//XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE

							//CONTROLLER IS PLUGGED IN
							// See if ControllerState.dwPacketNumber increments too rapidly
							XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;

							bool Up = (Pad ->wButtons & XINPUT_GAMEPAD_DPAD_UP);
							bool Down = (Pad ->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
							bool Left = (Pad ->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
							bool Right = (Pad ->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
							
							real32 X; if (Pad->sThumbLX < 0){X = (real32)Pad->sThumbLX / 32768.0f;} else {X = (real32)Pad->sThumbLX / 32767.0f;}
							real32 Y; if (Pad->sThumbLY < 0){Y = (real32)Pad->sThumbLY / 32768.0f;} else {Y = (real32)Pad->sThumbLY / 32767.0f;}
							//todo min/max macros
							NewController->Analog = true;
							NewController->MinX = OldController->MaxX = NewController->EndX = X;
							NewController->MinY = OldController->MaxY = NewController->EndY = Y;
							

							NewController->StartX = OldController->EndX;
							NewController->StartY = OldController->EndY;
							
							Win32ProcessXInputDigitalButton(Pad ->wButtons, &OldController->Down, &NewController->Down, XINPUT_GAMEPAD_A);
							Win32ProcessXInputDigitalButton(Pad ->wButtons, &OldController->Right, &NewController->Right, XINPUT_GAMEPAD_B);
							Win32ProcessXInputDigitalButton(Pad ->wButtons, &OldController->Left, &NewController->Left, XINPUT_GAMEPAD_X);
							Win32ProcessXInputDigitalButton(Pad ->wButtons, &OldController->Up, &NewController->Up, XINPUT_GAMEPAD_Y);
							//Win32ProcessXInputDigitalButton(Pad ->wButtons, &OldController->State, &NewController->State, XINPUT_GAMEPAD_START);
							//Win32ProcessXInputDigitalButton(Pad ->wButtons, &OldController->State, &NewController->State, XINPUT_GAMEPAD_BACK);
							Win32ProcessXInputDigitalButton(Pad ->wButtons, &OldController->LeftShoulder, &NewController->LeftShoulder, XINPUT_GAMEPAD_LEFT_SHOULDER);
							Win32ProcessXInputDigitalButton(Pad ->wButtons, &OldController->RightShoulder, &NewController->RightShoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER);




							//if(AButton){Add xboxcontroller conditional input here}


						}
						else
						{
							//CONTROLLER IS NOT PLUGGED IN
						}
					}
					//Vibrates xbox controller
					//XINPUT_VIBRATION Vibration;
					//Vibration.wLeftMotorSpeed = 60000;
					//Vibration.wRightMotorSpeed = 60000;
					//XInputSetState(0, &Vibration);
					DWORD ByteToLock = 0;
					DWORD BytesToWrite = 0;
					DWORD TargetCursor = 0;
					DWORD PlayCursor = 0;
					DWORD WriteCursor = 0;
					bool32 SoundIsValid = false;
					//todo tighten up sound logic so that twe know where we should be writing to and can anticipate the time spent in the game update
					if(SUCCEEDED(GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor)))
					{ ByteToLock = (SoundOutput.RunningSampleIndex*SoundOutput.BytesPerSample) % SoundOutput.SecondaryBufferSize;
						TargetCursor = ((PlayCursor + SoundOutput.LatencySampleCount*SoundOutput.BytesPerSample) % SoundOutput.SecondaryBufferSize);
						
						// change this to using a lower latency offset from the playcursor
						// when we actually start having sound effects
						if(ByteToLock > TargetCursor)
						{	
							BytesToWrite = (SoundOutput.SecondaryBufferSize - ByteToLock);
							BytesToWrite += TargetCursor;
						}
						else
						{
							BytesToWrite = TargetCursor - ByteToLock;
						}
						SoundIsValid = true;
					}

					game_sound_output_buffer SoundBuffer = {};
					SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
					SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
					SoundBuffer.Samples = Samples;

					

					game_offscreen_buffer Buffer = {};
					Buffer.Memory = GlobalBackBuffer.Memory;
					Buffer.Width = GlobalBackBuffer.Width;
					Buffer.Height = GlobalBackBuffer.Height;
					Buffer.Pitch = GlobalBackBuffer.Pitch;
					GameUpdateAndRender(&GameMemory, NewInput, &Buffer, &SoundBuffer);
					
					//Buggy
					//DirectSound output test
					if(SoundIsValid){
						
						Win32FillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite, &SoundBuffer);
					}

					win32_window_dimension Dimension = Win32GetWindowDimension(Window);
					Win32DisplayBufferInWindow(&GlobalBackBuffer,DeviceContext, Dimension.Width, Dimension.Height);
					//deleted in casey code
					//ReleaseDC(Window, DeviceContext);
					//++XOffset;
					//++YOffset;

					//Performance 
					uint64 EndCycleCount = __rdtsc();

					LARGE_INTEGER EndCounter;
					QueryPerformanceCounter(&EndCounter);

					uint64 CyclesElapsed = EndCycleCount - LastCycleCount;
					int64 CounterElapsed = EndCounter.QuadPart - LastCounter.QuadPart;
					real64 MSPerFrame = (((1000.0f*(real64)CounterElapsed) / (real64)PerfCountFrequency));
					real64 FPS = (real64)PerfCountFrequency / (real64)CounterElapsed;
					real64 MCPF = (real64)(CyclesElapsed / (1000.0f *1000.0f));
					//percountfreq/counterelapsed is better than 1000/MSPerFrame as it gives more accurate values than MSPerFrame since MSPerFrame rounds/truncates decimal values
					
					//todo average of FPS so far

					//problematic printout, doesnt know how big the buffer is so can overwrite memory
					//if you add more %d to the format string, it can start reading all kinds of stuff off memory which it isnt supposed to
					//not good for shippable code according to Casey, really must know possible outputs if using in shippable code
					//only fine for debug code
					//trying to convert values to 32 int values before printout

					//char Buffer[256];
					//milliseconds per frame, frames per second, (mega)cycles per frame
					//todo: make own printf
					//sprintf always takes 64bit floats
					//sprintf(Buffer, "%.02fmspf, %.02ffps, %.02fmcpf\n", MSPerFrame, FPS, MCPF);
					//OutputDebugStringA(Buffer);

					LastCounter = EndCounter;
					LastCycleCount = EndCycleCount;

					game_input *Temp = NewInput;
					NewInput = OldInput;
					OldInput = Temp;
					//todo should i clear these
				}
			}
			else
			{
				//todo logging
			}
		} 
		else{
			//todo logging
		}
  }
  else
  {
	//todo logging
  };
  return(0);
}




//Albert notes dump
//page up page down for quickly jumping up and down
//alt+arrowkeys to move a line up and down
//ctrl(rightside)+1,2 to jump between windows
//alt+z for lines automatically wrapping to next line, very handy to just write super long lines and then let alt+z wrapping cruth when double window
//ctrl+space for terminal swap
//Caseys header style is completely butchered for me at episode 13, might be include order
//might be intellisense stuff, might be casey build stuff, might be just dumb
//tldr every header related change atm seems more tedious and annoying than just having it declared toplayer, despite good reasons probably existing for using headers
//rename all variables to snake case things_name_stuff

/*
mat4 projection;
mat4 view;
mat4 model;

vec4 pos;

vec4 proj = projection * view * model * pos;

mat4 projection_from_view;
mat4 view_from_world;
mat4 world_from_model;

vec4 model_position;

Vec4 model_projection = projection_from_view * view_from_world * world_from_model * model_position;

graphics programming naming convetion to make code functionality clearer
"Domino naming convention"
transformation direction
typed coordinate spaces
important because the names show the correct ordering
For any code where coordinate spaces mix (graphics, robotics, physics, AR/VR), it's arguably the clearest naming system that exists.


Brutal asserting, always crash on assert fail (with log dump)
(dont assert on user input stuff)
Assert asserts invariance
assert thing HAS TO BE thing for program to continue

Dreams engine (currentstate = laststate * input)
checking Bit determinism by running two in parallel and checking they're equal
gives "on the fly testing of determinism every frame"

GAME ENGINE DETERMINISM!!

Combo of 1 brutal asserting and 2 determinism checking every frame makes (most) bugs consistent (deterministic) AND makes non-determinism itself a bug, so it's forcefully removed 

determinism is functional immutable stateless adjacent

code tradeoffs with this approach
- parallelism problematic (deterministic parallelism?)
- annoying and complicated code
- cultural resistance


For every byte read, you can do approximately 5 instructions

so load 8 bytes, can do 40 instructions before the next one

Not true numbers, but probably true in order of magnitude

rsync algorithm  a utility for transferring and synchronizing files between a computer and a storage drive and across networked computers by comparing the modification times and sizes of files
gzip rsyncable to do it with zip files that normally are not compatible with rsync

for syncing between two computers/nodes where u believe a high percentage of the content on each computer to already be equal
deduplication process

uniformly slow program

Reason for performance mindset shift

classic gamer mindset is faster = better make it as fast as possible

Alternatively developer mindset:

Make things as fast as possible, so u can spend a ton of performance solving hard problems with simple code

Seeing performance as 1 a budget and a constraint, 2 a resource to be utilised (except for battery life!)

avoid state management and creation of state as much as possible
be aware of state reachability, can this state become invalid state

Minimize state surface area. Every piece of state that can be derived from other state should be. Derived state is not state — it is a function
*/