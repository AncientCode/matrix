//
//	matrix.c
//
//	Matrix-window implementation
//
//	Copyright (C) 2006 J Brown
//	www.catch22.net
//
//	Freeware
//
#include <windows.h>
#include <windowsx.h>
#include "globals.h"
#include "message.h"
#include "matrix.h"
#include "resource.h"

void DoMatrixMessage(HDC hdc, MATRIX *matrix);

HBITMAP MakeBitmap(HINSTANCE hInst, UINT uId, double hue);
COLORREF HSLtoRGB( double H, double S, double L );
COLORREF RGBtoHSL( COLORREF col, double *H, double *S, double *L );

MATRIX *GetMatrix(HWND hwnd)
{
	return (MATRIX *)GetWindowLong(hwnd, 0);
}

void SetMatrix(HWND hwnd, MATRIX *matrix)
{
	SetWindowLong(hwnd, 0, (LONG)matrix);
}

// pseudo-random number generator, based on 16bit CRC algorithm
static WORD _crc_reg = 0;
int crc_rand()
{
	const  WORD mask = 0xb400;

	if (_crc_reg & 1)
		_crc_reg = (_crc_reg >> 1) ^ mask;
	else
		_crc_reg = (_crc_reg >> 1);

	return _crc_reg;
}

int GlyphIntensity(GLYPH glyph)
{
	return (int)((glyph & 0x7f00) >> 8);
}

GLYPH DarkenGlyph(GLYPH glyph)
{
	int intensity = GlyphIntensity(glyph);

	if (intensity > 0)
		return GLYPH_REDRAW | ((intensity - 1) << 8) | (glyph & 0x00FF);
	else
		return glyph;
}

GLYPH RandomGlyph(int intensity)
{
	return GLYPH_REDRAW | (intensity << 8) | (crc_rand() % NUM_GLYPHS);
}

void RedrawBlip(GLYPH *glypharr, int blippos)
{
	glypharr[blippos+0] |= GLYPH_REDRAW;
	glypharr[blippos+1] |= GLYPH_REDRAW;
	glypharr[blippos+8] |= GLYPH_REDRAW;
	glypharr[blippos+9] |= GLYPH_REDRAW;
}

void ScrollMatrixColumn(MATRIX_COLUMN *col)
{
	int y;
	GLYPH lastglyph = 0;
	GLYPH thisglyph;

	// wait until we are allowed to scroll
	if (col->started == FALSE) {
		if (--col->countdown <= 0)
			col->started = TRUE;

		return;
	}

	// "seed" the glyph-run
	lastglyph = col->state ? (GLYPH)0 : (GLYPH)(MAX_INTENSITY << 8);

	//
	// loop over the entire length of the column, looking for changes
	// in intensity/darkness. This change signifies the start/end
	// of a run of glyphs.
	//
	for (y = 0; y < col->length; y++) {
		thisglyph = col->glyph[y];

		// bottom-most part of "run". Insert a new character (glyph)
		// at the end to lengthen the run down the screen..gives the
		// impression that the run is "falling" down the screen
		if (GlyphIntensity(thisglyph) < GlyphIntensity(lastglyph) &&
		        GlyphIntensity(thisglyph) == 0) {
			col->glyph[y] = RandomGlyph(MAX_INTENSITY - 1);
			y++;
		}
		// top-most part of "run". Delete a character off the top by
		// darkening the glyph until it eventually disappears (turns black).
		// this gives the effect that the run as dropped downwards
		else if (GlyphIntensity(thisglyph) > GlyphIntensity(lastglyph)) {
			col->glyph[y] = DarkenGlyph(thisglyph);

			// if we've just darkened the last bit, skip on so
			// the whole run doesn't go dark
			if (GlyphIntensity(thisglyph) == MAX_INTENSITY - 1)
				y++;
		}

		lastglyph = col->glyph[y];
	}

	// change state from blanks <-> runs when the current run as expired
	if (--col->runlen <= 0) {
		int density = g_nDensity;
		density = DENSITY_MAX - density + DENSITY_MIN;
		if (col->state ^= 1)
			col->runlen = crc_rand() % (3 * density + DENSITY_MIN);
		else
			col->runlen = crc_rand() % (DENSITY_MAX-density+1) + (DENSITY_MIN*2);
	}

	//
	// make a "blip" run down this column at double-speed
	//

	// mark current blip as redraw so it gets "erased"
	if (col->blippos >= 0 && col->blippos < col->length)
		RedrawBlip(col->glyph, col->blippos);

	// advance down screen at double-speed
	col->blippos += 2;

	// if the blip gets to the end of a run, start it again (for a random
	// length so that the blips never get synched together)
	if (col->blippos >= col->bliplen) {
		col->bliplen = col->length + crc_rand() % 50;
		col->blippos = 0;
	}

	// now redraw blip at new position
	if (col->blippos >= 0 && col->blippos < col->length)
		RedrawBlip(col->glyph, col->blippos);

}

//
// randomly change a small collection glyphs in a column
//
void RandomMatrixColumn(MATRIX_COLUMN *col)
{
	int i, y;

	for (i = 1, y = 0; i < 16; i++) {
		// find a run
		while (GlyphIntensity(col->glyph[y]) < MAX_INTENSITY-1 && y < col->length)
			y++;

		if (y >= col->length)
			break;

		col->glyph[y]  = (col->glyph[y] & 0xff00) | (crc_rand() % NUM_GLYPHS);
		col->glyph[y] |= GLYPH_REDRAW;

		y += crc_rand() % 10;
	}
}

void DrawGlyph(MATRIX *matrix, HDC hdc, int xpos, int ypos, GLYPH glyph)
{
	int intensity = GlyphIntensity(glyph);
	int glyphidx  = glyph & 0xff;

	BitBlt(hdc, xpos, ypos, GLYPH_WIDTH, GLYPH_HEIGHT, matrix->hdcBitmap,
	       glyphidx * GLYPH_WIDTH, intensity * GLYPH_HEIGHT, SRCCOPY);
}

void RedrawMatrixColumn(MATRIX_COLUMN *col, MATRIX *matrix, HDC hdc, int xpos)
{
	int y;

	// loop down the length of the column redrawing only what needs doing
	for (y = 0; y < col->length; y++) {
		GLYPH glyph = col->glyph[y];

		// does this glyph (character) need to be redrawn?
		if (glyph & GLYPH_REDRAW) {
			if ((y == col->blippos+0 || y == col->blippos+1 ||
			        y == col->blippos+8 || y == col->blippos+9) &&
			        GlyphIntensity(glyph) >= MAX_INTENSITY-1)
				glyph |= MAX_INTENSITY << 8;

			DrawGlyph(matrix, hdc, xpos, y * GLYPH_HEIGHT, glyph);

			// clear redraw state
			col->glyph[y] &= ~GLYPH_REDRAW;
		}
	}
}

void SetMatrixBitmap(MATRIX *matrix, int hue)
{
	HBITMAP hBmp;

	hue %= 255;

	// create the new bitmap
	hBmp = MakeBitmap(GetModuleHandle(0), IDB_BITMAP1, hue / 255.0);
	DeleteObject(SelectObject(matrix->hdcBitmap, hBmp));

	matrix->hbmBitmap = hBmp;
	SelectObject(matrix->hdcBitmap, matrix->hbmBitmap);
}

void DecodeMatrix(HWND hwnd, MATRIX *matrix)
{
	int x;
	HDC hdc = GetDC(hwnd);

	for (x = 0; x < matrix->numcols; x++) {
		RandomMatrixColumn(&matrix->column[x]);
		ScrollMatrixColumn(&matrix->column[x]);
		RedrawMatrixColumn(&matrix->column[x], matrix, hdc, x * GLYPH_WIDTH);
	}

	if (matrix->message)
		DoMatrixMessage(hdc, matrix);

	//SetMatrixBitmap(matrix, g_nHue++);

	ReleaseDC(hwnd, hdc);
}

void RefreshMatrix(HWND hwnd)
{
	int x, y;
	MATRIX *matrix = GetMatrix(hwnd);
	for (x = 0; x < matrix->numcols; x++) {
		for (y = 0; y < matrix->column[x].length; y++)
			matrix->column[x].glyph[y] |= GLYPH_REDRAW;
	}

	SendMessage(hwnd, WM_TIMER, 0, 0);
}

HBITMAP MakeBitmap(HINSTANCE hInst, UINT uId, double hue)
{
	HBITMAP hDIB;
	HANDLE  hOldBmp;
	HBITMAP hBmp;
	HDC		hdc;

	DIBSECTION dib;
	BITMAPINFOHEADER *bih;
	DWORD *dest;
	BYTE  *src;
	int i;

	RGBQUAD pal[256];

	// load the 8bit image
	hBmp = LoadImage(hInst, MAKEINTRESOURCE(IDB_BITMAP1), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);

	// extract the colour table
	hdc = CreateCompatibleDC(NULL);
	hOldBmp = SelectObject(hdc, hBmp);
	GetDIBColorTable(hdc, 0, 256, pal);
	SelectObject(hdc, hOldBmp);

	//
	GetObject(hBmp, sizeof(dib), &dib);
	src = dib.dsBm.bmBits;
	bih = &dib.dsBmih;

	// change to a 32bit bitmap
	dib.dsBm.bmBitsPixel		= 32;
	dib.dsBm.bmPlanes			= 1;
	dib.dsBm.bmWidthBytes		= dib.dsBm.bmWidth * 4;
	dib.dsBm.bmType				= 0;
	dib.dsBmih.biBitCount		= 32;
	dib.dsBmih.biPlanes			= 1;
	dib.dsBmih.biCompression	= 0;
	dib.dsBmih.biSizeImage		= dib.dsBmih.biWidth * dib.dsBmih.biHeight * 4;
	dib.dsBmih.biClrUsed		= 0;
	dib.dsBmih.biClrImportant	= 0;


	// create a new (blank) 32bit DIB section
	hDIB = CreateDIBSection(hdc, (BITMAPINFO *)&dib.dsBmih, DIB_RGB_COLORS, &dest, 0, 0);

	// copy each pixel
	for (i = 0; i < bih->biWidth * bih->biHeight; i++) {
		// convert 8bit palette entry to 32bit colour
		RGBQUAD  rgb = pal[*src++];
		COLORREF col = RGB(rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue);

		// convert the RGB colour to H,S,L values
		double h,s,l;
		RGBtoHSL(col, &h, &s, &l);

		// create the new colour
		*dest++ = HSLtoRGB(hue, s, l);
	}

	DeleteObject(hBmp);
	DeleteDC(hdc);
	return hDIB;
}

//
//	Allocate matrix structures
//
MATRIX *CreateMatrix(HWND hwnd, int width, int height)
{
	MATRIX *matrix;
	HDC hdc;
	int x, y;

	int rows = height / GLYPH_HEIGHT + 1;
	int cols = width  / GLYPH_WIDTH  + 1;

	// allocate matrix!
	if ((matrix = malloc(sizeof(MATRIX) + sizeof(MATRIX_COLUMN) * cols)) == 0)
		return 0;

	matrix->numcols = cols;
	matrix->numrows = rows;
	matrix->width   = width;
	matrix->height  = height;

	for (x = 0; x < cols; x++) {
		matrix->column[x].length       = rows;
		matrix->column[x].started      = FALSE;
		matrix->column[x].countdown    = crc_rand() % 100;
		matrix->column[x].state        = crc_rand() % 2;
		matrix->column[x].runlen       = crc_rand() % 20 + 3;

		matrix->column[x].glyph  = malloc(sizeof(GLYPH) * (rows+16));

		for (y = 0; y < rows; y++)
			matrix->column[x].glyph[y] = 0;//;
	}

	// Load bitmap!!
	hdc = GetDC(NULL);
	matrix->hbmBitmap = MakeBitmap(GetModuleHandle(0), IDB_BITMAP3, g_nHue / 255.0);
	matrix->hdcBitmap = CreateCompatibleDC(hdc);
	SelectObject(matrix->hdcBitmap, matrix->hbmBitmap);
	ReleaseDC(NULL, hdc);

	// Create a message for this window...only if we are
	// screen-saving (not if in preview mode)
	if (GetParent(hwnd) == 0)
		matrix->message = InitMatrixMessage(hwnd, matrix->numcols, matrix->numrows);
	else
		matrix->message = 0;

	return matrix;
}

//
//	Free up matrix structures
//
void DestroyMatrix(MATRIX *matrix)
{
	int x;

	// free the matrix columns
	for (x = 0; x < matrix->numcols; x++)
		free(matrix->column[x].glyph);

	DeleteDC(matrix->hdcBitmap);
	DeleteObject(matrix->hbmBitmap);

	// now delete the matrix!
	free(matrix);
}

//
//	Window procedure for one matrix (1 per screen)
//
LRESULT WINAPI MatrixWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static POINT ptLast;
	static POINT ptCursor;
	static BOOL  fFirstTime = TRUE;

	MATRIX *matrix = GetMatrix(hwnd);

	switch (msg) {
		// window creation
	case WM_NCCREATE:

		// create the matrix based on how big this window is
		matrix = CreateMatrix(hwnd, ((CREATESTRUCT *)lParam)->cx, ((CREATESTRUCT *)lParam)->cy);

		// failed to allocate? stop window creation!
		if (matrix == 0)
			return FALSE;

		SetMatrix(hwnd, matrix);

		// start off an animation timer
		SetTimer(hwnd, 0xdeadbeef, ((SPEED_MAX - g_nMatrixSpeed) + SPEED_MIN) * 10, 0);
		if (!GetParent(hwnd))
			RegisterHotKey(hwnd, 0xbeefdead, MOD_WIN | MOD_SHIFT | MOD_CONTROL, 'Q');
		return TRUE;

		// window being destroyed, cleanup
	case WM_NCDESTROY:
		DestroyMatrix(matrix);
		PostQuitMessage(0);
		return 0;

		// animation timer has gone off, redraw the matrix!
	case WM_TIMER:
		DecodeMatrix(hwnd, matrix);
		return 0;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		return 0;
	case WM_LBUTTONUP:
		if (GetParent(hwnd)) {
			static HANDLE hProcess = 0;
			DWORD code;
			TCHAR name[MAX_PATH];
			STARTUPINFO si = { sizeof(STARTUPINFO) };
			PROCESS_INFORMATION pi = {0};

			GetExitCodeProcess(hProcess, &code);
			if (hProcess && code == STILL_ACTIVE)
				return 0;
			GetModuleFileName(NULL, name, MAX_PATH);
			CreateProcess(name, "matrix -s", NULL, NULL, FALSE, 0,
			              NULL, NULL, &si, &pi);
			hProcess = pi.hProcess;
			CloseHandle(pi.hThread);
		} else return 0;
	case WM_HOTKEY:
		if (wParam == 0xbeefdead)
			PostMessage(hwnd, WM_CLOSE, 0, 0);
		return 0;
		// someone wants to close us...see if it's ok
	case WM_CLOSE:
		if (VerifyPassword(hwnd)) {
			KillTimer(hwnd, 0xdeadbeef);
			DestroyWindow(hwnd);
		}

		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND CreateScreenSaveWnd(HWND hwndParent, RECT *rect)
{
	DWORD dwStyle = hwndParent ? WS_CHILD : WS_POPUP;

#ifdef _DEBUG
	DWORD dwStyleEx = 0;
#else
	DWORD dwStyleEx = WS_EX_TOPMOST;
#endif

	if (hwndParent)
		GetClientRect(hwndParent, rect);

	return CreateWindowEx(dwStyleEx, APPNAME, 0, WS_VISIBLE | dwStyle,
	                      rect->left, rect->top, rect->right - rect->left,
	                      rect->bottom - rect->top, hwndParent, 0,
	                      GetModuleHandle(0), 0);
}

//
//	Initialize class for matrix window
//
void InitScreenSaveClass(BOOL fPreview)
{
	WNDCLASSEX	wcx;

	wcx.cbSize			= sizeof(WNDCLASSEX);
	wcx.style			= 0;
	wcx.lpfnWndProc		= MatrixWndProc;
	wcx.cbClsExtra		= 0;
	wcx.cbWndExtra		= sizeof(MATRIX *);
	wcx.hInstance		= GetModuleHandle(0);
	wcx.hIcon			= 0;
	wcx.hbrBackground	= (HBRUSH)GetStockObject(BLACK_BRUSH);
	wcx.lpszMenuName	= 0;
	wcx.lpszClassName	= APPNAME;
	wcx.hIconSm			= 0;

	if (fPreview)
		wcx.hCursor = LoadCursor(0, IDC_ARROW);
	else
		wcx.hCursor = LoadCursor(wcx.hInstance, MAKEINTRESOURCE(IDC_BLANKCURSOR));

	// initialize the crc register used for "random" number generation
	_crc_reg = (WORD)GetTickCount();

	RegisterClassEx(&wcx);
}
