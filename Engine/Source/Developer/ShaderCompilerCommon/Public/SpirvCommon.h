// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Base structure for SPIR-V modules in the shader backends. */
struct FSpirv
{
	TArray<uint32> Data;

	/** Returns a byte pointer to the SPIR-V data. */
	FORCEINLINE int8* GetByteData()
	{
		return reinterpret_cast<int8*>(Data.GetData());
	}

	/** Returns a byte pointer to the SPIR-V data. */
	FORCEINLINE const int8* GetByteData() const
	{
		return reinterpret_cast<const int8*>(Data.GetData());
	}

	/** Returns the size of this SPIR-V module in bytes. */
	FORCEINLINE int32 GetByteSize() const
	{
		return Data.Num() * sizeof(uint32);
	}
};


