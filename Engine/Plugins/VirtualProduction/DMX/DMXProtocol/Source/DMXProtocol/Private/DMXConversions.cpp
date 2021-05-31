// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXConversions.h"


TArray<uint8> FDMXConversions::UnsignedInt32ToByteArray(uint32 Value, EDMXFixtureSignalFormat SignalFormat, bool bLSBOrder)
{
	const int32 NumBytes = static_cast<uint8>(SignalFormat) + 1;

	// To avoid branching in the loop, we'll decide before it on which byte to start
	// and which direction to go, depending on the Function's endianness.
	const int8 ByteIndexStep = bLSBOrder ? 1 : -1;
	int8 OutByteIndex = bLSBOrder ? 0 : NumBytes - 1;

	TArray<uint8> Bytes;
	Bytes.AddUninitialized(NumBytes);
	for (uint8 ValueByte = 0; ValueByte < NumBytes; ++ValueByte)
	{
		Bytes[OutByteIndex] = (Value >> 8 * ValueByte) & 0xFF;
		OutByteIndex += ByteIndexStep;
	}

	return Bytes;
}

TArray<uint8> FDMXConversions::NormalizedDMXValueToByteArray(float NormalizedValue, EDMXFixtureSignalFormat SignalFormat, bool bLSBOrder)
{
	NormalizedValue = FMath::Clamp(NormalizedValue, 0.f, 1.f);
	uint32 Value = FMath::Floor<uint32>(TNumericLimits<uint32>::Max() * static_cast<double>(NormalizedValue));
		
	// Shift the value into signal format range
	Value = Value >> ((3 - static_cast<uint8>(SignalFormat)) * 8);

	return UnsignedInt32ToByteArray(Value, SignalFormat, bLSBOrder);
}
