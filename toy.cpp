#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <stdint.h>
#include <assert.h>

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

#define ArrayCount(Array) (sizeof((Array))/sizeof((Array)[0]))

typedef u32 buffer_position;

struct buffer
{
	char* Name;
	u8*   Data;
	buffer_position GapStartPosition;
	buffer_position GapEndPosition;
	buffer_position EndPosition;
};

ID2D1Factory* D2dFactory;
IDWriteFactory* DwriteFactory;
ID2D1HwndRenderTarget* RenderTarget;
IDWriteTextFormat* TextFormat;
buffer* CurrentBuffer;
buffer_position CurrentPos;
f32 FontSize;

ID2D1SolidColorBrush* TextBrush;
WCHAR Text[] = L"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Proin vehicula lectus ut egestas tincidunt. Maecenas placerat ligula vel nunc accumsan pulvinar. Curabitur at quam risus. Duis convallis lorem nisi, et pretium nisl imperdiet ut. Nam nec lacus vitae turpis pharetra gravida eu sed nulla. Cras at leo volutpat, gravida purus vitae, tempor ante. Integer nec massa ut lacus mollis viverra. Phasellus id dui egestas, vulputate sapien eget, commodo lorem. Morbi ac suscipit leo. Ut imperdiet lectus sed pulvinar sagittis. Donec et semper ante. Donec laoreet tellus quis turpis tempor mattis. Donec pellentesque elit ac arcu scelerisque, sit amet posuere erat vehicula. Vestibulum volutpat consectetur mauris, quis faucibus mi gravida eget.\n"
"Mauris ac augue placerat, semper nisi et, aliquet velit. Suspendisse potenti. Etiam lorem enim, dapibus et augue eget, facilisis laoreet arcu. Interdum et malesuada fames ac ante ipsum primis in faucibus. Proin luctus fringilla mi, in lobortis ipsum pharetra sit amet. Curabitur eget ex at ante accumsan bibendum. Nam id rutrum magna.\n"
"Donec non porta neque. Nunc turpis nibh, gravida cursus malesuada at, eleifend ac metus. Vivamus laoreet, lorem et placerat tempor, libero ante condimentum arcu, a elementum metus erat in leo. Integer tincidunt posuere bibendum. Quisque gravida risus arcu, accumsan blandit nulla interdum eget. Nunc eu consequat lacus. Duis pharetra vel mi in suscipit. Pellentesque sed diam sed lectus varius ullamcorper. Donec sed dictum augue.\n"
"Curabitur non bibendum arcu. Suspendisse mauris leo, lobortis et placerat vitae, interdum vitae diam. Pellentesque porttitor et ligula vestibulum malesuada. Sed sagittis felis sed eros viverra, sollicitudin viverra sem cursus. Cras sit amet dignissim ipsum, a mattis nisi. Aliquam est augue, consequat sit amet varius quis, congue nec est. Maecenas at porta ligula, at vulputate metus. Nam ex augue, faucibus sit amet tempus mollis, posuere sed elit. Vestibulum posuere sed sem vitae imperdiet. Mauris a ipsum urna. Donec varius libero nibh. Vivamus interdum molestie mattis. Aenean lobortis, turpis a finibus consectetur, erat turpis tristique elit, sed aliquet eros risus id nunc. Cras eu posuere libero.\n"
"Aliquam erat volutpat. Aliquam egestas nulla sed nisi fermentum semper. Nam non sapien cursus, finibus felis vitae, efficitur metus. Integer sit amet dolor congue, mattis augue et, malesuada enim. Ut ultrices nisi enim, ut malesuada risus molestie quis. Nunc sit amet erat ut lectus tincidunt luctus ut at augue. Nullam auctor, magna vitae finibus ornare, metus sapien sagittis ante, eget sodales erat erat eget nisl. Aliquam vel dapibus elit. Quisque malesuada gravida leo. Duis finibus eros accumsan mi maximus vehicula. Quisque odio augue, facilisis at sagittis eget, efficitur eget metus. Duis sollicitudin interdum congue. Sed eu efficitur tellus. Vivamus finibus, libero sit amet sodales eleifend, tortor orci porta nisl, in condimentum nunc enim nec urna.\n";


void InitializeBuffer(buffer* Buffer, u32 InitialGapSize)
{
	Buffer->Data = (u8*)HeapAlloc(GetProcessHeap(), 0, InitialGapSize);
	Buffer->GapStartPosition = 0;
	Buffer->GapEndPosition = InitialGapSize;
	Buffer->EndPosition = InitialGapSize;
}

buffer_position MoveBufferPositionForward(buffer* Buffer, buffer_position Pos)
{
	assert(Pos != Buffer->EndPosition);
	Pos++;
	if (Pos == Buffer->GapStartPosition)
	{
		Pos = Buffer->GapEndPosition;
	}
	return Pos;
}

void ShiftGapToPosition(buffer* Buffer, buffer_position Pos)
{
	u32 GapSize = Buffer->GapEndPosition - Buffer->GapStartPosition;
	if (Pos < Buffer->GapStartPosition)
	{
		u32 GapDelta = Buffer->GapStartPosition - Pos;
		Buffer->GapStartPosition -= GapDelta;
		Buffer->GapEndPosition -= GapDelta;
		MoveMemory(Buffer->Data + Buffer->GapEndPosition, Buffer->Data + Buffer->GapStartPosition, GapDelta);
	}
	else if (Pos > Buffer->GapStartPosition)
	{
		u32 GapDelta = Pos - Buffer->GapStartPosition;
		MoveMemory(Buffer->Data + Buffer->GapStartPosition, Buffer->Data + Buffer->GapEndPosition, GapDelta);
		Buffer->GapStartPosition += GapDelta;
		Buffer->GapEndPosition += GapDelta;
	}
}

void EnsureGapSize(buffer* Buffer, u32 MinimumGapSize)
{
	u32 GapSize = Buffer->GapEndPosition - Buffer->GapStartPosition;
	if (GapSize < MinimumGapSize)
	{
		ShiftGapToPosition(Buffer, Buffer->EndPosition - GapSize);
		u32 NewEndPosition = 2 * Buffer->EndPosition;
		Buffer->Data = (u8*)HeapReAlloc(GetProcessHeap(), 0, Buffer->Data, NewEndPosition);
		Buffer->GapEndPosition = NewEndPosition;
		Buffer->EndPosition = NewEndPosition;
	}
}

void InsertCharacter(buffer* Buffer, buffer_position Pos, char c)
{
	EnsureGapSize(Buffer, 1);
	ShiftGapToPosition(Buffer, Pos);
	Buffer->Data[Buffer->GapStartPosition] = c;
	Buffer->GapStartPosition++;
}

buffer_position CopyLineFromBuffer(char* Dest, int DestSize, buffer* Buffer, buffer_position BufferPos)
{
	if (BufferPos >= Buffer->GapStartPosition && BufferPos < Buffer->GapEndPosition)
	{
		BufferPos = Buffer->GapEndPosition;
	}
	for(int i=0; i<DestSize && BufferPos < Buffer->EndPosition; i++)
	{
		char c = Buffer->Data[BufferPos];
		if (c == '\n')
		{
			break;
		}

		*Dest++ = c;
		BufferPos = MoveBufferPositionForward(Buffer, BufferPos);
	}
	return BufferPos;
}

void DebugWriteBuffer(buffer* Buffer)
{
	char temp[1024];
	CopyMemory(temp, Buffer->Data, Buffer->GapStartPosition);
	temp[Buffer->GapStartPosition] = 0;
	OutputDebugStringA(temp);
	CopyMemory(temp, Buffer->Data + Buffer->GapEndPosition, Buffer->EndPosition - Buffer->GapEndPosition);
	temp[Buffer->EndPosition - Buffer->GapEndPosition] = 0;
	OutputDebugStringA(temp);
}

void DrawBuffer(buffer* Buffer, f32 LineHeight, f32 x, f32 y, f32 width, f32 height)
{
	char Utf8Line[1024];
	WCHAR Utf16Line[1024];

	D2D1_RECT_F LayoutRect;
	LayoutRect.left = x;
	LayoutRect.right = x + width;
	LayoutRect.top = y;
	LayoutRect.bottom = y + LineHeight;

	buffer_position BufferPos = 0;
	for(;;)
	{
		ZeroMemory(Utf8Line, sizeof(Utf8Line));
		BufferPos = CopyLineFromBuffer(Utf8Line, sizeof(Utf8Line), Buffer, BufferPos);
		MultiByteToWideChar(CP_UTF8, 0, Utf8Line, sizeof(Utf8Line), Utf16Line, ArrayCount(Utf16Line));
		
		RenderTarget->DrawText(Utf16Line, wcslen(Utf16Line), TextFormat, LayoutRect, TextBrush);

		LayoutRect.top += LineHeight;
		LayoutRect.bottom += LineHeight;

		if (BufferPos == Buffer->EndPosition)
		{
			break;
		}
		BufferPos = MoveBufferPositionForward(Buffer, BufferPos);
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
			if (RenderTarget)
			{
				RenderTarget->Release();
			}
			RECT ClientRect;
			GetClientRect(Window, &ClientRect);
			D2D1_SIZE_U WindowSize;
			WindowSize.width = ClientRect.right - ClientRect.left;
			WindowSize.height = ClientRect.bottom - ClientRect.top;
			HResult = D2dFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), 
				D2D1::HwndRenderTargetProperties(Window, WindowSize), &RenderTarget);
			if(TextBrush)
			{
				TextBrush->Release();
			}
			HResult = RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &TextBrush);

		} break;
		case WM_CHAR: {
			char Character;
			WideCharToMultiByte(CP_UTF8, 0, (WCHAR*)&wParam, 1, &Character, 1, 0, 0);
			InsertCharacter(CurrentBuffer, CurrentPos++, Character);
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
	CurrentPos = 0;

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