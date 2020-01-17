// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolTypes.h"

bool FDMXBuffer::SetDMXFragment(const IDMXFragmentMap & DMXFragment)
{
	for (TMap<uint32, uint8>::TConstIterator It = DMXFragment.CreateConstIterator(); It; ++It)
	{
		if (It->Key < (DMX_UNIVERSE_SIZE - 1))
		{
			DMXData[It->Key] = It->Value;
		}
	}

	return false;
}
