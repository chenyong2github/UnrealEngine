// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DMXProtocolTypes.h"

#include "Misc/ByteSwap.h"


/** Common conversions for dmx */
class DMXPROTOCOL_API FDMXConversions
{
	//////////////////////////////////////////////////
	// TODO: All DMX conversions eventually should be moved here (e.g. from subsystem, fixture type) as statics, unit tested and optimized.
	// BP implementations in other classes should be forwarded here, native C++ implementations should be flagged deprecated to reduce code scattering.

public:
	/** Converts a uint32 to a byte array */
	static TArray<uint8> UnsignedInt32ToByteArray(uint32 Value, EDMXFixtureSignalFormat SignalFormat, bool bLSBOrder);

	/** Converts a normalized value to a byte array. Normalized value has to be in the 0-1 range. Assumes max signal format is 24bit. */
	static TArray<uint8> NormalizedDMXValueToByteArray(float NormalizedValue, EDMXFixtureSignalFormat SignalFormat, bool bLSBOrder);

private:
	FDMXConversions() = delete;
};
