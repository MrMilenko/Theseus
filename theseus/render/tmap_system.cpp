// tmap_system.cpp: TMAP (dynamic texture) rendering pipeline.
//
//   CSurfx           8-bit palettized pixel surface with line drawing
//   DeltaField       2D vector-field pixel-displacement effects
//   CPalette         HSV palette generator with timed crossfades
//   CDynamicTexture  Renders children into a palettized texture each frame
//   CImageFader      Delta-field effect applied to a CSurfx
//   CAudioVisualizer PCM / FFT audio visualisation rendered to CSurfx
//   FFT              Radix-2 Cooley-Tukey
//
// Decompiled from the 5960 retail XBE; see docs/decomp/TmapSystem.md.
// All node types confirmed in 5960 retail XBE.

#include "std.h"
#include "theseus.h"
#include "node.h"
#include "asset_loader.h"
#include "audio.h"
#include "tmap_system.h"
#define FFT_BUFFER_SIZE_LOG 8
#define FFT_BUFFER_SIZE (1 << FFT_BUFFER_SIZE_LOG)
typedef short int sound_sample;
typedef struct _struct_fft_state fft_state;
fft_state* fft_init (void);
void fft_perform (const sound_sample* input, float* output, fft_state* state);
void fft_close (fft_state* state);


// ===== CSurfx ===============================================================
// 8-bit palettized pixel surface. Used as the render target for all TMAP
// effects. Supports Bresenham line drawing and delta field warping.

int CSurfx::sTempRef = 0;
long CSurfx::sTempSize = 0;
char* CSurfx::sTemp = NULL;

CSurfx::CSurfx(int nWidth, int nHeight)
{
	m_nWidth = nWidth;
	m_nHeight = nHeight;
	m_pels = new BYTE [nWidth * (nHeight + 1)];

	mLineWidth = 1;

	mClipRect.left = 0;
	mClipRect.top = 0;
	mClipRect.right = nWidth;
	mClipRect.bottom = nHeight;

	mBytesPerRow = nWidth;

	sTempRef += 1;
}

CSurfx::~CSurfx()
{
	delete [] m_pels;

	sTempRef -= 1;
	if (sTempRef == 0)
	{
		delete [] sTemp;
		sTemp = NULL;
		sTempSize = 0;
	}
}

void CSurfx::Fade(DeltaFieldData* inGrad)
{
	Fade((char*)m_pels, m_nWidth, m_nWidth, m_nHeight, inGrad);
}

#define HALFCORD	0x003F
#define FIXED_BITS	5

// Source-to-source copy variant (separate source and dest buffers)
void CSurfx::Fade(const char* inSrce, long inBytesPerSrceRow, char* inDest, long inBytesPerDestRow, long inX, long inY, const char* inGrad)
{
	unsigned long p, x, y, u, v, u1, v1, P1, P2, P3, P4, codedDel, xGrad, yGrad;
	const char* srceMap;

	inSrce = inSrce - HALFCORD * inBytesPerSrceRow - HALFCORD;

	for (y = 0; y < (unsigned)inY; y++)
	{
		for (x = 0; x < (unsigned)inX; x++)
		{
			codedDel = *((const unsigned long*) inGrad);
			inGrad += 3;
			xGrad = 0xFFF & (codedDel >> 12);
			yGrad = 0xFFF & (codedDel);

			srceMap = inSrce + (xGrad >> FIXED_BITS) + (yGrad >> FIXED_BITS) * inBytesPerSrceRow + x;

			u = (yGrad & 0x1F);
			v = (xGrad & 0x1F);

			P1  = ((unsigned char*) srceMap)[0];
			P2  = ((unsigned char*) srceMap)[1];

			u1  = 0x20 - u;
			P1 *= u1;
			P2 *= u1;

			P3  = ((unsigned char*) srceMap)[inBytesPerSrceRow];
			P4  = ((unsigned char*) srceMap)[inBytesPerSrceRow + 1];

			v1  = 0x20 - v;
			P3 *= u;
			P4 *= u;

			// Bilinear interpolation
			p = v * (P2 + P4) + v1 * (P1 + P3);

			((unsigned char*) inDest)[x] = (unsigned char)((31 * p) >> 15);
		}

		inDest += inBytesPerDestRow;
		inSrce += inBytesPerSrceRow;
	}
}

// In-place variant using a trailing row buffer to avoid overwriting source data
void CSurfx::Fade(char* inPix, long inBytesPerRow, long inX, long inY, DeltaFieldData* inGrad)
{
	unsigned long p, x, y, u, v, u1, v1, P1, P2, P3, P4, codedDel, xGrad, yGrad, curBufRowNum;
	long bufRows;
	char* srceMap, *curBufRow, *srce, *grad = inGrad->mField;

	bufRows = inGrad->mNegYExtents;
	if (sTempSize < bufRows * inBytesPerRow)
	{
		sTempSize = bufRows * inBytesPerRow;
		delete [] sTemp;
		sTemp = new char [sTempSize];
	}

	if (inY <= bufRows)
		bufRows = inY;
	Fade(inPix, inBytesPerRow, sTemp, inBytesPerRow, inX, bufRows, grad);
	inY -= bufRows;
	grad += 3 * inX * bufRows;
	curBufRowNum = 0;

	srce = inPix + bufRows * inBytesPerRow - HALFCORD * inBytesPerRow - HALFCORD;

	for (y = 0; y < (unsigned)inY; y += 1)
	{
		curBufRow = sTemp + curBufRowNum * inBytesPerRow;

		for (x = 0; x < (unsigned)inX; x += 1)
		{
			codedDel = *((const unsigned long*) grad);
			grad += 3;
			xGrad = 0xFFF & (codedDel >> 12);
			yGrad = 0xFFF & (codedDel);

			inPix[x] = curBufRow[x];

			srceMap = srce + (xGrad >> FIXED_BITS) + (yGrad >> FIXED_BITS) * inBytesPerRow + x;

			u = (yGrad & 0x1F);
			v = (xGrad & 0x1F);

			P1 = ((unsigned char*)srceMap)[0];
			P2 = ((unsigned char*)srceMap)[1];

			u1 = 0x20 - u;
			P1 *= u1;
			P2 *= u1;

			P3 = ((unsigned char*)srceMap)[inBytesPerRow];
			P4 = ((unsigned char*)srceMap)[inBytesPerRow + 1];

			v1 = 0x20 - v;
			P3 *= u;
			P4 *= u;

			p = v * (P2 + P4) + v1 * (P1 + P3);

			curBufRow[x] = (char)((31 * p) >> 15);
		}

		curBufRowNum = (curBufRowNum + 1) % bufRows;
		inPix += inBytesPerRow;
		srce += inBytesPerRow;
	}

	// Flush trailing buffer
	p = inX >> 2;
	for (y = 0; y < (unsigned)bufRows; y += 1)
	{
		curBufRow = sTemp + ((curBufRowNum + y) % bufRows) * inBytesPerRow;
		for (x = 0; x < p; x += 1)
		{
			((unsigned long*)inPix)[x] = ((unsigned long*)curBufRow)[x];
		}

		inPix += inBytesPerRow;
	}
}


// ===== HSV Palette ==========================================================
// 10 palette presets used by the TMAP visualizer system.

int g_nPalette = -1;

inline void SetRGB(int R, int G, int B, DWORD& rgb)
{
	if (R < 0) R = 0; else if (R > 255) R = 255;
	if (G < 0) G = 0; else if (G > 255) G = 255;
	if (B < 0) B = 0; else if (B > 255) B = 255;

	rgb = (R << 16) | (G << 8) | (B);
}

#define __SET_RGB(R, G, B) \
	SetRGB(R, G, B, outRGB); break;

void HSV2RGB(float H, float S, float V, DWORD& outRGB)
{
	long hexQuadrant, m, n, v;
	H = (H - floorf(H)) * 6.0f;

	hexQuadrant = (long)H;
	float f = H - hexQuadrant;

	if (S < 0.0f) S = 0.0f;
	if (S > 1.0f) S = 1.0f;
	if (V < 0.0f) V = 0.0f;
	if (V > 1.0f) V = 1.0f;

	if (!(hexQuadrant & 1))
		f = 1.0f - f;

	V *= 255.0f;
	v = (long)V;
	m = (long)(V * (1.0f - S));
	n = (long)(V * (1.0f - S * f));

	switch (hexQuadrant) {
	case 1: __SET_RGB(n, v, m);
	case 2: __SET_RGB(m, v, n);
	case 3: __SET_RGB(m, n, v);
	case 4: __SET_RGB(n, m, v);
	case 5: __SET_RGB(v, m, n);
	default: __SET_RGB(v, n, m);
	}
}

void GetHSV(int nPalette, float i, float t, float& H, float& S, float& V)
{
	g_nPalette = nPalette;

	switch (nPalette)
	{
	default:
		g_nPalette = 0;
		// FALL THROUGH

	case 0: // Firestorm
		H = 0.166f * powf(i, 1.9f);
		S = 1;
		V = powf(i, 0.9f);
		break;

	case 1: // Aqua
		H = 0.45f;
		S = powf((1.0f - i), 0.3f);
		V = powf(i, 0.7f);
		break;

	case 2: // Purple & Blues
		H = 0.666f + 0.166f * powf(i, 1.4f);
		S = 0.5f + 0.5f * powf((1.8f * i - 1.0f), 2);
		V = powf(i, 0.4f);
		break;

	case 3: // Color Wheel
		H = wrap(0.03f * t);
		S = 1.0f - 0.6f * powf(i, 2.5f);
		V = i;
		break;

	case 4: // Bizarro Mystery Unveiled
		H = wrap(0.3f - 0.003f * t);
		S = powf(i, 2.9f);
		V = powf(i, 0.9f);
		break;

	case 5: // Bizarro Color Wheel
		H = wrap(0.02f * t);
		S = 1.0f;
		V = 1.0f - powf(i, 1.4f);
		break;

	case 6: // Dark Rainbow
		H = powf(i, 1.6f);
		S = 1.0f;
		V = i;
		break;

	case 7: // Ice Nightshade
		H = wrap(powf((0.7f - 0.4f * i), 0.6f));
		S = 0.9f;
		V = powf(i, 0.9f);
		break;

	case 8: // Mystery Unveiled
		H = wrap(0.002f * t);
		S = powf((1.0f - i), 1.5f);
		V = powf(i, 0.4f);
		break;

	case 9: // Roundabout
		H = wrap(0.2f * (powf(i, 2.0f) + powf(cosf(4.0f * i), 2.0f)) + (0.05f * t));
		S = 0.85f + 0.5f * i;
		V = powf(i, 0.55f);
		break;
	}
}

int g_nBlendPalette;
float g_nBlendPaletteAmount;

#define timeToBlend (2.0f)

void MakePalette(DWORD outPalette [256])
{
	static XTIME nextPaletteChangeTime = 0.0f;
	static XTIME startBlendTime;

	int i;
	float H, S, V, inc = 1.0f / 255.0f;
	float mIntensity = 0.0f;
	XTIME now = TheseusGetNow();

	if (now > nextPaletteChangeTime)
	{
		nextPaletteChangeTime = now + 10.0f + rnd(5.0f);
		g_nBlendPalette = g_nPalette;
		g_nPalette += 1;
		g_nBlendPaletteAmount = 0.0f;
		startBlendTime = now;
	}

	for (i = 0; i < 256; i++, mIntensity += inc)
	{
		GetHSV(g_nPalette, mIntensity, (float) now, H, S, V);

		if (g_nBlendPalette != -1)
		{
			float H2, S2, V2;

			GetHSV(g_nBlendPalette, mIntensity, (float) now, H2, S2, V2);

			H = g_nBlendPaletteAmount * H + (1.0f - g_nBlendPaletteAmount) * H2;
			S = g_nBlendPaletteAmount * S + (1.0f - g_nBlendPaletteAmount) * S2;
			V = g_nBlendPaletteAmount * V + (1.0f - g_nBlendPaletteAmount) * V2;
		}

		HSV2RGB(H, S, V, outPalette[i]);
	}

	if (g_nBlendPalette != -1)
	{
		g_nBlendPaletteAmount += (float) (now - startBlendTime) / timeToBlend;
		if (g_nBlendPaletteAmount >= 1.0f)
			g_nBlendPalette = -1;
	}
}


// ===== Line Drawing =========================================================
// Bresenham line with variable-width pen and circle stamp for thick lines.

#define __doXerr	error_term += dy;			\
					if (error_term >= dx) {		\
						error_term -= dx;		\
						basePtr += rowOffset;	\
						ymov--;					\
					}

#define __doYerr	error_term += dx;			\
					if (error_term >= dy) {		\
						error_term -= dy;		\
						basePtr += xDirection;	\
						xmov--;					\
					}

#define __circ(dia, a)	switch ((dia)) {								\
						case 2:		a = "\0\0"; break;					\
						case 3:		a = "\1\0\1"; break;				\
						case 4:		a = "\1\0\0\1"; break;				\
						case 5:		a = "\1\0\0\0\1"; break;			\
						case 6:		a = "\1\0\0\0\0\1"; break;			\
						case 7:		a = "\2\1\0\0\0\1\2"; break;		\
						case 8:		a = "\2\1\0\0\0\0\1\2"; break;		\
						case 9:		a = "\3\1\1\0\0\0\1\1\3"; break;	\
						case 10:	a = "\3\1\1\0\0\0\0\1\1\3"; break;	\
						case 11:	a = "\4\2\1\1\0\0\0\1\1\2\4"; break;\
						case 12:	a = "\4\2\1\1\0\0\0\0\1\1\2\4"; break;\
					}

void CSurfx::Line(int sx, int sy, int ex, int ey, unsigned char color)
{
	long xDirection, rowOffset, error_term;
	char* basePtr, *center;
	long xmov, ymov, dx, dy, t, j, lw;
	long penExtents;

	sx = (((long)(sx & 0x80000000)) >> 1) | (sx & 0x3FFFFFFF);
	ex = (((long)(ex & 0x80000000)) >> 1) | (ex & 0x3FFFFFFF);
	sy = (((long)(sy & 0x80000000)) >> 1) | (sy & 0x3FFFFFFF);
	ey = (((long)(ey & 0x80000000)) >> 1) | (ey & 0x3FFFFFFF);

	lw = mLineWidth;
	if (mLineWidth > 3) {
		dx = ex - sx; dx = dx * dx;
		dy = ey - sy; dy = dy * dy;
		if (dx > 0 && dx >= dy)
			lw = 128 + 55 * dy / dx;
		else if (dy > 0 && dy > dx)
			lw = 128 + 55 * dx / dy;

		if (dx > 0 || dy > 0)
			lw = (mLineWidth * lw + 64) >> 7;
	}
	penExtents = lw >> 1;

	// Clipping
	if (sx < mClipRect.left + penExtents || sx >= mClipRect.right - penExtents || sy < mClipRect.top + penExtents || sy >= mClipRect.bottom - penExtents) {
		if (ex < mClipRect.left + penExtents || ex >= mClipRect.right - penExtents || ey < mClipRect.top + penExtents || ey >= mClipRect.bottom - penExtents)
			return;

		t = ex; ex = sx; sx = t;
		t = ey; ey = sy; sy = t;
	}

	dx = ex - sx;
	dy = ey - sy;

	dx = ex - sx;
	xmov = dx;
	if (dx < 0) {
		xmov = -dx;
		if (sx - xmov < mClipRect.left + penExtents)
			xmov = sx - (mClipRect.left + penExtents);
		xDirection = -1;
		dx = -dx;
	} else if (dx > 0) {
		if (sx + xmov >= mClipRect.right - penExtents)
			xmov = mClipRect.right - penExtents - 1 - sx;
		xDirection = 1;
	} else {
		xDirection = 0;
	}

	ymov = dy;
	if (dy < 0) {
		ymov = -dy;
		if (sy - ymov < mClipRect.top + penExtents)
			ymov = sy - (mClipRect.top + penExtents);
		rowOffset = -mBytesPerRow;
		dy = -dy;
	} else {
		if (sy + ymov >= mClipRect.bottom - penExtents)
			ymov = mClipRect.bottom - penExtents - sy - 1;
		rowOffset = mBytesPerRow;
	}

	basePtr = (char*)m_pels + sy * mBytesPerRow + sx * 1;
	error_term = 0;

	long halfW;

	if (lw > 1)
	{
		long c_x, tw = mLineWidth;
		halfW = (tw) >> 1;

		if (tw < 12)
		{
			char* c_shape;
			__circ(tw, c_shape)
			for (j = 0; j < tw; j++)
			{
				c_x = c_shape[j];
				center = basePtr + (j - halfW) * mBytesPerRow;
				int k;
				for (k = c_x; k < tw - c_x; k++)
				{
					((unsigned char*) center)[k - halfW] = color;
				}
			}
		}
		else
		{
			for (j = 0; j < tw; j++)
			{
				c_x = 0;
				center = basePtr + (j - halfW) * mBytesPerRow;
				for (int k = c_x; k < tw - c_x; k++)
				{
					((unsigned char*) center)[k - halfW] = color;
				}
			}
		}

		halfW = lw >> 1;

		if (dx > dy)
		{
			for (; xmov >= 0 && ymov >= 0; xmov--)
			{
				center = basePtr - halfW * mBytesPerRow;
				for (j = 0; j < lw; j++)
				{
					*((unsigned char*) center) = color;
					center += mBytesPerRow;
				}

				basePtr += xDirection;
				__doXerr
			}
		}
		else
		{
			for (; ymov >= 0 && xmov >= 0; ymov--)
			{
				center = basePtr - (halfW) * 1;
				for (j = 0; j < lw; j++)
				{
					*((unsigned char*) center) = color;
					center += 1;
				}
				basePtr += rowOffset;
				__doYerr
			}
		}
	}
	else
	{
		if (dx >= dy)
		{
			for (; xmov >= 0 && ymov >= 0; xmov--)
			{
				*((unsigned char*) basePtr) = color;
				basePtr += xDirection;
				__doXerr
			}
		}
		else
		{
			for (; ymov >= 0 && xmov >= 0; ymov--)
			{
				*((unsigned char*) basePtr) = color;
				basePtr += rowOffset;
				__doYerr
			}
		}
	}
}


// ===== DeltaField ===========================================================
// Computes a 2D displacement vector field for pixel warping effects.
// 24 preset warp styles (polar and cartesian).

#define DEC_SIZE 5

DeltaField::DeltaField()
{
	mWidth = mHeight = 0;
	mCurrentY = -1;
	m_nStyle = 0;
}

DeltaFieldData::DeltaFieldData()
{
	mNegYExtents = 0;
	mField = NULL;
}

DeltaFieldData::~DeltaFieldData()
{
	delete [] mField;
}

DeltaFieldData* DeltaField::GetField()
{
	if (mCurrentY >= 0)
	{
		while (!IsCalculated())
			CalcSome();

		return &mFieldData;
	}

	return NULL;
}

void DeltaField::Assign()
{
	mAspect1to1 = true;
	mPolar = true;
	mHasRTerm = true;
	mHasThetaTerm = true;

	SetSize(mWidth, mHeight, true);
}

void DeltaField::SetSize(long inWidth, long inHeight, bool inForceRegen)
{
	if (inWidth != mWidth || inHeight != mHeight || inForceRegen)
	{
		mWidth = inWidth;
		mHeight = inHeight;

		delete [] mFieldData.mField;
		mFieldData.mField = new char [3 * mWidth * mHeight + 64];

		mXScale = 2.0f / ((float)mWidth);
		mYScale = 2.0f / ((float)mHeight);

		if (mAspect1to1)
		{
			if (mYScale > mXScale)
				mXScale = mYScale;
			else
				mYScale = mXScale;
		}

		mCurrentY = 0;
		mNegYExtents = 0;

		// Win32 flips Y coordinates
		mYScale *= -1;
	}
}

#define __encode(x, y)	sx = x + 0x7E0;					\
						sy = y + 0x7E0;					\
						*((unsigned long*)g) = (sx << 12) | (sy);	\
						g += 3;

int DeltaField::CalcSome()
{
	float xscale2, yscale2, r, fx, fy;
	long px, sx, sy, t;
	char* g;

	g = mFieldData.mField + 3 * mWidth * mCurrentY;

	if (mCurrentY == 0 || mCurrentY == mHeight - 1)
	{
		for (px = 0; px < mWidth; px++)
		{
			__encode(0, 0)
		}

		mCurrentY++;
	}

	if (mCurrentY > 0 && mCurrentY < mHeight - 1)
	{
		mY_Cord = 0.5f * mYScale * (mHeight - 2 * mCurrentY);

		xscale2 = ((float)(1 << DEC_SIZE)) / mXScale;
		yscale2 = ((float)(1 << DEC_SIZE)) / mYScale;

		__encode(0, 0)

		for (px = 1; px < mWidth - 1; px++)
		{
			mX_Cord = 0.5f * mXScale * (2 * px - mWidth);

			if (mHasRTerm)
				mR_Cord = sqrtf(mX_Cord * mX_Cord + mY_Cord * mY_Cord);
			if (mHasThetaTerm)
				mT_Cord = atan2f(mY_Cord, mX_Cord);

			GetXY(mR_Cord, mT_Cord, mX_Cord, mY_Cord, fx, fy, mPolar, px == 1 && mCurrentY == 1);

			if (mPolar)
			{
				r = fx;
				fx = r * cosf(fy);
				fy = r * sinf(fy);
			}

			sx = (long)(xscale2 * (fx - mX_Cord));
			sy = (long)(yscale2 * (mY_Cord - fy));

			t = px + (sx >> DEC_SIZE);
			if (t >= mWidth - 1)
				sx = ((mWidth - 1 - px) << DEC_SIZE);
			else if (t < 0)
				sx = ((-px) << DEC_SIZE);

			t = mCurrentY + (sy >> DEC_SIZE);
			if (t >= mHeight - 1)
				sy = ((mHeight - 1 - mCurrentY) << DEC_SIZE);
			else if (t < 0)
				sy = ((-mCurrentY) << DEC_SIZE);

			if (sy < mNegYExtents)
				mNegYExtents = sy;

			__encode(sx, sy)
		}

		__encode(0, 0)

		mCurrentY += 1;
	}

	if (IsCalculated())
	{
		mFieldData.mNegYExtents = 1 - (mNegYExtents >> DEC_SIZE);
	}

	return mHeight - mCurrentY;
}

void DeltaField::GetXY(float r, float theta, float x, float y, float& X, float& Y, bool& bPolar, bool bInit)
{
	static float A0, A1, A2;
	bPolar = true;

	switch (m_nStyle)
	{
	default:
		m_nStyle = 0;
		// FALL THROUGH

	case 0: // Radial Breakaway
		X = r * (1.0f + 0.16f * atanf(0.55f - r));
		Y = theta - 0.01f;
		break;

	case 1: // Hip-no-therapy
		X = r * 0.87f;
		Y = theta - 0.075f;
		break;

	case 2: // Sunburst - Many
		if (bInit)
			A0 = trunc(8.0f * rnd(5.0f)) * 3.141592653f;
		X = ((1.0f + sinf(A0 * theta)) * 0.5f * 0.06f + 0.92f) * r;
		Y = theta;
		break;

	case 3: // Theta Divergence
		if (bInit)
		{
			A0 = 2.0f + rnd(16.0f);
			A1 = 0.01f + rnd(0.05f);
			A2 = 0.002f + rnd(0.006f);
		}
		X = pos(r - A2);
		Y = theta + A1 * sinf(A0 * r);
		break;

	case 4: // Turbo Flow Out
		X = 0.87f * r;
		Y = theta - 0.009f;
		break;

	case 5: // Turbo Flow Out - More
		X = 0.8f * r;
		Y = theta - 0.008f;
		break;

	case 6: // Boxilite
		if (bInit)
		{
			A0 = 2.0f + rnd(16.0f);
			A1 = 0.01f + rnd(0.05f);
		}
		X = x + A1 * sinf(A0 * y);
		Y = y + A1 * sinf(A0 * x);
		bPolar = false;
		break;

	case 7: // Collapse & Turn
		X = 1.01f * r;
		Y = theta + 0.021f;
		break;

	case 8: // Constant Out
		X = r - 0.01f;
		Y = theta;
		break;

	case 9: // Directrix Expand - X
		if (bInit)
		{
			A0 = rnd(0.0045f);
			A1 = 0.10f + rnd(0.06f);
			A2 = 0.003f + rnd(0.005f);
		}
		X = (0.99f - A1 * powf(fabsf(x), 1.3f)) * r - rnd(A0);
		Y = theta - A2;
		break;

	case 10: // Directrix Expand - Y
		if (bInit)
		{
			A0 = rnd(0.0045f);
			A1 = 0.10f + rnd(0.06f);
			A2 = 0.003f + rnd(0.006f);
		}
		X = (0.99f - A1 * powf(fabsf(y), 1.3f)) * r - rnd(A0);
		Y = theta + A2;
		break;

	case 11: // Equalateral Hyperbola
		X = r - 0.18f * x * y;
		Y = theta - 0.005f;
		break;

	case 12: // Expand & Turn
		X = 0.96f * r;
		Y = theta - 0.021f;
		break;

	case 13: // Gravity
		X = x + rnd(0.01f) - 0.005f;
		Y = y - rnd(0.01f) - 0.005f - (y - 1.3f) * 0.04f;
		bPolar = false;
		break;

	case 14: // In or Out, Inner Turn
		if (bInit)
			A0 = 0.95f + rnd(0.06f);
		X = r * A0;
		Y = theta - pos(1.0f - r) * 0.035f;
		break;

	case 15: // Left turn & Flow Out
		X = 0.94f * r;
		Y = theta - 0.007f;
		break;

	case 16: // Linear Spread
		X = 0.9f * x + rnd(0.008f) - 0.004f;
		Y = 0.9f * y + rnd(0.008f) - 0.004f;
		bPolar = false;
		break;

	case 17: // Noise Field
		X = x + 0.1f * (rnd(2.0f) - 1.0f);
		Y = y + 0.1f * (rnd(2.0f) - 1.0f);
		bPolar = false;
		break;

	case 18: // Right Turn
		X = r * 0.99f;
		Y = theta + 0.009f;
		break;

	case 19: // Scattered Flow Out
		X = (0.92f + rnd(0.05f)) * r;
		Y = theta + 0.003f;
		break;

	case 20: // Simple Sine-Sphere
		X = r + 0.04f * sinf(6.2831853f * r);
		Y = theta + 0.015f;
		break;

	case 21: // Sine Multi-Circ
		X = r * (0.87f + 0.05f * (1.0f + sinf(r * 15.0f)));
		Y = theta;
		break;

	case 22: // Sphere
		if (bInit)
		{
			A0 = rnd(0.1f);
			A1 = 3.0f + rnd(7.0f);
			A2 = 0.9f + rnd(0.3f);
		}
		X = r * (1.0f + 0.13f * (r - A2));
		Y = theta + A0 * sinf(A1 * r);
		break;

	case 23: // Sunburst - Few
		if (bInit)
			A0 = trunc(1.0f + rnd(2.1f)) * 3.141592653f;
		X = ((1.0f + cosf(A0 * theta)) * 0.5f * 0.1f + 0.89f) * r;
		Y = theta + 0.005f;
		break;
	}
}


// ===== CPalette =============================================================
// XAP node that generates a 256-entry DWORD palette from HSV presets.
// On Xbox, also uploads to a hardware D3DPalette object.

class CPalette : public CNode
{
	DECLARE_NODE(CPalette, CNode)
public:
	CPalette();
	~CPalette();

	int m_type;
	float m_changePeriod;
	float m_changePeriodRandomness;
	float m_timeToBlend;

	void Advance(float nSeconds);

	const DWORD* GetPalette() { return m_palette; }

#ifdef _XBOX
	D3DPalette* m_pPalette;
#endif

protected:
	void Update();
	void RenderDynamicTexture(CSurfx* pSurfx);

	DWORD m_palette [256];
	int m_nBlendPalette;
	float m_nBlendPaletteAmount;
	XTIME m_nextPaletteChangeTime;
	XTIME m_startBlendTime;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("Palette", CPalette, CNode)

START_NODE_PROPS(CPalette, CNode)
	NODE_PROP(pt_integer, CPalette, type)
	NODE_PROP(pt_number, CPalette, changePeriod)
	NODE_PROP(pt_number, CPalette, changePeriodRandomness)
END_NODE_PROPS()

CPalette::CPalette() :
	m_changePeriod(0.0f),
	m_changePeriodRandomness(0.0f),
	m_timeToBlend(2.0f),
	m_type(0)
{
	m_nextPaletteChangeTime = 0.0f;
	m_nBlendPalette = -1;
#ifdef _XBOX
	m_pPalette = NULL;
#endif
}

CPalette::~CPalette()
{
#ifdef _XBOX
	if (m_pPalette != NULL)
		m_pPalette->Release();
#endif
}

void CPalette::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	XTIME now = TheseusGetNow();

	if (m_changePeriod > 0.0f && now > m_nextPaletteChangeTime)
	{
		m_nextPaletteChangeTime = now + m_changePeriod + rnd(m_changePeriodRandomness);
		m_nBlendPalette = m_type;
		m_type += 1;
		m_nBlendPaletteAmount = 0.0f;
		m_startBlendTime = now;
	}

	Update();

	if (m_nBlendPalette != -1)
	{
		m_nBlendPaletteAmount = (float) (now - m_startBlendTime) / m_timeToBlend;
		if (m_nBlendPaletteAmount >= 1.0f)
			m_nBlendPalette = -1;
	}
}

void CPalette::Update()
{
	XTIME now = TheseusGetNow();
	float H, S, V, inc = 1.0f / 255.0f;
	float mIntensity = 0.0f;

	int i;
	for (i = 0; i < 256; i++, mIntensity += inc)
	{
		GetHSV(m_type, mIntensity, (float) now, H, S, V);
		m_type = g_nPalette;

		if (m_nBlendPalette != -1)
		{
			float H2, S2, V2;
			GetHSV(m_nBlendPalette, mIntensity, (float) now, H2, S2, V2);

			H = m_nBlendPaletteAmount * H + (1.0f - m_nBlendPaletteAmount) * H2;
			S = m_nBlendPaletteAmount * S + (1.0f - m_nBlendPaletteAmount) * S2;
			V = m_nBlendPaletteAmount * V + (1.0f - m_nBlendPaletteAmount) * V2;
		}

		HSV2RGB(H, S, V, m_palette[i]);
	}

#ifdef _XBOX
	// Xbox: also upload the DWORD palette into a hardware D3DPalette
	// object so SetPalette can bind it. Desktop applies the palette in
	// software when rendering each CDynamicTexture instead.
	if (m_pPalette == NULL)
		VERIFYHR(TheseusGetD3DDev()->CreatePalette(D3DPALETTE_256, &m_pPalette));

	D3DCOLOR* rgColor;
	VERIFYHR(m_pPalette->Lock(&rgColor, D3DLOCK_NOOVERWRITE));
	for (i = 0; i < 256; i += 1)
	{
		BYTE r = (BYTE)(m_palette[i] >> 16);
		BYTE g = (BYTE)(m_palette[i] >> 8);
		BYTE b = (BYTE)m_palette[i];
		rgColor[i] = D3DCOLOR_RGBA(r, g, b, 255);
	}
	VERIFYHR(m_pPalette->Unlock());
#endif
}

void CPalette::RenderDynamicTexture(CSurfx* pSurfx)
{
	for (int y = 0; y < pSurfx->m_nHeight; y += 1)
		FillMemory(pSurfx->m_pels + y * pSurfx->m_nWidth, pSurfx->m_nWidth, (BYTE)(y * 256 / pSurfx->m_nHeight));
}


// ===== CDynamicTexture ======================================================
// Renders children into a palettized 8-bit texture (D3DFMT_P8) each frame.
// Children contribute via RenderDynamicTexture(CSurfx*). The pixel buffer
// is swizzled into an Xbox texture on each update.

class CDynamicTexture : public CTexture
{
	DECLARE_NODE(CDynamicTexture, CTexture)
public:
	CDynamicTexture();
	~CDynamicTexture();

	CNodeArray m_children;
	int m_size;
	bool m_erase;
	float m_fps;
	CNode* m_palette;

	virtual bool Create(int nWidth, int nHeight);

	void Advance(float nSeconds);
	LPDIRECT3DTEXTURE8 GetTextureSurface();

protected:
	void Update();

	CSurfx* m_pSurfx;
	XTIME m_lastUpdateTime;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("DynamicTexture", CDynamicTexture, CTexture)

START_NODE_PROPS(CDynamicTexture, CTexture)
	NODE_PROP(pt_children, CDynamicTexture, children)
	NODE_PROP(pt_integer, CDynamicTexture, size)
	NODE_PROP(pt_boolean, CDynamicTexture, erase)
	NODE_PROP(pt_number, CDynamicTexture, fps)
	NODE_PROP(pt_node, CDynamicTexture, palette)
END_NODE_PROPS()

CDynamicTexture::CDynamicTexture() :
	m_size(256),
	m_erase(true),
	m_fps(15.0f),
	m_palette(NULL)
{
	m_lastUpdateTime = 0.0f;
	m_pSurfx = NULL;
	m_format = D3DFMT_P8;
}

CDynamicTexture::~CDynamicTexture()
{
	if (m_palette != NULL)
		m_palette->Release();

	delete m_pSurfx;
}

bool CDynamicTexture::Create(int nWidth, int nHeight)
{
	D3DLOCKED_RECT d3dlr;

	m_nImageWidth = 512;
	m_nImageHeight = 512;

	if (FAILED(D3DXCreateTexture(TheseusGetD3DDev(), m_nImageWidth, m_nImageHeight, 1, 0, m_format, D3DPOOL_MANAGED, &m_surface)))
	{
		return false;
	}

	m_surface->LockRect(0, &d3dlr, NULL, 0);
	memset(d3dlr.pBits, 0, m_nImageWidth * m_nImageHeight);
	m_surface->UnlockRect(0);

	TheseusGetTextureSize(m_surface, m_nImageWidth, m_nImageHeight);

	return true;
}

void CDynamicTexture::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_palette != NULL)
		m_palette->Advance(nSeconds);

	int nChildCount = m_children.GetLength();
	for (int i = 0; i < nChildCount; i += 1)
	{
		CNode* pChildNode = m_children.GetNode(i);
		pChildNode->Advance(nSeconds);
	}
}

LPDIRECT3DTEXTURE8 CDynamicTexture::GetTextureSurface()
{
	if (TheseusGetNow() > m_lastUpdateTime + (1.0f / m_fps))
	{
		m_lastUpdateTime = TheseusGetNow();
		Update();
	}

#ifdef _XBOX
	// Xbox: bind the hardware palette so the GPU expands paletted texels.
	// Desktop expands them in software during the LockRect upload below.
	if (m_palette != NULL && m_palette->IsKindOf(NODE_CLASS(CPalette)))
	{
		CPalette* pPalette = (CPalette*)m_palette;
		VERIFYHR(TheseusGetD3DDev()->SetPalette(0, pPalette->m_pPalette));
	}
#endif

	return m_surface;
}

void CDynamicTexture::Update()
{
	if (m_surface == NULL && m_size > 0)
	{
		if (!Create(m_size, m_size))
			return;
	}

	if (m_pSurfx == NULL && m_size > 0)
	{
		m_pSurfx = new CSurfx(m_size, m_size);
	}

	if (m_surface == NULL || m_pSurfx == NULL || TheseusGetNow() < m_lastUpdateTime)
		return;

	if (m_erase)
		ZeroMemory(m_pSurfx->m_pels, m_pSurfx->m_nWidth * m_pSurfx->m_nHeight);

	int nChildCount = m_children.GetLength();
	int i;
	for (i = 0; i < nChildCount; i += 1)
	{
		CNode* pChildNode = m_children.GetNode(i);
		pChildNode->RenderDynamicTexture(m_pSurfx);
	}

	// Upload the 8-bit pixmap into the texture surface.
	// Xbox: swizzle to NV2A tiled layout and let the GPU expand the palette.
	// Desktop: expand the palette in software (one ARGB pixel per byte) since
	// the GL emulator doesn't have palette-indexed texture support.
	{
		const DWORD* rgpe = NULL;

		if (m_palette != NULL)
			rgpe = m_palette->GetPalette();

		DWORD rgpeDefault [256];
		if (rgpe == NULL)
		{
			rgpe = rgpeDefault;
			MakePalette(rgpeDefault);
		}

#ifdef _XBOX
		Swizzler swz(m_nImageWidth, m_nImageHeight, 1);
		BYTE* pSrc = (BYTE*)m_pSurfx->m_pels;
		BYTE* pDst;
		D3DLOCKED_RECT lr;
		VERIFYHR(m_surface->LockRect(0, &lr, NULL, D3DLOCK_DISCARD));
		pDst = (BYTE*)lr.pBits;
		for (i = 0; i < m_size; i++, swz.IncV()) {
			for (int j = 0; j < m_size; j++, swz.IncU()) {
				pDst[swz.Get2D()] = pSrc[j];
			}
			pSrc += m_size;
			swz.AddU(swz.SwizzleU(m_nImageWidth - m_size));
		}
		VERIFYHR(m_surface->UnlockRect(0));
#else
		D3DLOCKED_RECT lr;
		VERIFYHR(m_surface->LockRect(0, &lr, NULL, D3DLOCK_DISCARD));
		uint32_t* pDest = (uint32_t*)lr.pBits;
		uint8_t* pbSrc = (uint8_t*)m_pSurfx->m_pels;
		int nPels = m_size * m_size;
		for (int i2 = 0; i2 < nPels; i2 += 1)
		{
			uint8_t b = *pbSrc++;
			*pDest++ = 0xff000000 | rgpe[b];
		}
		VERIFYHR(m_surface->UnlockRect(0));
#endif
	}
}


// ===== CImageFader ==========================================================
// Applies a DeltaField warp effect to a CSurfx. Cycles through warp
// styles on a timer.

class CImageFader : public CNode
{
	DECLARE_NODE(CImageFader, CNode)
public:
	CImageFader();
	~CImageFader();

	int m_type;
	float m_changePeriod;
	float m_changePeriodRandomness;

	void RenderDynamicTexture(CSurfx* pSurfx);
	bool OnSetProperty(const PRD* pprd, const void* pvValue);

protected:
	DeltaField* m_pDeltaField;
	DeltaField* m_pDeltaField2;
	XTIME m_nextChangeTime;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("ImageFader", CImageFader, CNode)

START_NODE_PROPS(CImageFader, CNode)
	NODE_PROP(pt_integer, CImageFader, type)
	NODE_PROP(pt_number, CImageFader, changePeriod)
	NODE_PROP(pt_number, CImageFader, changePeriodRandomness)
END_NODE_PROPS()

CImageFader::CImageFader() :
	m_type(0),
	m_changePeriod(0.0f),
	m_changePeriodRandomness(0.0f)
{
	m_nextChangeTime = 0.0f;
	m_pDeltaField = NULL;
	m_pDeltaField2 = NULL;
}

CImageFader::~CImageFader()
{
	delete m_pDeltaField;
	delete m_pDeltaField2;
}

void CImageFader::RenderDynamicTexture(CSurfx* pSurfx)
{
	if (m_pDeltaField == NULL)
	{
		m_pDeltaField = new DeltaField;
		m_pDeltaField->m_nStyle = m_type;
		m_pDeltaField->Assign();
		m_pDeltaField->SetSize(pSurfx->m_nWidth, pSurfx->m_nHeight);

		m_nextChangeTime = TheseusGetNow() + m_changePeriod + rnd(m_changePeriodRandomness);
	}
	else if (m_changePeriod > 0.0f && TheseusGetNow() >= m_nextChangeTime)
	{
		m_nextChangeTime = TheseusGetNow() + m_changePeriod;

		m_type += 1;

		delete m_pDeltaField;
		m_pDeltaField = m_pDeltaField2;
		m_pDeltaField2 = NULL;
	}

	if (m_pDeltaField2 == NULL)
	{
		m_pDeltaField2 = new DeltaField;
		m_pDeltaField2->m_nStyle = m_type + 1;
		m_pDeltaField2->Assign();
		m_pDeltaField2->SetSize(pSurfx->m_nWidth, pSurfx->m_nHeight);
	}

	if (m_pDeltaField->IsCalculating())
	{
		int i;
		for (i = 0; i < 10; i += 1)
			m_pDeltaField->CalcSome();
		m_type = m_pDeltaField->m_nStyle;
		return;
	}

	if (m_pDeltaField2->IsCalculating())
	{
		for (int i = 0; i < 5; i += 1)
		{
			if (m_pDeltaField2 != NULL)
				m_pDeltaField2->CalcSome();
		}
	}

	pSurfx->Fade(m_pDeltaField->GetField());
}

bool CImageFader::OnSetProperty(const PRD* pprd, const void* pvValue)
{
	if (PTR2INT(pprd->pbOffset) == offsetof(m_type))
	{
		delete m_pDeltaField;
		m_pDeltaField = m_pDeltaField2;
		m_pDeltaField2 = NULL;
	}

	return true;
}


// ===== CAudioVisualizer =====================================================
// Renders PCM waveform and FFT spectrum data from an AudioClip source
// into a CSurfx. Supports line scope, spinner scope, spectrum analyzer,
// and spectral overlay effects.
//
// Xbox-only here -- desktop has its own SDL_mixer-backed CAudioVisualizer
// in desktop/desktop_nodes.cpp because the audio sample buffer comes from
// DashAudio_GetPCMSamples() (a ring buffer), not from CAudioClip::GetSampleBuffer().
#ifdef _XBOX

class CAudioVisualizer : public CNode
{
	DECLARE_NODE(CAudioVisualizer, CNode)
public:
	CAudioVisualizer();
	~CAudioVisualizer();

	TCHAR* m_type;
	TCHAR* m_channel;
	CNode* m_source;
	float m_scale;
	float m_offset;

protected:
	short* GetMonoPCM();
	void UpdateSpectrum();

	void RenderDynamicTexture(CSurfx* pSurfx);
	void Advance(float nSeconds);
	void CalcSpectrum(short* pcm, short* fft);

	void RenderEffect1(CSurfx* pSurfx);
	void RenderEffect2(CSurfx* pSurfx);

	short m_pcmLeft [256];
	short m_pcmRight [256];

	short m_fftLeft [128];
	short m_fftRight [128];
	bool m_bFFTValid;
	short m_pcmMono [256];
	bool m_bMonoValid;

	DECLARE_NODE_PROPS()
};

IMPLEMENT_NODE("AudioVisualizer", CAudioVisualizer, CNode)

START_NODE_PROPS(CAudioVisualizer, CNode)
	NODE_PROP(pt_number, CAudioVisualizer, scale)
	NODE_PROP(pt_number, CAudioVisualizer, offset)
	NODE_PROP(pt_string, CAudioVisualizer, type)
	NODE_PROP(pt_string, CAudioVisualizer, channel)
	NODE_PROP(pt_node, CAudioVisualizer, source)
END_NODE_PROPS()

CAudioVisualizer::CAudioVisualizer() :
	m_type(NULL),
	m_source(NULL),
	m_scale(1.0f),
	m_offset(0.0f),
	m_channel(NULL)
{
}

CAudioVisualizer::~CAudioVisualizer()
{
	delete [] m_type;
	delete [] m_channel;

	if (m_source != NULL)
		m_source->Release();
}

void CAudioVisualizer::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_source != NULL)
		m_source->Advance(nSeconds);
}

short* CAudioVisualizer::GetMonoPCM()
{
	if (!m_bMonoValid)
	{
		int i;
		for (i = 0; i < 256; i += 1)
		{
			m_pcmMono[i] = (short)(((int)m_pcmLeft[i] + (int)m_pcmRight[i]) / 2);
		}

		m_bMonoValid = true;
	}

	return m_pcmMono;
}

void CAudioVisualizer::CalcSpectrum(short* pcm, short* fft)
{
	static fft_state *state = NULL;
	float buf [FFT_BUFFER_SIZE / 2 + 1];

	if (!state)
		state = fft_init();

	fft_perform(pcm, buf, state);

	for (int i = 0; i < FFT_BUFFER_SIZE / 2 + 1; i += 1)
	{
		buf[i] = sqrtf(buf[i]) / FFT_BUFFER_SIZE;
		fft[i] = (short)buf[i];
	}
}

void CAudioVisualizer::UpdateSpectrum()
{
	if (m_bFFTValid)
		return;

	CalcSpectrum(m_pcmLeft, m_fftLeft);
	CalcSpectrum(m_pcmRight, m_fftRight);

	m_bFFTValid = true;
}

#define mag(s) ((float)samples[(int)(s * nSamples) * 2 + lrc] / 32767.0f)

void CAudioVisualizer::RenderDynamicTexture(CSurfx* pSurfx)
{
	CAudioClip* pAudioClip = (CAudioClip*)m_source;
	if (pAudioClip == NULL || pAudioClip->GetNodeClass() != NODE_CLASS(CAudioClip) || pAudioClip->m_transportMode != TRANSPORT_PLAY)
		return;

	short* samples = (short*)pAudioClip->GetSampleBuffer();
	if (samples == NULL)
		return;

	int nSamples = pAudioClip->GetSampleBufferSize() / 4;

	if (nSamples <= 0)
		return;

	// Setup local PCM buffers
	{
		if (nSamples > 256)
			nSamples = 256;

		int i;
		for (i = 0; i < nSamples; i += 1)
		{
			m_pcmLeft[i] = samples[i * 2];
			m_pcmRight[i] = samples[i * 2 + 1];
		}

		for (; i < 256; i += 1)
		{
			m_pcmLeft[i] = 0;
			m_pcmRight[i] = 0;
		}

		m_bMonoValid = false;
		m_bFFTValid = false;
	}

	if (nSamples > pSurfx->m_nHeight)
		nSamples = pSurfx->m_nHeight;

	int lrc = 0;
	if (m_channel != NULL)
	{
		if (m_channel[0] == 'r' || m_channel[0] == 'R')
			lrc = 1;
	}

	int nType = 0;
	if (m_type != NULL)
	{
		switch (m_type[0])
		{
		case 's': case 'S': nType = 1; break; // spinner
		case 'c': case 'C': nType = 2; break; // circle scope
		case 'a': case 'A': nType = 3; break; // spectrum analyzer
		}
	}

	float t = (float) TheseusGetNow();

	switch (nType)
	{
	case 0: // Line Scope
		{
			int xCenter = pSurfx->m_nWidth / 2;
			int xp = xCenter;
			int yp = 0;

			for (int y = 0; y < nSamples; y += 1)
			{
				long s = (((long)samples[y * 2 + lrc]) * pSurfx->m_nWidth) >> 16;
				int x = xCenter + s;

				pSurfx->Line(xp, yp, x, y, 255);
				xp = x;
				yp = y;
			}
		}
		break;

	case 1: // Spinner Scope
		{
			int xCenter = pSurfx->m_nWidth / 2;
			int yCenter = pSurfx->m_nHeight / 2;

			float step = 1.0f / (float)pSurfx->m_nWidth;

			float firstX, firstY;
			float prevX, prevY;

			float A0 = 0.5f + rnd(0.2f);

			float B0 = cosf(t * 0.2f);
			float B1 = sinf(t * 0.2f);

			for (float s = 0.0f; s <= 1.0; s += step)
			{
				float C0 = mag(s) * m_scale + m_offset;
				float C1 = 2.1f * (s - 0.5f);

				float X0 = B0 * C1 + B1 * C0;
				float Y0 = -B0 * C0 + B1 * C1;

				float X1 = B0 * C1 - B1 * C0;
				float Y1 = B0 * C0 + B1 * C1;

				if (s == 0.0f)
				{
					firstX = X0;
					firstY = Y0;
				}
				else
				{
					pSurfx->Line(xCenter + (int)(prevX * xCenter), yCenter + (int)(prevY * yCenter),
						xCenter + (int)(X0 * xCenter), yCenter + (int)(Y0 * yCenter), 255);
				}

				prevX = X0;
				prevY = Y0;
			}
		}
		break;

	case 3: // Spectrum Analyzer
		{
			UpdateSpectrum();

			static int peak_buf [256];
			BYTE spectrum [256];

			int i;
			for (i = 0; i < 128; i += 1)
			{
				float n = logf(m_fftLeft[127 - i]) * 8.0f;
				if (n <= 0.0f)
					spectrum[i] = 0;
				else if (n >= 255.0f)
					spectrum[i] = 255;
				else
					spectrum[i] = (BYTE)n;
			}

			for (; i < 256; i += 1)
			{
				float n = logf(m_fftRight[i - 128]) * 8.0f;
				if (n <= 0.0f)
					spectrum[i] = 0;
				else if (n >= 255.0f)
					spectrum[i] = 255;
				else
					spectrum[i] = (BYTE)n;
			}

			for (i = 0; i < 256; i += 1)
			{
				if (peak_buf[i] > 2)
					peak_buf[i] -= 2;
				else
					peak_buf[i] = 0;

				int y = spectrum[i] * 2;
				if (peak_buf[i] < y)
					peak_buf[i] = y;
			}

			int nWidth = pSurfx->m_nWidth;
			for (int x = 0; x < nWidth; x += 1)
			{
				float nHeight = ((((float)spectrum[(x * 256) / nWidth])) * (float)pSurfx->m_nHeight) / 256.0f;
				BYTE bColor = 255;
				for (int y = pSurfx->m_nHeight - (int)nHeight; y < pSurfx->m_nHeight; y += 1, bColor -= 1)
					*pSurfx->Pixel(x, y) = bColor;

				nHeight = (float)peak_buf[(x * 256) / nWidth];
				nHeight /= 2.0f;
				nHeight = (nHeight * (float)pSurfx->m_nHeight) / 256.0f;
				*pSurfx->Pixel(x, pSurfx->m_nHeight - (int)nHeight) = 255;
			}

			RenderEffect1(pSurfx);
			RenderEffect2(pSurfx);
		}
		break;
	}
}


// Bass beat detection for spectrum analyzer flash effect
#define BASS_EXT_MEMORY 10

struct bass_info {
	int max_recent;
	int max_old;
	int time_last_max;
	int min_recent;
	int min_old;
	int time_last_min;
	int activated;
} bass_info;

void CAudioVisualizer::RenderEffect1(CSurfx* pSurfx)
{
	static int t = 0;

	int bass = 0;
	const int step = 5;
	int i;
	for (i = 0; i < step; i += 1)
		bass += (m_fftLeft[i] >> 4) + (m_fftRight[i] >> 4);
	bass /= (step * 2);

	if (bass > bass_info.max_recent)
		bass_info.max_recent = bass;

	if (bass < bass_info.min_recent)
		bass_info.min_recent = bass;

	if (t - bass_info.time_last_max > BASS_EXT_MEMORY)
	{
		bass_info.max_old = bass_info.max_recent;
		bass_info.max_recent = 0;
		bass_info.time_last_max = t;
	}

	if (t - bass_info.time_last_min > BASS_EXT_MEMORY)
	{
		bass_info.min_old = bass_info.min_recent;
		bass_info.min_recent = 0;
		bass_info.time_last_min = t;
	}

	if (bass > (bass_info.max_old * 6 + bass_info.min_old * 4) / 10 && bass_info.activated == 0)
	{
		FillMemory(pSurfx->m_pels, pSurfx->m_nWidth * pSurfx->m_nHeight, 255);
		bass_info.activated = 1;
	}

	if (bass < (bass_info.max_old * 4 + bass_info.min_old * 6) / 10 && bass_info.activated == 1)
		bass_info.activated = 0;

	t += 1;
}


// Spectral overlay effect with Lissajous curve
#define PI D3DX_PI

struct sincos {
	int i;
	float *f;
};
static struct sincos cosw = { 0, NULL };
static struct sincos sinw = { 0, NULL };

int spectral_amplitude = 50;
int spectral_shift = 30;
int mode_spectre = -1;
BYTE spectral_color = 128;

void SetPixel(CSurfx* pSurfx, int x, int y, BYTE color)
{
	if (x < 0 || y < 0 || x >= pSurfx->m_nWidth || y >= pSurfx->m_nHeight)
		return;
	*pSurfx->Pixel(x, y) = color;
}

void SetPixel2(CSurfx* pSurfx, int x, int y, BYTE color)
{
	SetPixel(pSurfx, x, y, color);
	SetPixel(pSurfx, x + 1, y, color);
	SetPixel(pSurfx, x + 1, y + 1, color);
	SetPixel(pSurfx, x, y + 1, color);
}

void CAudioVisualizer::RenderEffect2(CSurfx* pSurfx)
{
	int halfheight, halfwidth;
	float old_y1, old_y2;
	float y1 = (float)((((m_pcmLeft[0] + m_pcmRight[0]) >> 9) * spectral_amplitude * pSurfx->m_nHeight) >> 12);
	float y2 = (float)((((m_pcmLeft[0] + m_pcmRight[0]) >> 9) * spectral_amplitude * pSurfx->m_nHeight) >> 12);
	const int density_lines = 5;
	const int step = 4;
	const int shift = (spectral_shift * pSurfx->m_nHeight) >> 8;

	static XTIME timeToChange = 0.0f;
	if (mode_spectre < 0 || TheseusGetNow() >= timeToChange)
	{
		mode_spectre += 1;
		timeToChange = TheseusGetNow() + 3.0f + rnd(5.0f);
	}

	if ((UINT)mode_spectre > 4)
		mode_spectre = 0;

	if (cosw.i != pSurfx->m_nWidth || sinw.i != pSurfx->m_nWidth)
	{
		delete [] cosw.f;
		delete [] sinw.f;
		sinw.f = cosw.f = NULL;
		sinw.i = cosw.i = 0;
	}

	if (cosw.i == 0 || cosw.f == NULL)
	{
		const float halfPI = (float)PI / 2;
		cosw.i = pSurfx->m_nWidth;
		cosw.f = new float [pSurfx->m_nWidth];
		int i;
		for (i = 0; i < pSurfx->m_nWidth; i += step)
			cosw.f[i] = cosf((float)i / pSurfx->m_nWidth * PI + halfPI);
	}

	if (sinw.i == 0 || sinw.f == NULL)
	{
		const float halfPI = (float)PI / 2;
		sinw.i = pSurfx->m_nWidth;
		sinw.f = new float [pSurfx->m_nWidth];
		int i;
		for (i = 0; i < pSurfx->m_nWidth; i += step)
			sinw.f[i] = sinf((float)i / pSurfx->m_nWidth * PI + halfPI);
	}

	if (mode_spectre == 3)
	{
		if (y1 < 0) y1 = 0;
		if (y2 < 0) y2 = 0;
	}

	halfheight = pSurfx->m_nHeight >> 1;
	halfwidth  = pSurfx->m_nWidth >> 1;

	int i;
	for (i = step; i < pSurfx->m_nWidth; i += step)
	{
		old_y1 = y1;
		old_y2 = y2;

		y1 = (float)(((m_pcmRight[(i << 8) / pSurfx->m_nWidth / density_lines] >> 8) * spectral_amplitude * pSurfx->m_nHeight) >> 12);
		y2 = (float)(((m_pcmLeft[(i << 8) / pSurfx->m_nWidth / density_lines] >> 8) * spectral_amplitude * pSurfx->m_nHeight) >> 12);

		switch (mode_spectre)
		{
		case 0:
			pSurfx->Line(i - step, (int)(halfheight + shift + old_y2),
				i, (int)(halfheight + shift + y2),
				spectral_color);
			break;

		case 1:
			pSurfx->Line(i - step, (int)(halfheight + shift + old_y1),
				i, (int)(halfheight + shift + y1),
				spectral_color);
			pSurfx->Line(i - step, (int)(halfheight - shift + old_y2),
				i, (int)(halfheight - shift + y2),
				spectral_color);
			break;

		case 2:
			pSurfx->Line(i - step, (int)(halfheight + shift + old_y1),
				i, (int)(halfheight + shift + y1),
				spectral_color);
			pSurfx->Line(i - step, (int)(halfheight - shift + old_y1),
				i, (int)(halfheight - shift + y1),
				spectral_color);
			pSurfx->Line((int)(halfwidth + shift + old_y2), i - step,
				(int)(halfwidth + shift + y2), i,
				spectral_color);
			pSurfx->Line((int)(halfwidth - shift + old_y2), i - step,
				(int)(halfwidth - shift + y2), i,
				spectral_color);
			break;

		case 3:
			if (y1 < 0) y1 = 0;
			if (y2 < 0) y2 = 0;
			// FALL THROUGH

		case 4:
			pSurfx->Line(
				(int)(halfwidth  + cosw.f[i - step] * (shift + old_y1)),
				(int)(halfheight + sinw.f[i - step] * (shift + old_y1)),
				(int)(halfwidth  + cosw.f[i]        * (shift + y1)),
				(int)(halfheight + sinw.f[i]        * (shift + y1)),
				spectral_color);
			pSurfx->Line(
				(int)(halfwidth  - cosw.f[i - step] * (shift + old_y2)),
				(int)(halfheight + sinw.f[i - step] * (shift + old_y2)),
				(int)(halfwidth  - cosw.f[i]        * (shift + y2)),
				(int)(halfheight + sinw.f[i]        * (shift + y2)),
				spectral_color);
			break;
		}
	}

	if (mode_spectre == 3 || mode_spectre == 4)
	{
		pSurfx->Line(
			(int)(halfwidth  + cosw.f[pSurfx->m_nWidth - step] * (shift + y1)),
			(int)(halfheight + sinw.f[pSurfx->m_nWidth - step] * (shift + y1)),
			(int)(halfwidth  - cosw.f[pSurfx->m_nWidth - step] * (shift + y2)),
			(int)(halfheight + sinw.f[pSurfx->m_nWidth - step] * (shift + y2)),
			spectral_color);
	}

#define curve_color 255
#define curve_amplitude 50
	static int x_curve = 0;

	// Lissajous curve overlay
	{
		int i, j, k;
		float v, vr;
		float x, y;
		float amplitude = (float)curve_amplitude / 256;

		for (j = 0; j < 2; j += 1)
		{
			v = 80;
			vr = 0.001f;
			k = x_curve;
			for (i = 0; i < 64; i += 1)
			{
				x = cosf((float)(k) / (v + v * j * 1.34f)) * pSurfx->m_nHeight * amplitude;
				y = sinf((float)(k) / (1.756f * (v + v * j * 0.93f))) * pSurfx->m_nHeight * amplitude;
				SetPixel2(pSurfx, (int)(x * cosf((float)k * vr) + y * sinf((float)k * vr) + pSurfx->m_nWidth / 2), (int)(x * sinf((float)k * vr) - y * cosf((float)k * vr) + pSurfx->m_nHeight / 2), curve_color);
				k++;
			}
		}

		x_curve = k;
	}
}

#endif // _XBOX -- end CAudioVisualizer Xbox-only block


// ===== FFT ==================================================================
// Radix-2 Cooley-Tukey FFT. Input: FFT_BUFFER_SIZE sound_samples.
// Output: FFT_BUFFER_SIZE/2+1 intensity floats. Pure math, used by both
// Xbox CAudioVisualizer above and desktop's CAudioVisualizer in
// desktop/desktop_nodes.cpp.

#undef PI
#define PI 3.14159265358979323846f

struct _struct_fft_state
{
	float real [FFT_BUFFER_SIZE];
	float imag [FFT_BUFFER_SIZE];
};

static void fft_prepare(const sound_sample *input, float * re, float * im);
static void fft_calculate(float * re, float * im);
static void fft_output(const float *re, const float *im, float *output);
static int reverseBits(unsigned int initial);

static unsigned int bitReverse [FFT_BUFFER_SIZE];

static float sintable [FFT_BUFFER_SIZE / 2];
static float costable [FFT_BUFFER_SIZE / 2];

fft_state* fft_init(void)
{
	fft_state *state;
	unsigned int i;

	state = (fft_state*)malloc(sizeof (fft_state));
	if (state == NULL)
		return NULL;

	for (i = 0; i < FFT_BUFFER_SIZE; i++)
		bitReverse[i] = reverseBits(i);

	for (i = 0; i < FFT_BUFFER_SIZE / 2; i++)
	{
		float j = 2.0f * PI * (float)i / (float)FFT_BUFFER_SIZE;
		costable[i] = cosf(j);
		sintable[i] = sinf(j);
	}

	return state;
}

void fft_perform(const sound_sample* input, float* output, fft_state* state)
{
	fft_prepare(input, state->real, state->imag);
	fft_calculate(state->real, state->imag);
	fft_output(state->real, state->imag, output);
}

void fft_close(fft_state *state)
{
	if (state != NULL)
		free(state);
}

static void fft_prepare(const sound_sample* input, float* re, float* im)
{
	unsigned int i;
	float* realptr = re;
	float* imagptr = im;

	for (i = 0; i < FFT_BUFFER_SIZE; i++)
	{
		*realptr++ = input[bitReverse[i]];
		*imagptr++ = 0;
	}
}

static void fft_output(const float* re, const float* im, float* output)
{
	float* outputptr = output;
	const float* realptr = re;
	const float* imagptr = im;
	float* endptr = output + FFT_BUFFER_SIZE / 2;

	while (outputptr <= endptr)
	{
		*outputptr = (*realptr * *realptr) + (*imagptr * *imagptr);
		outputptr++; realptr++; imagptr++;
	}

	*output /= 4;
	*endptr /= 4;
}

static void fft_calculate(float* re, float* im)
{
	unsigned int i, j, k;
	unsigned int exchanges;
	float fact_real, fact_imag;
	float tmp_real, tmp_imag;
	unsigned int factfact;

	exchanges = 1;
	factfact = FFT_BUFFER_SIZE / 2;

	for (i = FFT_BUFFER_SIZE_LOG; i != 0; i--)
	{
		for (j = 0; j != exchanges; j++)
		{
			fact_real = costable[j * factfact];
			fact_imag = sintable[j * factfact];

			for (k = j; k < FFT_BUFFER_SIZE; k += exchanges << 1)
			{
				int k1 = k + exchanges;

				tmp_real = fact_real * re[k1] - fact_imag * im[k1];
				tmp_imag = fact_real * im[k1] + fact_imag * re[k1];
				re[k1] = re[k] - tmp_real;
				im[k1] = im[k] - tmp_imag;
				re[k] += tmp_real;
				im[k] += tmp_imag;
			}
		}

		exchanges <<= 1;
		factfact >>= 1;
	}
}

static int reverseBits(unsigned int initial)
{
	unsigned int reversed = 0, loop;

	for (loop = 0; loop < FFT_BUFFER_SIZE_LOG; loop++)
	{
		reversed <<= 1;
		reversed += (initial & 1);
		initial >>= 1;
	}

	return reversed;
}
