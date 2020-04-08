// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ChaosArchive.h"
#include "UObject/ExternalPhysicsCustomObjectVersion.h"

struct CHAOS_API FCollisionFilterData
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

inline Chaos::FChaosArchive& operator<<(Chaos::FChaosArchive& Ar, FCollisionFilterData& Filter)
{
	Ar << Filter.Word0 << Filter.Word1 << Filter.Word2 << Filter.Word3;
	return Ar;
}

inline bool operator!=(const FCollisionFilterData& A, const FCollisionFilterData& B)
{
	return A.Word0!=B.Word0 || A.Word1!=B.Word1 || A.Word2!=B.Word2 || A.Word3!=B.Word3;
}