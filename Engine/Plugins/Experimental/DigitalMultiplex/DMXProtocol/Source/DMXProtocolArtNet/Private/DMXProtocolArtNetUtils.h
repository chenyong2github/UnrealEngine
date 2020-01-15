// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolArtNetConstants.h"

#include "Serialization/ArrayReader.h"

namespace ArtNet
{
	/** Get package 2 bytes ID from the package */
	uint16 GetPacketType(const FArrayReaderPtr& Buffer);
}