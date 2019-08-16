// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#if INCLUDE_CHAOS

struct FCollisionFilterData
{
	uint32 Word0;
	uint32 Word1;
	uint32 Word2;
	uint32 Word3;

	FORCEINLINE FCollisionFilterData()
	{
		Word0 = Word1 = Word2 = Word3 = 0;
	}
};

#endif