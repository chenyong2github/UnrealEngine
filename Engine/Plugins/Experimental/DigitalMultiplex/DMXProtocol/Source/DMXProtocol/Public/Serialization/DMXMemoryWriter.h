// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/BufferArchive.h"

class FDMXMemoryWriter : public FBufferArchive
{
public:
	virtual void Serialize(void* Data, int64 Num) override
	{
		if (IsByteSwapping())
		{
			ByteSwap(Data, Num);
		}

		// Call Parent
		FBufferArchive::Serialize(Data, Num);
	}
};