// tmap_system.h: the 8 bit warp/fade surface behind the TMAP effects and
// the delta fields that drive it. Bodies in render/tmap_system.cpp, binary
// notes in docs/decomp/TmapSystem.md.

#pragma once

// A finished delta field: how many rows above a pixel can pull from, and the
// 3 byte cells (see CSurfx::Fade).
class DeltaFieldData
{
public:
	DeltaFieldData();
	~DeltaFieldData();

	long mNegYExtents;
	char* mField;
};

// 8-bit palettized drawing surface for the TMAP effects. Layout follows the
// 5960 object at 0x0005f3f0.
class CSurfx
{
public:
	CSurfx(int w, int h);
	~CSurfx();

	BYTE* Pixel(int x, int y) { return m_pels + (y * mBytesPerRow + x); }

	void Line(int x, int y, int x2, int y2, unsigned char c);

	// Whole surface, one block, or the raw rows; all end in the 3 byte cell blend.
	void Fade(DeltaFieldData* grad);
	void Fade(char* pix, long stride, long x, long y, DeltaFieldData* grad);
	void Fade(const char* src, long srcStride, char* dst, long dstStride, long x, long y, const char* grad);

	// Live surface count and the fade scratch buffer they share.
	static int sTempRef;
	static long sTempSize;
	static char* sTemp;

	int m_nWidth;
	int m_nHeight;
	BYTE* m_pels;
	int mBytesPerRow;
	int mLineWidth;
	RECT mClipRect;

private:
	// Owns its pixels, so no copies.
	CSurfx(const CSurfx&);
	CSurfx& operator=(const CSurfx&);
};

// Builds a warp field a few rows at a time so a style change doesn't stall a frame.
class DeltaField
{
public:
	DeltaField();

	// Resets the flags and restarts the field at the current size.
	void Assign();

	// Restarts the field if the size changed (or when forced).
	void SetSize(long w, long h, bool force = false);

	// Computes the next row; returns how many are left.
	int CalcSome();

	// mCurrentY is -1 before sizing, counts up while building, ends at mHeight.
	bool IsUninitialized() { return mCurrentY == -1; }
	bool IsCalculating() { return mCurrentY >= 0 && mCurrentY != mHeight; }
	bool IsCalculated() { return mCurrentY == mHeight; }

	// The field, finished first if it wasn't; NULL if it was never sized.
	DeltaFieldData* GetField();

	// Style formula for one pixel, in cartesian or polar terms.
	void GetXY(float r, float th, float x, float y, float& outX, float& outY, bool& polar, bool init);

	int m_nStyle;

protected:
	long mCurrentY;
	long mNegYExtents;
	float mX_Cord, mY_Cord, mR_Cord, mT_Cord;
	float mXScale, mYScale;
	bool mPolar, mHasRTerm, mHasThetaTerm;
	long mWidth, mHeight;
	long mAspect1to1;
	DeltaFieldData mFieldData;
};

extern void MakePalette(DWORD pal[256]);
