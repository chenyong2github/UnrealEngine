// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolArtNetUtils.h"

namespace ArtNet
{
	uint16 GetPacketType(const FArrayReaderPtr& Buffer)
	{
		uint16 OpCode = 0x0000;
		const uint32 MinCheck = ARTNET_STRING_SIZE + 2;
		if (Buffer->Num() > MinCheck)
		{
			// Get OpCode
			Buffer->Seek(ARTNET_STRING_SIZE);
			*Buffer << OpCode;

			// Reset Position
			Buffer->Seek(0);
		}

		return OpCode;
	}
}
