// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolSACNConstants.h"

#include "Serialization/ArrayReader.h"

namespace SACN
{
	static uint32 GetRootPacketType(const FArrayReaderPtr& Buffer)
	{

		uint32 Vector = 0x00000000;
		const uint32 MinCheck = ACN_ADDRESS_ROOT_VECTOR + 4;
		if (Buffer->Num() > MinCheck)
		{
			// Get OpCode
			Buffer->Seek(ACN_ADDRESS_ROOT_VECTOR);
			*Buffer << Vector;
			Buffer->ByteSwap(&Vector, 4);

			// Reset Position
			Buffer->Seek(0);
		}

		return Vector;
	}
}