// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"

namespace Metasound
{
	class FWave
	{
		TArray<uint8> CompressedBytes;
	public:
		const TArray<uint8>& GetCompressedData() const { return CompressedBytes; }
	};

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FWave, 0x0ddba11, FWaveTypeInfo, FWaveReadRef, FWaveWriteRef)
}
