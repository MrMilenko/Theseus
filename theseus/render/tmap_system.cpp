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
#ifndef _XBOX
#include "milkdrop_window.h"
extern "C" int DashMusic_GetCurrentSoundtrackIdx(void);
extern "C" const unsigned char* DashMusic_GetAlbumRGBA(int, int*, int*);
#endif
#define FFT_BUFFER_SIZE_LOG 8
#define FFT_BUFFER_SIZE (1 << FFT_BUFFER_SIZE_LOG)
typedef short int sound_sample;
typedef struct _struct_fft_state fft_state;
fft_state* fft_init (void);
void fft_perform (const sound_sample* input, float* output, fft_state* state);
void fft_close (fft_state* state);


// ===== CSurfx ===============================================================
// 8-bit palettized pixel surface. Every TMAP effect draws into one of these.

int CSurfx::sTempRef = 0;
long CSurfx::sTempSize = 0;
char* CSurfx::sTemp = NULL;

// CSurfx::CSurfx(nWidth, nHeight)
// Reconstructed from 5960 retail XBE @ 0x0005f3f0 (inlined into
// CDynamicTexture's update at 0x0005dde0). Ghidra decompile:
//     w = nWidth; h = nHeight; pels = new[(h + 1) * w];
//     lineWidth = 1; clip = {0, 0, w, h}; bytesPerRow = w; ++DAT_0017e75c;
// The spare row under the image catches the analyzer's peak dots at y == h.
CSurfx::CSurfx(int nWidth, int nHeight)
{
	m_nWidth = nWidth;
	m_nHeight = nHeight;
	m_pels = new BYTE [(nHeight + 1) * nWidth];
	mBytesPerRow = nWidth;
	mLineWidth = 1;
	mClipRect.left = 0;
	mClipRect.top = 0;
	mClipRect.right = nWidth;
	mClipRect.bottom = nHeight;
	sTempRef++;
}

// CSurfx::~CSurfx()
// Reconstructed from 5960 retail XBE, inlined into CDynamicTexture_dtor @ 0x0005dbc0:
//     free(pels); if (--DAT_0017e75c == 0) { free(DAT_0017e764); DAT_0017e764 = 0; DAT_0017e760 = 0; }
// The last surface to go frees the shared fade scratch buffer.
CSurfx::~CSurfx()
{
	delete [] m_pels;
	if (--sTempRef == 0)
	{
		delete [] sTemp;
		sTemp = NULL;
		sTempSize = 0;
	}
}

// A delta field cell is 24 bits: source x offset in the top 12, source y in
// the bottom 12, both in 1/32 pixel steps and biased by 63 pixels (0x7E0).
#define DELTA_BIAS_PELS	63
#define DELTA_FRAC_BITS	5

static inline unsigned long ReadDelta(const char* cell)
{
	const unsigned char* b = (const unsigned char*)cell;
	return b[0] | (b[1] << 8) | ((unsigned long)b[2] << 16);
}

// Bilinear pull for one destination pixel. base is the source pixel that a
// zero offset lands on, already shifted back by the 63 pixel bias.
static inline BYTE PullPel(const char* base, long bytesPerRow, unsigned long code)
{
	unsigned long dx = (code >> 12) & 0xFFF;
	unsigned long dy = code & 0xFFF;
	const unsigned char* p = (const unsigned char*)base + (dx >> DELTA_FRAC_BITS) + (dy >> DELTA_FRAC_BITS) * bytesPerRow;
	unsigned long fx = dx & 31;
	unsigned long fy = dy & 31;

	unsigned long left = p[0] * (32 - fy) + p[bytesPerRow] * fy;
	unsigned long right = p[1] * (32 - fy) + p[bytesPerRow + 1] * fy;

	// 32 * 32 weights sum to 1024; the * 31 >> 15 also dims each pass by 1/32.
	return (BYTE)(((left * (32 - fx) + right * fx) * 31) >> 15);
}

// CSurfx::Fade(inGrad)
// Reconstructed from the call in CImageFader_RenderDynamicTexture @ 0x0005e120:
//     Fade(pels, width, width, height, field)   width doubles as the row pitch.
void CSurfx::Fade(DeltaFieldData* inGrad)
{
	Fade((char*)m_pels, m_nWidth, m_nWidth, m_nHeight, inGrad);
}

// CSurfx::Fade(inSrce, srcPitch, inDest, destPitch, inX, inY, inGrad)
// Reconstructed from 5960 retail XBE @ 0x0005f430. Ghidra decompile:
//     src += -0x3f * srcPitch - 0x3f;
//     for each row: for x < inX: code = *(uint*)grad; grad += 3;
//         p = src + x + (code>>12 & 0xfff)>>5 + ((code & 0xfff)>>5) * srcPitch;
//         dest[x] = ((p[pitch]*fy + p[0]*(32-fy))*(32-fx) + (p[pitch+1]*fy + p[1]*(32-fy))*fx) * 0x1f >> 0xf;
//       dest += destPitch; src += srcPitch;
// Out-of-place pass: warps inY rows of source into a separate buffer.
void CSurfx::Fade(const char* inSrce, long inBytesPerSrceRow, char* inDest, long inBytesPerDestRow, long inX, long inY, const char* inGrad)
{
	const char* base = inSrce - DELTA_BIAS_PELS * inBytesPerSrceRow - DELTA_BIAS_PELS;

	for (long row = 0; row < inY; row++)
	{
		for (long x = 0; x < inX; x++, inGrad += 3)
			inDest[x] = (char)PullPel(base + x, inBytesPerSrceRow, ReadDelta(inGrad));

		base += inBytesPerSrceRow;
		inDest += inBytesPerDestRow;
	}
}

// CSurfx::Fade(inPix, inBytesPerRow, inX, inY, inGrad)
// Reconstructed from 5960 retail XBE @ 0x0005f540. Warps the image in place.
// A pixel can pull from up to mNegYExtents rows above itself, so finished rows
// wait in a ring of that many rows (the shared DAT_0017e764 buffer, grown on
// demand) and only overwrite the image once nothing can read them anymore.
// Steps: first ring-full of rows goes through the out-of-place pass; each later
// row swaps the oldest ring row into the image and computes into its slot; the
// tail of the ring is flushed in order, inX / 4 dwords per row like the binary.
// See docs/decomp/TmapSystem.md.
void CSurfx::Fade(char* inPix, long inBytesPerRow, long inX, long inY, DeltaFieldData* inGrad)
{
	long ringRows = inGrad->mNegYExtents;
	if (sTempSize < ringRows * inBytesPerRow)
	{
		sTempSize = ringRows * inBytesPerRow;
		delete [] sTemp;
		sTemp = new char [sTempSize];
	}
	if (ringRows > inY)
		ringRows = inY;

	const char* grad = inGrad->mField;
	Fade(inPix, inBytesPerRow, sTemp, inBytesPerRow, inX, ringRows, grad);
	grad += 3 * inX * ringRows;

	const char* base = inPix + (ringRows - DELTA_BIAS_PELS) * inBytesPerRow - DELTA_BIAS_PELS;
	long slot = 0;

	for (long row = ringRows; row < inY; row++)
	{
		char* ring = sTemp + slot * inBytesPerRow;
		for (long x = 0; x < inX; x++, grad += 3)
		{
			inPix[x] = ring[x];
			ring[x] = (char)PullPel(base + x, inBytesPerRow, ReadDelta(grad));
		}

		slot = (slot + 1) % ringRows;
		inPix += inBytesPerRow;
		base += inBytesPerRow;
	}

	for (long i = 0; i < ringRows; i++)
	{
		memcpy(inPix, sTemp + ((slot + i) % ringRows) * inBytesPerRow, (inX / 4) * 4);
		inPix += inBytesPerRow;
	}
}


// ===== HSV Palette ==========================================================
// The 10 palette recipes behind every TMAP texture, plus the shared default
// palette used when a DynamicTexture has none of its own.

// Last recipe GetHSV ran; out of range requests come back here as 0.
int g_nPalette = -1;

static inline int Clamp255(int c)
{
	return c < 0 ? 0 : (c > 255 ? 255 : c);
}

// HSV2RGB(H, S, V, outRGB)
// Written from first principles: the standard six sector hue wheel, H wrapped
// to [0, 1), S and V clamped to [0, 1], packed as 0x00RRGGBB. Matches the 5960
// routine @ 0x0005f7e0, which truncates every channel of V * 255 with
// cvttss2si and clamps through its packer @ 0x0005f770.
void HSV2RGB(float H, float S, float V, DWORD& outRGB)
{
	float h6 = (H - floorf(H)) * 6.0f;
	int sector = (int)h6;
	float f = h6 - sector;

	S = S < 0.0f ? 0.0f : (S > 1.0f ? 1.0f : S);
	V = V < 0.0f ? 0.0f : (V > 1.0f ? 1.0f : V);

	float v255 = V * 255.0f;
	int v = (int)v255;
	int p = (int)(v255 * (1.0f - S));
	int q = (int)(v255 * (1.0f - S * f));
	int t = (int)(v255 * (1.0f - S * (1.0f - f)));

	int r, g, b;
	switch (sector)
	{
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	case 5: r = v; g = p; b = q; break;
	default: r = v; g = t; b = p; break;
	}

	outRGB = (Clamp255(r) << 16) | (Clamp255(g) << 8) | Clamp255(b);
}

// GetHSV(nPalette, i, t, H, S, V)
// Reconstructed from 5960 retail XBE @ 0x0005f920. Maps intensity i (0..1) at
// time t (seconds) to a color for one of 10 recipes; writes the recipe it used
// to g_nPalette (0x00175e70) first, and anything past 9 snaps to 0. The pow
// exponents are the doubles at 0x0002c068..0x0002c0f8; wrap() is the
// x - floor(x) helper @ 0x0005f3d0. See docs/decomp/TmapSystem.md for the table.
void GetHSV(int nPalette, float i, float t, float& H, float& S, float& V)
{
	g_nPalette = nPalette;

	switch (nPalette)
	{
	case 1:
		H = 0.45f;
		S = powf(1.0f - i, 0.3f);
		V = powf(i, 0.7f);
		return;

	case 2:
		H = powf(i, 1.4f) * 0.166f + 0.666f;
		S = (powf(i * 1.8f - 1.0f, 2.0f) + 1.0f) * 0.5f;
		V = powf(i, 0.4f);
		return;

	case 3:
		H = wrap(t * 0.03f);
		S = 1.0f - powf(i, 2.5f) * 0.6f;
		V = i;
		return;

	case 4:
		H = wrap(0.3f - t * 0.003f);
		S = powf(i, 2.9f);
		V = powf(i, 0.9f);
		return;

	case 5:
		H = wrap(t * 0.02f);
		S = 1.0f;
		V = 1.0f - powf(i, 1.4f);
		return;

	case 6:
		H = powf(i, 1.6f);
		S = 1.0f;
		V = i;
		return;

	case 7:
		H = wrap(powf(0.7f - i * 0.4f, 0.6f));
		S = 0.9f;
		V = powf(i, 0.9f);
		return;

	case 8:
		H = wrap(t * 0.002f);
		S = powf(1.0f - i, 1.5f);
		V = powf(i, 0.4f);
		return;

	case 9:
		H = wrap((powf(cosf(i * 4.0f), 2.0f) + powf(i, 2.0f)) * 0.2f + t * 0.05f);
		S = i * 0.5f + 0.85f;
		V = powf(i, 0.55f);
		return;

	default:
		g_nPalette = 0;
		// fall through
	case 0:
		H = powf(i, 1.9f) * 0.166f;
		S = 1.0f;
		V = powf(i, 0.9f);
		return;
	}
}

// Crossfade from the previous recipe, shared by MakePalette and CPalette.
static inline void BlendHSV(float amount, float H2, float S2, float V2, float& H, float& S, float& V)
{
	H = amount * H + (1.0f - amount) * H2;
	S = amount * S + (1.0f - amount) * S2;
	V = amount * V + (1.0f - amount) * V2;
}

int g_nBlendPalette;
float g_nBlendPaletteAmount;

// MakePalette(outPalette)
// Reconstructed from 5960 retail XBE @ 0x0005fc30. The default palette: every
// 10 + rnd(5) seconds (0x0002c148, 0x0002c140) it moves on to the next recipe
// and crossfades from the old one. The blend amount grows by half the time
// since the switch on every call (0x0002bed4), so it accumulates rather than
// tracking elapsed time. During a fade the second lookup leaves g_nPalette on
// the old recipe, so from entry 1 on both lookups read it; the binary does the same.
void MakePalette(DWORD outPalette [256])
{
	static XTIME nextChange = 0.0f;
	static XTIME blendStart;

	XTIME now = TheseusGetNow();
	if (now > nextChange)
	{
		nextChange = now + 10.0f + rnd(5.0f);
		g_nBlendPalette = g_nPalette;
		g_nPalette++;
		g_nBlendPaletteAmount = 0.0f;
		blendStart = now;
	}

	float intensity = 0.0f;
	for (int n = 0; n < 256; n++, intensity += 1.0f / 255.0f)
	{
		float H, S, V;
		GetHSV(g_nPalette, intensity, (float)now, H, S, V);
		if (g_nBlendPalette != -1)
		{
			float H2, S2, V2;
			GetHSV(g_nBlendPalette, intensity, (float)now, H2, S2, V2);
			BlendHSV(g_nBlendPaletteAmount, H2, S2, V2, H, S, V);
		}
		HSV2RGB(H, S, V, outPalette[n]);
	}

	if (g_nBlendPalette != -1)
	{
		g_nBlendPaletteAmount += (float)(now - blendStart) / 2.0f;
		if (g_nBlendPaletteAmount >= 1.0f)
			g_nBlendPalette = -1;
	}
}

// ===== Line Drawing =========================================================
// Bresenham stepping with an optional round pen. Nothing in the dash ever
// raises mLineWidth above 1, but the wide pen is there and kept working.

// Pen insets per row for widths 2..11, from the byte tables at 0x0002a310..0x0002a364.
static const char* PenShape(int width)
{
	static const char w2[] = { 0, 0 };
	static const char w3[] = { 1, 0, 1 };
	static const char w4[] = { 1, 0, 0, 1 };
	static const char w5[] = { 1, 0, 0, 0, 1 };
	static const char w6[] = { 1, 0, 0, 0, 0, 1 };
	static const char w7[] = { 2, 1, 0, 0, 0, 1, 2 };
	static const char w8[] = { 2, 1, 0, 0, 0, 0, 1, 2 };
	static const char w9[] = { 3, 1, 1, 0, 0, 0, 1, 1, 3 };
	static const char w10[] = { 3, 1, 1, 0, 0, 0, 0, 1, 1, 3 };
	static const char w11[] = { 4, 2, 1, 1, 0, 0, 0, 1, 1, 2, 4 };
	static const char* const shapes[] = { w2, w3, w4, w5, w6, w7, w8, w9, w10, w11 };
	return shapes[width - 2];
}

// Keeps bits 0..29 and copies the sign into bits 30 and 31, so a wild
// coordinate keeps its side of the surface without overflowing the math below.
static inline int FoldCoord(int v)
{
	return (v & 0x3FFFFFFF) | ((v >> 1) & (int)0xC0000000);
}

// CSurfx::Line(sx, sy, ex, ey, color)
// Reconstructed from 5960 retail XBE @ 0x0005fdf0. Behavior:
//   - Pens wider than 3 shrink toward 128/128 on axis lines and 183/128 on
//     diagonals: w = (lineWidth * (128 + 55 * minor^2 / major^2) + 64) >> 7.
//   - If the start sits outside the clip rect (inset by half the pen) the ends
//     are swapped; if both do, nothing is drawn. Step counts are trimmed so the
//     walk stops at the clip edge.
//   - Width 1 plots single pixels. Wider pens stamp a round cap at the start
//     (insets from the 0x2a310 tables below 12, halfW - sqrt(halfW^2 - k^2) at
//     12 and up) and then lay a w pixel span across the minor axis per step.
// The stepping itself is the textbook integer Bresenham walk; see
// docs/decomp/TmapSystem.md for the full breakdown.
void CSurfx::Line(int sx, int sy, int ex, int ey, unsigned char color)
{
	sx = FoldCoord(sx);
	sy = FoldCoord(sy);
	ex = FoldCoord(ex);
	ey = FoldCoord(ey);

	long w = mLineWidth;
	if (mLineWidth > 3)
	{
		// 64 bit squares: the folded coordinates can reach 2^30.
		long long dx2 = (long long)(ex - sx) * (ex - sx);
		long long dy2 = (long long)(ey - sy) * (ey - sy);
		if (dx2 > 0 && dx2 >= dy2)
			w = (long)(128 + 55 * dy2 / dx2);
		else if (dy2 > 0 && dy2 > dx2)
			w = (long)(128 + 55 * dx2 / dy2);
		if (dx2 > 0 || dy2 > 0)
			w = (mLineWidth * w + 64) >> 7;
	}
	long half = w >> 1;

	long left = mClipRect.left + half;
	long top = mClipRect.top + half;
	long right = mClipRect.right - half;
	long bottom = mClipRect.bottom - half;

	if (sx < left || sx >= right || sy < top || sy >= bottom)
	{
		if (ex < left || ex >= right || ey < top || ey >= bottom)
			return;
		int t = sx; sx = ex; ex = t;
		t = sy; sy = ey; ey = t;
	}

	// Major/minor run lengths, step directions, and how many steps fit on screen.
	long dx = ex - sx, dy = ey - sy;
	long xStep, xLeft, rowStep, yLeft;

	if (dx < 0)
	{
		dx = -dx;
		xStep = -1;
		xLeft = (sx - dx < left) ? sx - left : dx;
	}
	else if (dx > 0)
	{
		xStep = 1;
		xLeft = (sx + dx >= right) ? right - 1 - sx : dx;
	}
	else
	{
		xStep = 0;
		xLeft = 0;
	}

	if (dy < 0)
	{
		dy = -dy;
		rowStep = -mBytesPerRow;
		yLeft = (sy - dy < top) ? sy - top : dy;
	}
	else
	{
		rowStep = mBytesPerRow;
		yLeft = (sy + dy >= bottom) ? bottom - 1 - sy : dy;
	}

	unsigned char* p = m_pels + sy * mBytesPerRow + sx;
	long err = 0;

	if (w > 1)
	{
		long pen = mLineWidth;
		long penHalf = pen >> 1;
		const char* shape = pen < 12 ? PenShape(pen) : NULL;

		for (long row = 0; row < pen; row++)
		{
			long k = row - penHalf;
			long inset = shape ? shape[row] : penHalf - (long)sqrt((double)(penHalf * penHalf - k * k));
			if (inset < pen - inset)
				memset(p + k * mBytesPerRow + inset - penHalf, color, pen - 2 * inset);
		}

		if (dx > dy)
		{
			for (; xLeft >= 0 && yLeft >= 0; xLeft--)
			{
				unsigned char* q = p - half * mBytesPerRow;
				for (long i = 0; i < w; i++, q += mBytesPerRow)
					*q = color;

				p += xStep;
				err += dy;
				if (err >= dx)
				{
					err -= dx;
					p += rowStep;
					yLeft--;
				}
			}
		}
		else
		{
			for (; yLeft >= 0 && xLeft >= 0; yLeft--)
			{
				memset(p - half, color, w);

				p += rowStep;
				err += dx;
				if (err >= dy)
				{
					err -= dy;
					p += xStep;
					xLeft--;
				}
			}
		}
		return;
	}

	if (dx >= dy)
	{
		for (; xLeft >= 0 && yLeft >= 0; xLeft--)
		{
			*p = color;
			p += xStep;
			err += dy;
			if (err >= dx)
			{
				err -= dx;
				p += rowStep;
				yLeft--;
			}
		}
	}
	else
	{
		for (; yLeft >= 0 && xLeft >= 0; yLeft--)
		{
			*p = color;
			p += rowStep;
			err += dx;
			if (err >= dy)
			{
				err -= dy;
				p += xStep;
				xLeft--;
			}
		}
	}
}


// ===== DeltaField ===========================================================
// Builds the per-pixel "pull from here" map that CImageFader warps the
// surface with. 24 warp styles, each a small formula in polar or x/y space.

#define DELTA_ZERO	0x7E07E0	// both offsets at the bias, so "stay put"

// DeltaField::DeltaField()
// Reconstructed from the inline construction in CImageFader_RenderDynamicTexture @ 0x0005e120:
//     style = 0; currentY = -1; width = height = 0; field = {negY 0, buffer NULL}
DeltaField::DeltaField()
{
	m_nStyle = 0;
	mCurrentY = -1;
	mWidth = 0;
	mHeight = 0;
}

DeltaFieldData::DeltaFieldData()
{
	mNegYExtents = 0;
	mField = NULL;
}

// DeltaFieldData::~DeltaFieldData()
// Reconstructed from 5960 retail XBE @ 0x0005e110: free(field).
DeltaFieldData::~DeltaFieldData()
{
	delete [] mField;
}

// DeltaField::GetField()
// Reconstructed from the tail of CImageFader_RenderDynamicTexture @ 0x0005e120:
//     if (currentY >= 0 && currentY != height) do CalcSome(); while (currentY != height);
// Finishes the field on demand. Returning NULL for a field that was never sized
// is a Theseus guard; the binary never reaches here in that state.
DeltaFieldData* DeltaField::GetField()
{
	if (mCurrentY < 0)
		return NULL;

	while (mCurrentY != mHeight)
		CalcSome();

	return &mFieldData;
}

// DeltaField::Assign()
// Reconstructed from 5960 retail XBE @ 0x0005e4e0:
//     aspect1to1 = 1; polar = hasR = hasTheta = 1; then the SetSize body, forced.
void DeltaField::Assign()
{
	mAspect1to1 = true;
	mPolar = true;
	mHasRTerm = true;
	mHasThetaTerm = true;
	SetSize(mWidth, mHeight, true);
}

// DeltaField::SetSize(w, h, force)
// Reconstructed from 5960 retail XBE, inlined at 0x0005e4e0 and twice in
// CImageFader_RenderDynamicTexture @ 0x0005e120:
//     if (w != width || h != height || force) {
//         width = w; height = h; free(buf); buf = new[w * h * 3 + 0x40];
//         xs = 2.0 / w; ys = 2.0 / h; if (aspect1to1) xs = ys = max(xs, ys);
//         ys *= -1.0; currentY = 0; negY = 0; }
// The y scale is flipped so +y points up in the warp formulas.
void DeltaField::SetSize(long inWidth, long inHeight, bool inForceRegen)
{
	if (inWidth == mWidth && inHeight == mHeight && !inForceRegen)
		return;

	mWidth = inWidth;
	mHeight = inHeight;

	delete [] mFieldData.mField;
	mFieldData.mField = new char [mWidth * mHeight * 3 + 64];

	mXScale = 2.0f / (float)mWidth;
	mYScale = 2.0f / (float)mHeight;
	if (mAspect1to1)
	{
		if (mYScale > mXScale)
			mXScale = mYScale;
		else
			mYScale = mXScale;
	}
	mYScale *= -1.0f;

	mCurrentY = 0;
	mNegYExtents = 0;
}

static inline void WriteDelta(char* cell, unsigned long code)
{
	cell[0] = (char)code;
	cell[1] = (char)(code >> 8);
	cell[2] = (char)(code >> 16);
}

// DeltaField::CalcSome()
// Reconstructed from 5960 retail XBE @ 0x0005e570. Computes one row per call
// (two when it starts on the top border row) and returns rows still to go.
// Border rows and the first and last cell of every row are DELTA_ZERO. For an
// inner cell the pixel position maps to x, y in -1..1 (aspect corrected),
// r and theta are filled in when the style needs them, GetXY gives the source
// point (converted from polar when it says so), and the offset in 1/32 pixel
// steps is clamped so the pull stays inside the image. The most negative y
// offset sizes the fade's ring buffer: negY = 1 - (minDy >> 5).
// See docs/decomp/TmapSystem.md.
int DeltaField::CalcSome()
{
	char* cell = mFieldData.mField + mCurrentY * mWidth * 3;

	if (mCurrentY == 0 || mCurrentY == mHeight - 1)
	{
		for (long x = 0; x < mWidth; x++, cell += 3)
			WriteDelta(cell, DELTA_ZERO);
		mCurrentY++;
	}

	if (mCurrentY > 0 && mCurrentY < mHeight - 1)
	{
		mY_Cord = 0.5f * mYScale * (mHeight - 2 * mCurrentY);
		float toFixedX = 32.0f / mXScale;
		float toFixedY = 32.0f / mYScale;

		WriteDelta(cell, DELTA_ZERO);
		cell += 3;

		for (long px = 1; px < mWidth - 1; px++, cell += 3)
		{
			mX_Cord = 0.5f * mXScale * (2 * px - mWidth);
			if (mHasRTerm)
				mR_Cord = sqrtf(mX_Cord * mX_Cord + mY_Cord * mY_Cord);
			if (mHasThetaTerm)
				mT_Cord = atan2f(mY_Cord, mX_Cord);

			float srcX, srcY;
			GetXY(mR_Cord, mT_Cord, mX_Cord, mY_Cord, srcX, srcY, mPolar, px == 1 && mCurrentY == 1);
			if (mPolar)
			{
				float radius = srcX;
				srcX = radius * cosf(srcY);
				srcY = radius * sinf(srcY);
			}

			long dx = (long)(toFixedX * (srcX - mX_Cord));
			long dy = (long)(toFixedY * (mY_Cord - srcY));

			long landX = px + (dx >> 5);
			if (landX >= mWidth - 1)
				dx = (mWidth - 1 - px) * 32;
			else if (landX < 0)
				dx = -px * 32;

			long landY = mCurrentY + (dy >> 5);
			if (landY >= mHeight - 1)
				dy = (mHeight - 1 - mCurrentY) * 32;
			else if (landY < 0)
				dy = -mCurrentY * 32;

			if (dy < mNegYExtents)
				mNegYExtents = dy;

			WriteDelta(cell, ((dx + 0x7E0) << 12) | (dy + 0x7E0));
		}

		WriteDelta(cell, DELTA_ZERO);
		mCurrentY++;
	}

	if (mCurrentY == mHeight)
		mFieldData.mNegYExtents = 1 - (mNegYExtents >> 5);

	return mHeight - mCurrentY;
}

// DeltaField::GetXY(r, theta, x, y, X, Y, bPolar, bInit)
// Reconstructed from 5960 retail XBE @ 0x0005e7d0 (a 24 way switch). Each
// style returns a source point, as (radius, angle) unless it clears bPolar.
// bInit is true on the first inner cell of a field; the random styles roll
// their shared parameters A0..A2 (globals 0x00188f60/5c/58) there. Where the
// binary multiplies rand() by a folded constant such as 0x0002bff0 =
// 16/32767, that is written here as rnd(16). A few styles had their constant
// products folded (0.5 * 0.06 -> 0.03 at 0x0002bff8, 0.5 * 0.1 -> 0.05); the
// product is kept spelled out, which can differ from the folded value in the
// last float bit. Out of range styles snap back to 0. The formulas are the
// binary's; constants are listed per style in docs/decomp/TmapSystem.md.
void DeltaField::GetXY(float r, float theta, float x, float y, float& X, float& Y, bool& bPolar, bool bInit)
{
	static float A0, A1, A2;
	const float pi = 3.141592653f;

	bPolar = true;

	switch (m_nStyle)
	{
	case 1:
		X = r * 0.87f;
		Y = theta - 0.075f;
		return;

	case 2:
		if (bInit)
			A0 = trunc(8.0f * rnd(5.0f)) * pi;
		X = ((1.0f + sinf(A0 * theta)) * 0.5f * 0.06f + 0.92f) * r;
		Y = theta;
		return;

	case 3:
		if (bInit)
		{
			A0 = 2.0f + rnd(16.0f);
			A1 = 0.01f + rnd(0.05f);
			A2 = 0.002f + rnd(0.006f);
		}
		X = pos(r - A2);
		Y = theta + A1 * sinf(A0 * r);
		return;

	case 4:
		X = r * 0.87f;
		Y = theta - 0.009f;
		return;

	case 5:
		X = r * 0.8f;
		Y = theta - 0.008f;
		return;

	case 6:
		if (bInit)
		{
			A0 = 2.0f + rnd(16.0f);
			A1 = 0.01f + rnd(0.05f);
		}
		X = x + A1 * sinf(A0 * y);
		Y = y + A1 * sinf(A0 * x);
		bPolar = false;
		return;

	case 7:
		X = r * 1.01f;
		Y = theta + 0.021f;
		return;

	case 8:
		X = r - 0.01f;
		Y = theta;
		return;

	case 9:
	case 10:
		if (bInit)
		{
			A0 = rnd(0.0045f);
			A1 = 0.1f + rnd(0.06f);
			A2 = 0.003f + (m_nStyle == 9 ? rnd(0.005f) : rnd(0.006f));
		}
		X = (0.99f - A1 * powf(fabsf(m_nStyle == 9 ? x : y), 1.3f)) * r - rnd(A0);
		Y = m_nStyle == 9 ? theta - A2 : theta + A2;
		return;

	case 11:
		X = r - 0.18f * x * y;
		Y = theta - 0.005f;
		return;

	case 12:
		X = r * 0.96f;
		Y = theta - 0.021f;
		return;

	case 13:
		X = x + rnd(0.01f) - 0.005f;
		Y = y - rnd(0.01f) - 0.005f - (y - 1.3f) * 0.04f;
		bPolar = false;
		return;

	case 14:
		if (bInit)
			A0 = 0.95f + rnd(0.06f);
		X = r * A0;
		Y = theta - pos(1.0f - r) * 0.035f;
		return;

	case 15:
		X = r * 0.94f;
		Y = theta - 0.007f;
		return;

	case 16:
		X = 0.9f * x + rnd(0.008f) - 0.004f;
		Y = 0.9f * y + rnd(0.008f) - 0.004f;
		bPolar = false;
		return;

	case 17:
		X = x + 0.1f * (rnd(2.0f) - 1.0f);
		Y = y + 0.1f * (rnd(2.0f) - 1.0f);
		bPolar = false;
		return;

	case 18:
		X = r * 0.99f;
		Y = theta + 0.009f;
		return;

	case 19:
		X = (0.92f + rnd(0.05f)) * r;
		Y = theta + 0.003f;
		return;

	case 20:
		X = r + 0.04f * sinf(6.2831853f * r);
		Y = theta + 0.015f;
		return;

	case 21:
		X = r * (0.87f + 0.05f * (1.0f + sinf(r * 15.0f)));
		Y = theta;
		return;

	case 22:
		if (bInit)
		{
			A0 = rnd(0.1f);
			A1 = 3.0f + rnd(7.0f);
			A2 = 0.9f + rnd(0.3f);
		}
		X = r * (1.0f + 0.13f * (r - A2));
		Y = theta + A0 * sinf(A1 * r);
		return;

	case 23:
		if (bInit)
			A0 = trunc(1.0f + rnd(2.1f)) * pi;
		X = ((1.0f + cosf(A0 * theta)) * 0.5f * 0.1f + 0.89f) * r;
		Y = theta + 0.005f;
		return;

	default:
		m_nStyle = 0;
		// fall through
	case 0:
		X = r * (1.0f + 0.16f * atanf(0.55f - r));
		Y = theta - 0.01f;
		return;
	}
}


// ===== CPalette =============================================================
// Palette node: one of the 10 recipes, optionally cycling on a timer.

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

// CPalette::CPalette()
// Reconstructed from CPalette_CreateNode @ 0x0005d6a0 (0x448 bytes):
//     type = changePeriod = randomness = 0; timeToBlend = 2.0; d3dPalette = NULL;
//     blendPalette = -1; nextChange = 0.0
CPalette::CPalette() :
	m_type(0),
	m_changePeriod(0.0f),
	m_changePeriodRandomness(0.0f),
	m_timeToBlend(2.0f)
{
	m_nBlendPalette = -1;
	m_nextPaletteChangeTime = 0.0f;
#ifdef _XBOX
	m_pPalette = NULL;
#endif
}

// CPalette::~CPalette()
// Reconstructed from 5960 retail XBE @ 0x0005d740: releases the D3D palette
// (the binary inlines D3DResource_Release) before the CNode teardown.
CPalette::~CPalette()
{
#ifdef _XBOX
	if (m_pPalette != NULL)
		m_pPalette->Release();
#endif
}

// CPalette::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0005d7d0. Ghidra decompile:
//     CNode_Advance(); now = DAT_0017e808;
//     if (0.0 < changePeriod && nextChange < now) {
//         nextChange = rnd(randomness) + changePeriod + now;
//         blendPalette = type; type++; blendAmount = 0; blendStart = now; }
//     Update();
//     if (blendPalette != -1 && (blendAmount = (now - blendStart) / timeToBlend) >= 1.0) blendPalette = -1;
void CPalette::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	XTIME now = TheseusGetNow();
	if (m_changePeriod > 0.0f && now > m_nextPaletteChangeTime)
	{
		m_nextPaletteChangeTime = now + m_changePeriod + rnd(m_changePeriodRandomness);
		m_nBlendPalette = m_type;
		m_type++;
		m_nBlendPaletteAmount = 0.0f;
		m_startBlendTime = now;
	}

	Update();

	if (m_nBlendPalette != -1)
	{
		m_nBlendPaletteAmount = (float)(now - m_startBlendTime) / m_timeToBlend;
		if (m_nBlendPaletteAmount >= 1.0f)
			m_nBlendPalette = -1;
	}
}

// CPalette::Update()
// Reconstructed from 5960 retail XBE @ 0x0005d8b0. Fills the 256 entries from
// GetHSV (crossfading from the old recipe while a blend runs) and copies
// g_nPalette back into type after each lookup, so an out of range type wraps
// to 0. Then copies the table into a D3D palette with alpha forced to 0xff,
// creating it on first use (the binary inlines CreatePalette and Lock).
// The palette upload is Xbox only; on desktop the texture upload in
// CDynamicTexture::Update applies the table in software instead.
void CPalette::Update()
{
	float now = (float)TheseusGetNow();
	float intensity = 0.0f;

	for (int n = 0; n < 256; n++, intensity += 1.0f / 255.0f)
	{
		float H, S, V;
		GetHSV(m_type, intensity, now, H, S, V);
		m_type = g_nPalette;

		if (m_nBlendPalette != -1)
		{
			float H2, S2, V2;
			GetHSV(m_nBlendPalette, intensity, now, H2, S2, V2);
			BlendHSV(m_nBlendPaletteAmount, H2, S2, V2, H, S, V);
		}

		HSV2RGB(H, S, V, m_palette[n]);
	}

#ifdef _XBOX
	if (m_pPalette == NULL)
		VERIFYHR(TheseusGetD3DDev()->CreatePalette(D3DPALETTE_256, &m_pPalette));

	D3DCOLOR* entries;
	VERIFYHR(m_pPalette->Lock(&entries, D3DLOCK_NOOVERWRITE));
	for (int n = 0; n < 256; n++)
		entries[n] = 0xFF000000 | (m_palette[n] & 0x00FFFFFF);
	VERIFYHR(m_pPalette->Unlock());
#endif
}

// CPalette::RenderDynamicTexture(pSurfx)
// Reconstructed from 5960 retail XBE @ 0x0005da80: fills row y with
// y * 256 / height, a top to bottom ramp through the whole palette.
void CPalette::RenderDynamicTexture(CSurfx* pSurfx)
{
	for (int y = 0; y < pSurfx->m_nHeight; y++)
		memset(pSurfx->m_pels + y * pSurfx->m_nWidth, (y * 256) / pSurfx->m_nHeight, pSurfx->m_nWidth);
}


// ===== CDynamicTexture ======================================================
// A texture node whose children draw into an 8-bit surface each frame.

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

// CDynamicTexture::CDynamicTexture()
// Reconstructed from 5960 retail XBE @ 0x0005db30:
//     format = 0xb (P8); size = 0x100; erase = 1; fps = 15.0 (0x0002bf68);
//     palette = NULL; surfx = NULL; lastUpdate = 0.0
CDynamicTexture::CDynamicTexture() :
	m_size(256),
	m_erase(true),
	m_fps(15.0f),
	m_palette(NULL)
{
	m_format = D3DFMT_P8;
	m_pSurfx = NULL;
	m_lastUpdateTime = 0.0f;
}

// CDynamicTexture::~CDynamicTexture()
// Reconstructed from 5960 retail XBE @ 0x0005dbc0: palette->Release(), then
// the surface (CSurfx teardown inlined), then the children and CTexture.
CDynamicTexture::~CDynamicTexture()
{
	if (m_palette != NULL)
		m_palette->Release();
	delete m_pSurfx;
}

// CDynamicTexture::Create(nWidth, nHeight)
// Reconstructed from 5960 retail XBE @ 0x0005dc70. Ghidra decompile:
//     imageW = imageH = 0x200;
//     if (D3DXCreateTexture(dev, 0x200, 0x200, 1, 0, format, 1, &surface) < 0) return 0;
//     LockRect; memset(bits, 0, imageW * imageH); Unlock; imageW, imageH = surface desc; return 1;
// Always 512x512 whatever size asks for; the effect only fills the top left corner.
bool CDynamicTexture::Create(int nWidth, int nHeight)
{
	m_nImageWidth = 512;
	m_nImageHeight = 512;
	if (FAILED(D3DXCreateTexture(TheseusGetD3DDev(), m_nImageWidth, m_nImageHeight, 1, 0, m_format, D3DPOOL_MANAGED, &m_surface)))
		return false;

	D3DLOCKED_RECT lr;
	m_surface->LockRect(0, &lr, NULL, 0);
	memset(lr.pBits, 0, m_nImageWidth * m_nImageHeight);
	m_surface->UnlockRect(0);

	TheseusGetTextureSize(m_surface, m_nImageWidth, m_nImageHeight);
	return true;
}

// CDynamicTexture::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x0005dd20: CNode::Advance, then the
// palette node, then every child.
void CDynamicTexture::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);

	if (m_palette != NULL)
		m_palette->Advance(nSeconds);

	for (int n = 0; n < m_children.GetLength(); n++)
		m_children.GetNode(n)->Advance(nSeconds);
}

// CDynamicTexture::GetTextureSurface()
// Reconstructed from 5960 retail XBE @ 0x0005dd70. Redraws at most fps times a
// second, then, if the palette node is a Palette (the binary walks the class
// chain), binds its D3D palette to stage 0. Binding is Xbox only; desktop
// applies the palette while uploading in Update.
LPDIRECT3DTEXTURE8 CDynamicTexture::GetTextureSurface()
{
	if (TheseusGetNow() > m_lastUpdateTime + 1.0f / m_fps)
	{
		m_lastUpdateTime = TheseusGetNow();
		Update();
	}

#ifdef _XBOX
	if (m_palette != NULL && m_palette->IsKindOf(NODE_CLASS(CPalette)))
		VERIFYHR(TheseusGetD3DDev()->SetPalette(0, ((CPalette*)m_palette)->m_pPalette));
#endif

	return m_surface;
}

// CDynamicTexture::Update()
// Reconstructed from 5960 retail XBE @ 0x0005dde0. Creates the texture and the
// size x size CSurfx on first use, clears the surface when erase is set, lets
// each child draw into it, picks the palette (the node's, else the default from
// MakePalette), and uploads: on Xbox the 8-bit pixels go straight into the P8
// texture through the Swizzler (inlined in the binary), so the GPU applies the
// palette. The desktop upload below is Theseus's own: palette expansion to
// RGBA in software, plus the projectM and album cover modes.
void CDynamicTexture::Update()
{
	if (m_surface == NULL && m_size > 0 && !Create(m_size, m_size))
		return;

	if (m_pSurfx == NULL && m_size > 0)
		m_pSurfx = new CSurfx(m_size, m_size);

	if (m_surface == NULL || m_pSurfx == NULL || TheseusGetNow() < m_lastUpdateTime)
		return;

	if (m_erase)
		memset(m_pSurfx->m_pels, 0, m_pSurfx->m_nWidth * m_pSurfx->m_nHeight);

	for (int n = 0; n < m_children.GetLength(); n++)
		m_children.GetNode(n)->RenderDynamicTexture(m_pSurfx);

	const DWORD* rgpe = m_palette != NULL ? m_palette->GetPalette() : NULL;
	DWORD rgpeDefault [256];
	if (rgpe == NULL)
	{
		MakePalette(rgpeDefault);
		rgpe = rgpeDefault;
	}

	{
#ifdef _XBOX
		(void)rgpe;
		Swizzler swz(m_nImageWidth, m_nImageHeight, 1);
		D3DLOCKED_RECT lr;
		VERIFYHR(m_surface->LockRect(0, &lr, NULL, D3DLOCK_DISCARD));
		BYTE* dst = (BYTE*)lr.pBits;
		const BYTE* src = m_pSurfx->m_pels;
		for (int y = 0; y < m_size; y++, src += m_size)
		{
			for (int x = 0; x < m_size; x++, swz.IncU())
				dst[swz.Get2D()] = src[x];
			swz.IncV();
			swz.AddU(swz.SwizzleU(m_nImageWidth - m_size));
		}
		VERIFYHR(m_surface->UnlockRect(0));
#else
		D3DLOCKED_RECT lr;
		VERIFYHR(m_surface->LockRect(0, &lr, NULL, D3DLOCK_DISCARD));
		uint32_t* pBase = (uint32_t*)lr.pBits;
		// Texture is m_nImageWidth wide but we only fill m_size; respect
		// the row stride or the data ends up smeared diagonally.
		int dstStridePx = (lr.Pitch > 0) ? (lr.Pitch / 4) : m_nImageWidth;

		extern bool g_useMilkdropViz;
		extern bool g_showAlbumCover;
		bool hasAudioViz = false;
		if (g_useMilkdropViz || g_showAlbumCover) {
			int nChildCount2 = m_children.GetLength();
			for (int ci = 0; ci < nChildCount2; ci++) {
				CNode* c = m_children.GetNode(ci);
				if (!c) continue;
				CNodeClass* nc = c->GetNodeClass();
				if (nc && nc->m_className &&
					strcmp(nc->m_className, "AudioVisualizer") == 0)
				{
					hasAudioViz = true;
					break;
				}
			}
		}

		int pmW = 0, pmH = 0;
		const unsigned char* pmSrc = (hasAudioViz && g_useMilkdropViz)
			? MilkdropWindow_GetReadbackRGBA(&pmW, &pmH) : NULL;

		const unsigned char* albumPx = NULL;
		int albumW = 0, albumH = 0;
		if (g_showAlbumCover && hasAudioViz && !g_useMilkdropViz) {
			int idx = DashMusic_GetCurrentSoundtrackIdx();
			if (idx >= 0)
				albumPx = DashMusic_GetAlbumRGBA(idx, &albumW, &albumH);
		}

		if (g_useMilkdropViz && hasAudioViz)
		{
			if (pmSrc && pmW > 0 && pmH > 0) {
				for (int dy = 0; dy < m_size; dy++) {
					int sy = (dy * pmH) / m_size;
					const unsigned char* row = pmSrc + sy * pmW * 4;
					uint32_t* dstRow = pBase + dy * dstStridePx;
					for (int dx = 0; dx < m_size; dx++) {
						int sx = (dx * pmW) / m_size;
						const unsigned char* p = row + sx * 4;
						dstRow[dx] =  (uint32_t)p[0]
						           | ((uint32_t)p[1] <<  8)
						           | ((uint32_t)p[2] << 16)
						           | 0xff000000u;
					}
				}
			} else {
				// FRAME-DEBUG: cycle the texture's solid color so we can
				// see whether per-frame uploads actually reach the GPU.
				// Toggle should be ON but projectM window NOT open to hit this.
				extern double TheseusGetNow();
				float t = (float)TheseusGetNow();
				uint8_t r = (uint8_t)(127.5f + 127.5f * sinf(t * 2.0f));
				uint8_t g = (uint8_t)(127.5f + 127.5f * sinf(t * 2.0f + 2.094f));
				uint8_t b = (uint8_t)(127.5f + 127.5f * sinf(t * 2.0f + 4.188f));
				uint32_t v = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xff000000u;
				for (int dy = 0; dy < m_size; dy++) {
					uint32_t* dstRow = pBase + dy * dstStridePx;
					for (int dx = 0; dx < m_size; dx++)
						dstRow[dx] = v;
				}
			}
		}
		else if (albumPx && albumW > 0 && albumH > 0)
		{
			for (int dy = 0; dy < m_size; dy++) {
				int sy = (dy * albumH) / m_size;
				const unsigned char* row = albumPx + sy * albumW * 4;
				uint32_t* dstRow = pBase + dy * dstStridePx;
				for (int dx = 0; dx < m_size; dx++) {
					int sx = (dx * albumW) / m_size;
					const unsigned char* p = row + sx * 4;
					dstRow[dx] =  (uint32_t)p[0]
					           | ((uint32_t)p[1] <<  8)
					           | ((uint32_t)p[2] << 16)
					           | 0xff000000u;
				}
			}
		}
		else
		{
			uint8_t* pbSrc = (uint8_t*)m_pSurfx->m_pels;
			for (int dy = 0; dy < m_size; dy++) {
				uint32_t* dstRow = pBase + dy * dstStridePx;
				for (int dx = 0; dx < m_size; dx++) {
					uint8_t b = *pbSrc++;
					// rgpe[] is D3DCOLOR_ARGB (0xAARRGGBB); convert to
					// RGBA byte order for the bgfx-backed texture.
					DWORD argb = rgpe[b];
					dstRow[dx] = ((argb >> 16) & 0xff)
					           | (((argb >> 8) & 0xff) <<  8)
					           | (( argb       & 0xff) << 16)
					           | 0xff000000u;
				}
			}
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
// Xbox-only here. desktop has its own SDL_mixer-backed CAudioVisualizer
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

#endif // _XBOX. end CAudioVisualizer Xbox-only block


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
