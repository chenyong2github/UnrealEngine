// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPixelMappingUtils.h"
#include "DMXPixelMappingTypes.h"
#include "Interfaces/IDMXProtocol.h"

uint32 FDMXPixelMappingUtils::GetNumChannelsPerPixel(EDMXPixelFormat InPixelFormat)
{
	switch (InPixelFormat)
	{
	case EDMXPixelFormat::PF_RG:
	case EDMXPixelFormat::PF_RB:
	case EDMXPixelFormat::PF_GB:
	case EDMXPixelFormat::PF_GR:
	case EDMXPixelFormat::PF_BR:
	case EDMXPixelFormat::PF_BG:
		return 2;
	case EDMXPixelFormat::PF_RGB:
	case EDMXPixelFormat::PF_BRG:
	case EDMXPixelFormat::PF_GRB:
	case EDMXPixelFormat::PF_GBR:
		return 3;
	case EDMXPixelFormat::PF_RGBA:
	case EDMXPixelFormat::PF_GBRA:
	case EDMXPixelFormat::PF_BRGA:
	case EDMXPixelFormat::PF_GRBA:
		return 4;
	}

	return 1;
}

uint32 FDMXPixelMappingUtils::GetUniverseMaxChannels(EDMXPixelFormat InPixelFormat, uint32 InStartAddress)
{
	uint32 NumChannelsPerPixel = FDMXPixelMappingUtils::GetNumChannelsPerPixel(InPixelFormat);

	return DMX_MAX_ADDRESS - ((DMX_MAX_ADDRESS - (InStartAddress - 1)) % NumChannelsPerPixel);
}

bool FDMXPixelMappingUtils::CanFitPixelIntoChannels(EDMXPixelFormat InPixelFormat, uint32 InStartAddress)
{
	return (InStartAddress + GetNumChannelsPerPixel(InPixelFormat) - 1) <= DMX_MAX_ADDRESS;
}
