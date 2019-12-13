// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolArtNetUtils.h"
#include "DMXProtocolArtNetPublicUtils.h"

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

	uint16 ComputeUniverseID(const uint8 Net, const uint8 Subnet, const uint8 Universe)
	{
		/* Bits 15 			Bits 14-8        | Bits 7-4      | Bits 3-0
		* 0 				Net 			 | Sub-Net 		 | Universe
		* 0				    (0b1111111 << 8) | (0b1111 << 4) | (0b1111)
		*/
		return (Net << 8) | (Subnet << 4) | Universe;
	}
}
