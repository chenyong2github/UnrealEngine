// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "allocator_utils.h"

#include <cstdlib>
#include <cstring>

namespace VivoxClientApi {
	static void*(*AllocateFunction)(size_t) = nullptr;
	static void(*DeallocateFunction)(void*) = nullptr;

	void SetMemFunctions(void*(*InAllocateFunction)(size_t), void(*InDeallocateFunction)(void*))
	{
		AllocateFunction = InAllocateFunction;
		DeallocateFunction = InDeallocateFunction;
	}

	void* Allocate(size_t n)
	{
		if (AllocateFunction)
		{
			return (*AllocateFunction)(n);
		}
		else
		{
			return malloc(n);
		}
	}

	void Deallocate(void* p)
	{
		if (DeallocateFunction)
		{
			(DeallocateFunction)(p);
		}
		else
		{
			free(p);
		}
	}

	char* StrDup(const char* s)
	{
		size_t len = strlen(s);
		char* ret = static_cast<char*>(Allocate(len + 1));
		memcpy(ret, s, len + 1);
		return ret;
	}
}