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
	CSurfx(int nWidth, int nHeight);
	~CSurfx();

	int m_nWidth;
	int m_nHeight;
	BYTE* m_pels;

	int mBytesPerRow;

	int mLineWidth;
	RECT mClipRect;

	void Fade(DeltaFieldData* inGrad);
	void Fade(char* inPix, long inBytesPerRow, long inX, long inY, DeltaFieldData* inGrad);
	void Fade(const char* inSrce, long inBytesPerSrceRow, char* inDest, long inBytesPerDestRow, long inX, long inY, const char* inGrad);

	void Line(int sx, int sy, int ex, int ey, unsigned char color);

	inline BYTE* Pixel(int x, int y) { return &m_pels[mBytesPerRow * y + x]; }

	// Live surface count and the fade scratch buffer they share.
	static int sTempRef;
	static long sTempSize;
	static char* sTemp;

private:
	// Owns its pixels, so no copies.
	CSurfx(const CSurfx&);
	CSurfx& operator=(const CSurfx& rhs);
};

// Builds a warp field a few rows at a time so a style change doesn't stall a frame.
class DeltaField
{
public:
	DeltaField();

	// Resets the flags and restarts the field at the current size.
	void Assign();

	// Restarts the field if the size changed (or when forced).
	void SetSize(long inWidth, long inHeight, bool inForceRegen = false);

	// Computes the next row; returns how many are left.
	int CalcSome();

	bool IsCalculated() { return mCurrentY == mHeight; }
	bool IsCalculating() { return mCurrentY >= 0 && mCurrentY != mHeight; }
	bool IsUninitialized() { return mCurrentY == -1; }

	// The field, finished first if it wasn't; NULL if it was never sized.
	DeltaFieldData* GetField();

	void GetXY(float r, float theta, float x, float y, float& X, float& Y, bool& bPolar, bool bInit);
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

extern void MakePalette(DWORD outPalette[256]);
