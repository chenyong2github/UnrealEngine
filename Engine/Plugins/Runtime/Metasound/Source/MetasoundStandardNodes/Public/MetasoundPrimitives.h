// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"

#include "MetasoundTime.h"
#include "MetasoundFrequency.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundAudioFormats.h"

namespace Metasound
{
	DECLARE_METASOUND_DATA_REFERENCE_TYPES(bool, METASOUNDSTANDARDNODES_API, FBoolTypeInfo, FBoolReadRef, FBoolWriteRef);
	
	DECLARE_METASOUND_DATA_REFERENCE_TYPES(int32, METASOUNDSTANDARDNODES_API, FInt32TypeInfo, FInt32ReadRef, FInt32WriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(int64, METASOUNDSTANDARDNODES_API, FInt64TypeInfo, FInt64ReadRef, FInt64WriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(float, METASOUNDSTANDARDNODES_API, FFloatTypeInfo, FFloatReadRef, FFloatWriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(double, METASOUNDSTANDARDNODES_API, FDoubleTypeInfo, FDoubleReadRef, FDoubleWriteRef);
}

