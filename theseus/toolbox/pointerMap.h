// pointerMap.h: header-only string-keyed map of pointer values, used by
// the toolbox where the C++ STL containers aren't available or aren't
// worth the codegen.

#pragma once

#include "xboxinternals.h"
#include "pointerVector.h"
#include "stringUtility.h"
#include <string.h>

#define MAP_SIZE_INCREMENT 100

template<typename T1>
class pointerMap
{
public:

	pointerMap(bool useDelete)
	{
		mUseDelete = useDelete;
		mSize = 0;
		mKeyValuePairs = (keyValuePair*)malloc(sizeof(keyValuePair) * MAP_SIZE_INCREMENT);
		if (mKeyValuePairs == NULL)
		{
			mMaxSize = 0;
			return;
		}
		mMaxSize = MAP_SIZE_INCREMENT;
	}

	~pointerMap()
	{
		for (uint32_t i = 0; i < mSize; i++)
		{
			free(mKeyValuePairs[i].key);
			if (mUseDelete == true)
			{
				delete(mKeyValuePairs[i].value);
			}
			else
			{
				free(mKeyValuePairs[i].value);
			}
		}
		free(mKeyValuePairs);
	}

	void add(const char* key, T1 value) 
	{
		if (mSize == mMaxSize)
		{
			keyValuePair* reallocedKeyValuePairs = (keyValuePair*)realloc(mKeyValuePairs, sizeof(keyValuePair) * (mMaxSize + MAP_SIZE_INCREMENT));
			if (reallocedKeyValuePairs == NULL)
			{ 
				return;
			}
			mMaxSize += MAP_SIZE_INCREMENT;
			mKeyValuePairs = reallocedKeyValuePairs;
		}

		mKeyValuePairs[mSize].key = strdup(key);
		mKeyValuePairs[mSize].value = value;
		mSize++;
	}

	void add(uint32_t key, T1 value) 
	{
		char* keyValue = stringUtility::formatString("%ui", key);
		add(keyValue, value);
		free(keyValue);
	}

	T1 get(const char* key) 
	{
		for (uint32_t i = 0; i < mSize; i++)
		{
			if (stringUtility::equals(mKeyValuePairs[i].key, key, false) == true) 
			{
				return mKeyValuePairs[i].value;
			}
		}
		return NULL;
	}

	T1 get(uint32_t key) 
	{
		char* keyValue = stringUtility::formatString("%ui", key);
		T1 result = get(keyValue);
		free(keyValue);
		return result;
	}

	void removeKey(const char* key) 
	{
		for (uint32_t i = 0; i < mSize; i++)
		{
			if (stringUtility::equals(mKeyValuePairs[i].key, key, false) == true) 
			{
				free(mKeyValuePairs[i].key);
				if (mUseDelete == true)
				{
					delete(mKeyValuePairs[i].value);
				}
				else
				{
					free(mKeyValuePairs[i].value);
				}
				for (uint32_t j = i; j < mSize - 1; j++)
				{
					mKeyValuePairs[j].key = mKeyValuePairs[j + 1].key;
					mKeyValuePairs[j].value = mKeyValuePairs[j + 1].value;
				}
				mSize--;
			}
		}
	}

	void removeKey(uint32_t key) 
	{
		char* keyValue = stringUtility::formatString("%ui", key);
		removeKey(keyValue);
		free(keyValue);
	}

	uint32_t count()
	{
		return mSize;
	}

	pointerVector<char*>* keys()
	{
		pointerVector<char*>* keys = new pointerVector<char*>(false);
		for (uint32_t i = 0; i < mSize; i++)
		{
			keys->add(strdup(mKeyValuePairs[i].key));
		}
		return keys;
	}

private:

    typedef struct keyValuePair {
        char* key;
        T1 value;
    } keyValuePair;

    keyValuePair* mKeyValuePairs;
	bool mUseDelete;
    uint32_t mSize;
    uint32_t mMaxSize;
};


