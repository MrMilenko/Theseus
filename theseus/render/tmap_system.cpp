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
// Warps the surface through a DeltaField every frame, changing style on a timer.

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

// CImageFader::CImageFader()
// Reconstructed from CImageFader_CreateNode @ 0x0005e030 (0x38 bytes):
//     type = changePeriod = randomness = 0; field = nextField = NULL; nextChange = 0.0
CImageFader::CImageFader() :
	m_type(0),
	m_changePeriod(0.0f),
	m_changePeriodRandomness(0.0f)
{
	m_pDeltaField = NULL;
	m_pDeltaField2 = NULL;
	m_nextChangeTime = 0.0f;
}

// CImageFader::~CImageFader()
// Reconstructed from 5960 retail XBE @ 0x0005e0c0: frees both fields.
CImageFader::~CImageFader()
{
	delete m_pDeltaField;
	delete m_pDeltaField2;
}

// Inlined twice in 0x0005e120: new DeltaField, style, Assign, SetSize to the surface.
static DeltaField* NewDeltaField(int style, CSurfx* pSurfx)
{
	DeltaField* field = new DeltaField;
	field->m_nStyle = style;
	field->Assign();
	field->SetSize(pSurfx->m_nWidth, pSurfx->m_nHeight);
	return field;
}

// CImageFader::RenderDynamicTexture(pSurfx)
// Reconstructed from 5960 retail XBE @ 0x0005e120. Keeps the current warp
// field plus the next style's field building in the background:
//   - first frame: build the field for type and schedule the next change at
//     now + changePeriod + rnd(randomness);
//   - when changePeriod > 0 and that time comes: type++, next change at
//     now + changePeriod, and the background field becomes current;
//   - make sure the background field (style type + 1) exists;
//   - while the current field is unfinished, compute 10 rows of it, copy its
//     style back into type (it snaps bad styles to 0) and skip the warp;
//   - otherwise compute 5 rows of the background field and fade the surface
//     through the current one.
void CImageFader::RenderDynamicTexture(CSurfx* pSurfx)
{
	if (m_pDeltaField == NULL)
	{
		m_pDeltaField = NewDeltaField(m_type, pSurfx);
		m_nextChangeTime = TheseusGetNow() + m_changePeriod + rnd(m_changePeriodRandomness);
	}
	else if (m_changePeriod > 0.0f && TheseusGetNow() >= m_nextChangeTime)
	{
		m_type++;
		m_nextChangeTime = TheseusGetNow() + m_changePeriod;
		delete m_pDeltaField;
		m_pDeltaField = m_pDeltaField2;
		m_pDeltaField2 = NULL;
	}

	if (m_pDeltaField2 == NULL)
		m_pDeltaField2 = NewDeltaField(m_type + 1, pSurfx);

	if (m_pDeltaField->IsCalculating())
	{
		for (int n = 0; n < 10; n++)
			m_pDeltaField->CalcSome();
		m_type = m_pDeltaField->m_nStyle;
		return;
	}

	if (m_pDeltaField2->IsCalculating())
	{
		for (int n = 0; n < 5; n++)
			m_pDeltaField2->CalcSome();
	}

	pSurfx->Fade(m_pDeltaField->GetField());
}

// CImageFader::OnSetProperty(pprd, pvValue)
// Reconstructed from 5960 retail XBE @ 0x0005e3f0:
//     if (prd->offset == 0x1c /* type */) { delete field; field = nextField; nextField = NULL; }
//     return 1;
// The next frame rebuilds the background field for the new type.
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
// Draws the playing AudioClip into the surface as a scope, a spinner or a
// spectrum analyzer with a beat flash and an overlay of wave lines.
//
// Xbox only here. Desktop has its own CAudioVisualizer in
// desktop/desktop_nodes.cpp, fed from the SDL mixer's sample ring.
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

// CAudioVisualizer::CAudioVisualizer()
// Reconstructed from CAudioVisualizer_CreateNode @ 0x00063770 (0x834 bytes):
//     type = channel = source = NULL; scale = 1.0; offset = 0
CAudioVisualizer::CAudioVisualizer() :
	m_type(NULL),
	m_channel(NULL),
	m_source(NULL),
	m_scale(1.0f),
	m_offset(0.0f)
{
}

// CAudioVisualizer::~CAudioVisualizer()
// Reconstructed from 5960 retail XBE @ 0x00063800: free(type); free(channel); source->Release().
CAudioVisualizer::~CAudioVisualizer()
{
	delete [] m_type;
	delete [] m_channel;
	if (m_source != NULL)
		m_source->Release();
}

// CAudioVisualizer::Advance(nSeconds)
// Reconstructed from 5960 retail XBE @ 0x00063870: CNode::Advance, then the source.
void CAudioVisualizer::Advance(float nSeconds)
{
	CNode::Advance(nSeconds);
	if (m_source != NULL)
		m_source->Advance(nSeconds);
}

// CAudioVisualizer::CalcSpectrum(pcm, fft)
// Reconstructed from 5960 retail XBE @ 0x000638a0. Ghidra decompile:
//     if (!state) state = fft_init();  fft_perform(pcm, buf[129], state);
//     for (i = 0; i < 0x81; i++) fft[i] = (short)(sqrt(buf[i]) * (1/256.0));
// 129 bins go into a 128 entry array: the last lands in the next field, the
// same spill the binary has with this layout.
void CAudioVisualizer::CalcSpectrum(short* pcm, short* fft)
{
	static fft_state* state = NULL;
	float power [FFT_BUFFER_SIZE / 2 + 1];

	if (state == NULL)
		state = fft_init();
	fft_perform(pcm, power, state);

	for (int n = 0; n <= FFT_BUFFER_SIZE / 2; n++)
		fft[n] = (short)(sqrtf(power[n]) / FFT_BUFFER_SIZE);
}

// CAudioVisualizer::UpdateSpectrum()
// Reconstructed from 5960 retail XBE @ 0x00063900: once per frame, left then right.
void CAudioVisualizer::UpdateSpectrum()
{
	if (m_bFFTValid)
		return;
	CalcSpectrum(m_pcmLeft, m_fftLeft);
	CalcSpectrum(m_pcmRight, m_fftRight);
	m_bFFTValid = true;
}

// log(level) * 8 as a byte, 0 for silence (and NaN), 255 at the top.
static inline BYTE SpectrumByte(short level)
{
	float n = logf(level) * 8.0f;
	if (!(n > 0.0f))
		return 0;
	return n < 255.0f ? (BYTE)n : 255;
}

// CAudioVisualizer::RenderDynamicTexture(pSurfx)
// Reconstructed from 5960 retail XBE @ 0x00063940. Only draws while the source
// is a playing AudioClip with a sample buffer. Copies up to 256 stereo samples
// into the left/right arrays (zero padded) and marks the mono and spectrum
// caches stale. type picks the effect by first letter: none or other = line
// scope, 's' = spinner, 'c' = nothing, 'a' = analyzer. channel 'r'/'R' takes
// the right channel. Analyzer bars use 128 left bins mirrored into 128 right
// bins, falling peak dots (2 per frame, static array @ 0x00188f90), then the
// beat flash and the wave overlay. Details in docs/decomp/TmapSystem.md.
void CAudioVisualizer::RenderDynamicTexture(CSurfx* pSurfx)
{
	CAudioClip* clip = (CAudioClip*)m_source;
	if (clip == NULL || clip->GetNodeClass() != NODE_CLASS(CAudioClip) || clip->m_transportMode != TRANSPORT_PLAY)
		return;

	short* samples = (short*)clip->GetSampleBuffer();
	if (samples == NULL)
		return;

	int count = clip->GetSampleBufferSize() / 4;
	if (count <= 0)
		return;
	if (count > 256)
		count = 256;

	for (int n = 0; n < 256; n++)
	{
		m_pcmLeft[n] = n < count ? samples[n * 2] : 0;
		m_pcmRight[n] = n < count ? samples[n * 2 + 1] : 0;
	}
	m_bMonoValid = false;
	m_bFFTValid = false;

	if (count > pSurfx->m_nHeight)
		count = pSurfx->m_nHeight;

	int channel = 0;
	if (m_channel != NULL && (m_channel[0] == 'r' || m_channel[0] == 'R'))
		channel = 1;

	TCHAR kind = m_type != NULL ? m_type[0] : 0;
	int width = pSurfx->m_nWidth;
	int height = pSurfx->m_nHeight;
	float t = (float)TheseusGetNow();

	if (kind == 's' || kind == 'S')
	{
		int xc = width / 2;
		int yc = height / 2;
		float step = 1.0f / (float)width;
		float c = cosf(t * 0.2f);
		float s = sinf(t * 0.2f);
		float lastX = 0.0f, lastY = 0.0f;
		rand();	// the binary draws one value here and never uses it

		for (float pos = 0.0f; pos <= 1.0; pos += step)
		{
			float level = (float)samples[(int)(pos * count) * 2 + channel] / 32767.0f * m_scale + m_offset;
			float along = 2.1f * (pos - 0.5f);
			float x = c * along + s * level;
			float y = -c * level + s * along;

			if (pos != 0.0f)
				pSurfx->Line(xc + (int)(lastX * xc), yc + (int)(lastY * yc), xc + (int)(x * xc), yc + (int)(y * yc), 255);
			lastX = x;
			lastY = y;
		}
	}
	else if (kind == 'a' || kind == 'A')
	{
		static int peaks [256];
		BYTE bars [256];

		UpdateSpectrum();
		for (int n = 0; n < 128; n++)
			bars[n] = SpectrumByte(m_fftLeft[127 - n]);
		for (int n = 128; n < 256; n++)
			bars[n] = SpectrumByte(m_fftRight[n - 128]);

		for (int n = 0; n < 256; n++)
		{
			peaks[n] = peaks[n] > 2 ? peaks[n] - 2 : 0;
			if (peaks[n] < bars[n] * 2)
				peaks[n] = bars[n] * 2;
		}

		for (int x = 0; x < width; x++)
		{
			int bin = (x * 256) / width;
			float barHeight = ((float)bars[bin] * (float)height) / 256.0f;
			BYTE shade = 255;
			for (int y = height - (int)barHeight; y < height; y++)
				*pSurfx->Pixel(x, y) = shade--;

			float peakHeight = (float)peaks[bin] / 2.0f;
			peakHeight = (peakHeight * (float)height) / 256.0f;
			*pSurfx->Pixel(x, height - (int)peakHeight) = 255;
		}

		RenderEffect1(pSurfx);
		RenderEffect2(pSurfx);
	}
	else if (kind != 'c' && kind != 'C')
	{
		int xc = width / 2;
		int lastX = xc, lastY = 0;
		for (int y = 0; y < count; y++)
		{
			int x = xc + ((samples[y * 2 + channel] * width) >> 16);
			pSurfx->Line(lastX, lastY, x, y, 255);
			lastX = x;
			lastY = y;
		}
	}
}

// Beat detector state, globals @ 0x0017e6f4..0x0017e70c in the binary.
static struct
{
	int highNow, highLast, highTime;
	int lowNow, lowLast, lowTime;
	int flashing;
} s_beat;

// CAudioVisualizer::RenderEffect1(pSurfx)
// Reconstructed from 5960 retail XBE @ 0x00063ef0. Bass = average of the
// lowest 5 bins of each channel (each >> 4). Running high and low marks reset
// every 10 frames (frame count @ 0x00188f50) into "last" values. The surface
// flashes to 255 when bass passes 60% of the way from low to high, and rearms
// once it drops under 40%.
void CAudioVisualizer::RenderEffect1(CSurfx* pSurfx)
{
	static int frame = 0;

	int bass = 0;
	for (int n = 0; n < 5; n++)
		bass += (m_fftLeft[n] >> 4) + (m_fftRight[n] >> 4);
	bass /= 10;

	if (bass > s_beat.highNow)
		s_beat.highNow = bass;
	if (bass < s_beat.lowNow)
		s_beat.lowNow = bass;

	if (frame - s_beat.highTime > 10)
	{
		s_beat.highLast = s_beat.highNow;
		s_beat.highNow = 0;
		s_beat.highTime = frame;
	}
	if (frame - s_beat.lowTime > 10)
	{
		s_beat.lowLast = s_beat.lowNow;
		s_beat.lowNow = 0;
		s_beat.lowTime = frame;
	}

	if (!s_beat.flashing && bass > (s_beat.highLast * 6 + s_beat.lowLast * 4) / 10)
	{
		memset(pSurfx->m_pels, 255, pSurfx->m_nWidth * pSurfx->m_nHeight);
		s_beat.flashing = 1;
	}
	frame++;
	if (s_beat.flashing && bass < (s_beat.highLast * 4 + s_beat.lowLast * 6) / 10)
		s_beat.flashing = 0;
}

// Overlay settings, initialized globals @ 0x00176034..0x00176040 in the binary.
static int s_waveAmplitude = 50;
static int s_waveShift = 30;
static int s_waveMode = -1;
static BYTE s_waveColor = 128;

// Bounds checked 2x2 dot, inlined from 0x000640a0.
static void PlotDot(CSurfx* pSurfx, int x, int y, BYTE color)
{
	static const int offsets[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
	for (int n = 0; n < 4; n++)
	{
		int px = x + offsets[n][0], py = y + offsets[n][1];
		if (px >= 0 && py >= 0 && px < pSurfx->m_nWidth && py < pSurfx->m_nHeight)
			*pSurfx->Pixel(px, py) = color;
	}
}

// CAudioVisualizer::RenderEffect2(pSurfx)
// Reconstructed from 5960 retail XBE @ 0x00064130. Every 3 + rnd(5) seconds it
// moves to the next of 5 modes: one wave line, two lines (left and right),
// four lines (two across, two down), or the two channels bent around a half
// circle (mode 3 clips negative swings). Samples come from the high byte of
// every fifth slot across the width, 4 pixels per segment, scaled by
// height * 50 >> 12, offset height * 30 >> 8 from center. The half circle
// tables are cos/sin(i / width * pi + pi / 2), rebuilt when the width changes
// (globals @ 0x0017e774..0x0017e780). Last, two 64 point Lissajous trails
// (k / 80, k / (80 * 1.756) and k / 187.2, k / (154.4 * 1.756)) slowly
// rotating by k * 0.001, drawn as 2x2 dots and continuing from frame to
// frame (k @ 0x00188f40).
void CAudioVisualizer::RenderEffect2(CSurfx* pSurfx)
{
	static XTIME nextMode = 0.0f;
	static float* cosTable = NULL;
	static float* sinTable = NULL;
	static int cosWidth = 0, sinWidth = 0;
	static int curveStep = 0;

	const int width = pSurfx->m_nWidth;
	const int height = pSurfx->m_nHeight;
	const int segment = 4;
	const int shift = (s_waveShift * height) >> 8;
	const int midX = width >> 1;
	const int midY = height >> 1;

	float right = (float)((((m_pcmLeft[0] + m_pcmRight[0]) >> 9) * s_waveAmplitude * height) >> 12);
	float left = right;

	if (s_waveMode < 0 || TheseusGetNow() >= nextMode)
	{
		s_waveMode++;
		nextMode = TheseusGetNow() + 3.0f + rnd(5.0f);
	}
	if (s_waveMode > 4)
		s_waveMode = 0;

	if (cosWidth != width || sinWidth != width)
	{
		delete [] cosTable;
		delete [] sinTable;
		cosTable = sinTable = NULL;
		cosWidth = sinWidth = 0;
	}
	if (cosWidth == 0 || cosTable == NULL)
	{
		cosWidth = width;
		cosTable = new float [width];
		for (int n = 0; n < width; n += segment)
			cosTable[n] = cosf((float)n / width * D3DX_PI + (float)D3DX_PI / 2);
	}
	if (sinWidth == 0 || sinTable == NULL)
	{
		sinWidth = width;
		sinTable = new float [width];
		for (int n = 0; n < width; n += segment)
			sinTable[n] = sinf((float)n / width * D3DX_PI + (float)D3DX_PI / 2);
	}

	if (s_waveMode == 3)
	{
		if (right < 0) right = 0;
		if (left < 0) left = 0;
	}

	for (int n = segment; n < width; n += segment)
	{
		float lastRight = right, lastLeft = left;
		int slot = (n << 8) / width / 5;
		right = (float)(((m_pcmRight[slot] >> 8) * s_waveAmplitude * height) >> 12);
		left = (float)(((m_pcmLeft[slot] >> 8) * s_waveAmplitude * height) >> 12);

		switch (s_waveMode)
		{
		case 0:
			pSurfx->Line(n - segment, (int)(midY + shift + lastLeft), n, (int)(midY + shift + left), s_waveColor);
			break;

		case 1:
			pSurfx->Line(n - segment, (int)(midY + shift + lastRight), n, (int)(midY + shift + right), s_waveColor);
			pSurfx->Line(n - segment, (int)(midY - shift + lastLeft), n, (int)(midY - shift + left), s_waveColor);
			break;

		case 2:
			pSurfx->Line(n - segment, (int)(midY + shift + lastRight), n, (int)(midY + shift + right), s_waveColor);
			pSurfx->Line(n - segment, (int)(midY - shift + lastRight), n, (int)(midY - shift + right), s_waveColor);
			pSurfx->Line((int)(midX + shift + lastLeft), n - segment, (int)(midX + shift + left), n, s_waveColor);
			pSurfx->Line((int)(midX - shift + lastLeft), n - segment, (int)(midX - shift + left), n, s_waveColor);
			break;

		case 3:
			if (right < 0) right = 0;
			if (left < 0) left = 0;
			// fall through
		case 4:
			pSurfx->Line((int)(midX + cosTable[n - segment] * (shift + lastRight)), (int)(midY + sinTable[n - segment] * (shift + lastRight)),
				(int)(midX + cosTable[n] * (shift + right)), (int)(midY + sinTable[n] * (shift + right)), s_waveColor);
			pSurfx->Line((int)(midX - cosTable[n - segment] * (shift + lastLeft)), (int)(midY + sinTable[n - segment] * (shift + lastLeft)),
				(int)(midX - cosTable[n] * (shift + left)), (int)(midY + sinTable[n] * (shift + left)), s_waveColor);
			break;
		}
	}

	if (s_waveMode == 3 || s_waveMode == 4)
	{
		int last = width - segment;
		pSurfx->Line((int)(midX + cosTable[last] * (shift + right)), (int)(midY + sinTable[last] * (shift + right)),
			(int)(midX - cosTable[last] * (shift + left)), (int)(midY + sinTable[last] * (shift + left)), s_waveColor);
	}

	const float amplitude = 50.0f / 256;
	const float spin = 0.001f;
	const float base = 80.0f;
	int k = curveStep;
	for (int trail = 0; trail < 2; trail++)
	{
		k = curveStep;
		for (int n = 0; n < 64; n++, k++)
		{
			float cx = cosf((float)k / (base + base * trail * 1.34f)) * height * amplitude;
			float cy = sinf((float)k / (1.756f * (base + base * trail * 0.93f))) * height * amplitude;
			float a = (float)k * spin;
			PlotDot(pSurfx, (int)(cx * cosf(a) + cy * sinf(a) + width / 2), (int)(cx * sinf(a) - cy * cosf(a) + height / 2), 255);
		}
	}
	curveStep = k;
}

#endif // _XBOX


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
