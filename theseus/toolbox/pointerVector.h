// pointerVector.h: header-only growable pointer array used by the toolbox
// in place of std::vector<T*>. Owns its storage; capacity grows by
// VECTOR_SIZE_INCREMENT entries at a time.

#pragma once

#include "xboxinternals.h"

#define VECTOR_SIZE_INCREMENT 100

template<typename T1>
class pointerVector
{
public:

	pointerVector(bool useDelete)
	{
		mUseDelete = useDelete;
		mSize = 0;
		mValueContainers = (valueContainer*)malloc(sizeof(valueContainer) * VECTOR_SIZE_INCREMENT);
		if (mValueContainers == NULL)
		{
			mMaxSize = 0;
			return;
		}
		mMaxSize = VECTOR_SIZE_INCREMENT;
	}

	~pointerVector()
	{
		for (uint32_t i = 0; i < mSize; i++)
		{
			if (mUseDelete == true)
			{
				delete(mValueContainers[i].value);
			}
			else
			{
				free(mValueContainers[i].value);
			}
		}
		free(mValueContainers);
	}

	void add(T1 value)
	{
		if (mSize == mMaxSize)
		{
			valueContainer* reallocedValueContainers = (valueContainer*)realloc(mValueContainers, sizeof(valueContainer) * (mMaxSize + VECTOR_SIZE_INCREMENT));
			if (reallocedValueContainers == NULL)
			{ 
				return;
			}
			mMaxSize += VECTOR_SIZE_INCREMENT;
			mValueContainers = reallocedValueContainers;
		}

		mValueContainers[mSize].value = value;
		mSize++;
	}

	T1 get(uint32_t index)
	{
		return mValueContainers[index].value;
	}

	void remove(uint32_t index)
	{
		if (mUseDelete == true)
		{
			delete(mValueContainers[index].value);
		}
		else
		{
			free(mValueContainers[index].value);
		}
		for (uint32_t j = index; j < mSize - 1; j++)
		{
			mValueContainers[j].value = mValueContainers[j + 1].value;
		}
		mSize--; 
	}

	void swap(uint32_t index1, uint32_t index2)
	{
		T1 temp = mValueContainers[index1].value;
		mValueContainers[index1].value = mValueContainers[index2].value;
		mValueContainers[index2].value = temp;
	}

	uint32_t count()
	{
		return mSize;
	}

	void clear()
	{    
		for (uint32_t i = 0; i < mSize; i++)
		{
			if (mUseDelete == true)
			{
				delete(mValueContainers[i].value);
			}
			else
			{
				free(mValueContainers[i].value);
			}
		}
		mSize = 0;
	}

private:

    typedef struct valueContainer {
        T1 value;
    } valueContainer;

    valueContainer* mValueContainers;
	bool mUseDelete;
    uint32_t mSize;
    uint32_t mMaxSize;
};

