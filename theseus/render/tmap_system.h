#pragma once

class DeltaFieldData
{
public:
	DeltaFieldData();
	~DeltaFieldData();

	long mNegYExtents;
	char* mField;
};


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

	static int sTempRef;
	static long sTempSize;
	static char* sTemp;

private:
    // Need this to prevent the compiler from using default copy ctor
    CSurfx(const CSurfx&);
    CSurfx& operator=(const CSurfx& rhs);
};

class DeltaField
{
public:
	DeltaField();

	// Suck in a new grad field. Resize must be called after Assign().
	void Assign();

	// Reinitiate/reset the computation of this grad field.
	void SetSize(long inWidth, long inHeight, bool inForceRegen = false);

	// Compute a small portion of the grad field. Call GetField() to see if it finished.
	int CalcSome();

	// See if this delta field is 100% calculated
	bool IsCalculated()
	{
		return mCurrentY == mHeight;
	}

	bool IsCalculating()
	{
		return mCurrentY >= 0 && mCurrentY != mHeight;
	}

	bool IsUninitialized()
	{
		return mCurrentY == -1;
	}

	// Returns a ptr to the buf of this grad field.
	// Note: If the field is not 100% calculated, it will finish calculating
	// and may take a couple seconds.
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
