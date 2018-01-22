#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

#define Assert(Cond) \
	if(!(Cond)) { \
		char __temp[256]; \
		sprintf(__temp, "%s(%d): Assertion Failed!\n", __FILE__, __LINE__); \
		OutputDebugStringA(__temp); \
		*(int*)0=0; \
	}

#define ArrayCount(Array) (sizeof((Array))/sizeof((Array)[0]))

#define MAX(x, y) ((x) >= (y) ? (x):(y))

void* Allocate(u32 Size)
{
	return HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, Size);
}

void* Reallocate(void* Ptr, u32 Size)
{
	return HeapReAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, Ptr, Size);
}

void Free(void* Ptr)
{
	HeapFree(GetProcessHeap(), 0, Ptr);
}


typedef u32 cursor;

struct buffer
{
	u8*   Data;
	cursor Point;
	u32 GapStart;
	u32 GapEnd;
	u32 End;
};

#define BufferGapSize(Buffer) (Buffer->GapEnd - Buffer->GapStart)

#define BufferLength(Buffer) (Buffer->End - BufferGapSize(Buffer))

#define BufferIndex(Buffer, Cursor) ((Cursor < Buffer->GapStart) ? Cursor : Cursor + BufferGapSize(Buffer))

#define BufferCharacter(Buffer, Cursor) (Buffer->Data[BufferIndex(Buffer, Cursor)])

#define CursorNext(Buffer, Cursor) ((Cursor < BufferLength(Buffer)) ? Cursor + 1 : Cursor)

#define CursorPrevious(Buffer, Cursor) ((Cursor > 0) ? Cursor - 1 : Cursor)

void InitializeBuffer(buffer* Buffer, u32 InitialGapSize)
{
	Buffer->Data = (u8*)Allocate(InitialGapSize);
	Buffer->Point = 0;
	Buffer->GapStart = 0;
	Buffer->GapEnd = InitialGapSize;
	Buffer->End = InitialGapSize;
}

static void AssertBufferInvariants(buffer* Buffer)
{
	Assert(Buffer->Data);
	Assert(Buffer->GapStart <= Buffer->GapEnd);
	Assert(Buffer->GapEnd <= Buffer->End);
}

static void AssertCursorInvariants(buffer* Buffer, cursor Cursor)
{
	Assert(Cursor <= BufferLength(Buffer));
}

static void ShiftGapToCursor(buffer* Buffer, cursor Cursor)
{
	u32 GapSize = BufferGapSize(Buffer);
	if (Cursor < Buffer->GapStart)
	{
		u32 GapDelta = Buffer->GapStart - Cursor;
		Buffer->GapStart -= GapDelta;
		Buffer->GapEnd -= GapDelta;
		MoveMemory(Buffer->Data + Buffer->GapEnd, Buffer->Data + Buffer->GapStart, GapDelta);
	}
	else if (Cursor > Buffer->GapStart)
	{
		u32 GapDelta = Cursor - Buffer->GapStart;
		MoveMemory(Buffer->Data + Buffer->GapStart, Buffer->Data + Buffer->GapEnd, GapDelta);
		Buffer->GapStart += GapDelta;
		Buffer->GapEnd += GapDelta;
	}
	Assert(BufferGapSize(Buffer) == GapSize);
	AssertBufferInvariants(Buffer);
}

static void EnsureGapSize(buffer* Buffer, u32 MinimumGapSize)
{
	if (BufferGapSize(Buffer) < MinimumGapSize)
	{
		ShiftGapToCursor(Buffer, BufferLength(Buffer));
		u32 NewEnd = MAX(2 * Buffer->End, Buffer->End + MinimumGapSize);
		Buffer->Data = (u8*)Reallocate(Buffer->Data, NewEnd);
		Buffer->GapEnd = NewEnd;
		Buffer->End = NewEnd;
	}
	Assert(BufferGapSize(Buffer) >= MinimumGapSize);
}

bool ReplaceCharacter(buffer* Buffer, cursor Cursor, char Character)
{
	AssertCursorInvariants(Buffer, Cursor);
	if (Cursor < BufferLength(Buffer))
	{
		BufferCharacter(Buffer, Cursor) = Character;
		return true;
	}
	else
	{
		return false;
	}
}

void InsertCharacter(buffer* Buffer, cursor Cursor, char Character)
{
	AssertCursorInvariants(Buffer, Cursor);
	EnsureGapSize(Buffer, 100);
	ShiftGapToCursor(Buffer, Cursor);
	Buffer->Data[Buffer->GapStart] = Character;
	Buffer->GapStart++;
}

bool DeleteBackwardCharacter(buffer* Buffer, cursor Cursor)
{
	AssertCursorInvariants(Buffer, Cursor);
	if (Cursor > 0)
	{
		ShiftGapToCursor(Buffer, Cursor);
		Buffer->GapStart--;
		return true;
	}
	else
	{
		return false;
	}
}

bool DeleteForwardCharacter(buffer* Buffer, cursor Cursor)
{
	AssertCursorInvariants(Buffer, Cursor);
	if (Cursor < BufferLength(Buffer))
	{
		ShiftGapToCursor(Buffer, Cursor);
		Buffer->GapEnd++;
		return true;
	}
	else
	{
		return false;
	}
}

buffer* CurrentBuffer;
cursor CurrentCursor;

// Drawing

ID2D1Factory* D2dFactory;
IDWriteFactory* DwriteFactory;
ID2D1HwndRenderTarget* RenderTarget;
IDWriteTextFormat* TextFormat;
ID2D1SolidColorBrush* TextBrush;
f32 FontSize;

u32 CopyLineFromBuffer(char* Line, int MaxLineLength, buffer* Buffer, cursor* OutCursor)
{
	cursor Cursor = *OutCursor;
	int i;
	for (i=0; i<MaxLineLength && Cursor < BufferLength(Buffer); i++)
	{
		char Character = BufferCharacter(Buffer, Cursor);
		if (Character == '\n')
		{
			break;
		}

		Line[i] = Character;
		Cursor++;
	}
	while (Cursor < BufferLength(Buffer) && BufferCharacter(Buffer, Cursor) != '\n')
	{
		Cursor++;
	}
	*OutCursor = Cursor;
	return i;
}

void DebugWriteBuffer(buffer* Buffer)
{
	char temp[1024];
	CopyMemory(temp, Buffer->Data, Buffer->GapStart);
	temp[Buffer->GapStart] = 0;
	OutputDebugStringA(temp);
	CopyMemory(temp, Buffer->Data + Buffer->GapEnd, Buffer->End - Buffer->GapEnd);
	temp[Buffer->End - Buffer->GapEnd] = 0;
	OutputDebugStringA(temp);
}

void DrawBuffer(buffer* Buffer, f32 LineHeight, f32 x, f32 y, f32 width, f32 height)
{
	char Utf8Line[64];
	WCHAR Utf16Line[64];

	D2D1_RECT_F LayoutRect;
	LayoutRect.left = x;
	LayoutRect.right = x + width;
	LayoutRect.top = y;
	LayoutRect.bottom = y + LineHeight;
	for(cursor Cursor=0; Cursor < BufferLength(Buffer); Cursor++)
	{
		u32 LineLength = CopyLineFromBuffer(Utf8Line, sizeof(Utf8Line) - 1, Buffer, &Cursor);
		Utf8Line[LineLength] = 0;
		MultiByteToWideChar(CP_UTF8, 0, Utf8Line, sizeof(Utf8Line), Utf16Line, ArrayCount(Utf16Line));
		
		RenderTarget->DrawText(Utf16Line, wcslen(Utf16Line), TextFormat, LayoutRect, TextBrush);
		LayoutRect.top += LineHeight;
		LayoutRect.bottom += LineHeight;
	}
}

void RenderWindow(HWND Window)
{
	RECT ClientRect;
	GetClientRect(Window, &ClientRect);
	RenderTarget->BeginDraw();
	RenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));
	DrawBuffer(CurrentBuffer, FontSize, ClientRect.left, ClientRect.top, ClientRect.right - ClientRect.left, ClientRect.bottom - ClientRect.top);
	RenderTarget->EndDraw();
}


LRESULT CALLBACK QedWindowProc(HWND Window, UINT MessageType, WPARAM wParam, LPARAM lParam)
{
	LRESULT Result = 0;
	HRESULT HResult = 0;

	switch(MessageType)
	{
		case WM_SIZE: {
			RECT ClientRect;
			GetClientRect(Window, &ClientRect);
			D2D1_SIZE_U WindowSize;
			WindowSize.width = ClientRect.right - ClientRect.left;
			WindowSize.height = ClientRect.bottom - ClientRect.top;			
			if (RenderTarget)
			{
				RenderTarget->Release();
			}
			HResult = D2dFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), 
				D2D1::HwndRenderTargetProperties(Window, WindowSize), &RenderTarget);
			if(TextBrush)
			{
				TextBrush->Release();
			}
			HResult = RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &TextBrush);

		} break;
		case WM_KEYDOWN: {
			switch(wParam)
			{
				case VK_DELETE: {
					DeleteForwardCharacter(CurrentBuffer, CurrentCursor);
				} break;
				case VK_BACK: {
					DeleteBackwardCharacter(CurrentBuffer, CurrentCursor);
					CurrentCursor = CursorPrevious(CurrentBuffer, CurrentCursor);
				} break;
				case VK_LEFT: {					
					CurrentCursor = CursorPrevious(CurrentBuffer, CurrentCursor);
				} break;
				case VK_RIGHT: {					
					CurrentCursor = CursorNext(CurrentBuffer, CurrentCursor);
				} break;
				case VK_RETURN: {
					InsertCharacter(CurrentBuffer, CurrentCursor, '\n');
					CurrentCursor = CursorNext(CurrentBuffer, CurrentCursor);
				} break;
			}
			RenderWindow(Window);
		} break;
		case WM_CHAR: {
			char Character;
			WideCharToMultiByte(CP_UTF8, 0, (WCHAR*)&wParam, 1, &Character, 1, 0, 0);
			if (' ' <= Character && Character <= '~')
			{				
				InsertCharacter(CurrentBuffer, CurrentCursor, Character);
				CurrentCursor = CursorNext(CurrentBuffer, CurrentCursor);
				RenderWindow(Window);				
			}
		} break;
		case WM_PAINT: {
			RenderWindow(Window);
		} break;
		case WM_DESTROY: {
			PostQuitMessage(0);
			exit(1);
		} break;
		default: {
			Result = DefWindowProcA(Window, MessageType, wParam, lParam);
		} break;

	}

	return Result;	
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
	FontSize = 36.0f;
	buffer BufferData;
	CurrentBuffer = &BufferData;
	InitializeBuffer(CurrentBuffer, 2);

	char* BufferText = "Hello\nWorld\nfoo bar bazzle";
	for(int i=0; i<strlen(BufferText); i++)
	{
		InsertCharacter(CurrentBuffer, i, BufferText[i]);
	}

	HRESULT HResult;
	HResult = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &D2dFactory);
	HResult = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&DwriteFactory);
	HResult = DwriteFactory->CreateTextFormat(L"Consolas", 0, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, FontSize ,L"en-us", &TextFormat);
	TextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

	WNDCLASS WindowClass = {0};
	WindowClass.hInstance = instance;
	WindowClass.lpfnWndProc = QedWindowProc;
	WindowClass.lpszClassName = "qed";
	WindowClass.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
	RegisterClassA(&WindowClass);

	HWND Window = CreateWindowA("qed", "QED", 
		WS_OVERLAPPEDWINDOW | WS_VISIBLE, 
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 
		0, 0, instance, 0);

	MSG Message;
	while(GetMessage(&Message, Window, 0, 0))
	{
		TranslateMessage(&Message);
		DispatchMessage(&Message);
	}

	return 0;
}