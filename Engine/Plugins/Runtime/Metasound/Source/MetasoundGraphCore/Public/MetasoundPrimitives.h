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
	DECLARE_METASOUND_DATA_REFERENCE_TYPES(bool, "Primitive:Bool", 0xcc9d715e, FBoolTypeInfo, FBoolReadRef, FBoolWriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(int32, "Primitive:Int32", 0xf9920034, FInt32TypeInfo, FInt32ReadRef, FInt32WriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(int64, "Primitive:Int64", 0xfc8f8ada, FInt64TypeInfo, FInt64ReadRef, FInt64WriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(float, "Primitive:Float", 0xe7adc4db, FFloatTypeInfo, FFloatReadRef, FFloatWriteRef);

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(double, "Primitive:Double", 0x8f48a053, FDoubleTypeInfo, FDoubleReadRef, FDoubleWriteRef);
}

