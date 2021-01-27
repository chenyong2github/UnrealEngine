// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/UniqueIndexSet.h"

FUniqueIndexSet::~FUniqueIndexSet()
{
	if (Bits != nullptr)
	{
		delete[] Bits;
	}
}
