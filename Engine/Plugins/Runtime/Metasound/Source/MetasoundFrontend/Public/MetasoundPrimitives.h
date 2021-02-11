// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"

namespace Metasound
{
	DECLARE_METASOUND_DATA_REFERENCE_TYPES(bool, METASOUNDFRONTEND_API, FBoolTypeInfo, FBoolReadRef, FBoolWriteRef);
	
	DECLARE_METASOUND_DATA_REFERENCE_TYPES(int32, METASOUNDFRONTEND_API, FInt32TypeInfo, FInt32ReadRef, FInt32WriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(int64, METASOUNDFRONTEND_API, FInt64TypeInfo, FInt64ReadRef, FInt64WriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(float, METASOUNDFRONTEND_API, FFloatTypeInfo, FFloatReadRef, FFloatWriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(double, METASOUNDFRONTEND_API, FDoubleTypeInfo, FDoubleReadRef, FDoubleWriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FString, METASOUNDFRONTEND_API, FStringTypeInfo, FStringReadRef, FStringWriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(TArray<float>, METASOUNDFRONTEND_API, FFloatArrayTypeInfo, FFloatArrayReadRef, FFloatArrayWriteRef);
}

