// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/NetSerialization.h"
#include "HAL/IConsoleManager.h"

DEFINE_STAT(STAT_NetSerializeFastArray);
DEFINE_STAT(STAT_NetSerializeFastArray_BuildMap);
DEFINE_STAT(STAT_NetSerializeFastArray_DeltaStruct);

int32 FFastArraySerializer::MaxNumberOfAllowedChangesPerUpdate = 2048;
FAutoConsoleVariableRef FFastArraySerializer::CVarMaxNumberOfAllowedChangesPerUpdate(
	TEXT("net.MaxNumberOfAllowedTArrayChangesPerUpdate"),
	FFastArraySerializer::MaxNumberOfAllowedChangesPerUpdate,
	TEXT("")
);

int32 FFastArraySerializer::MaxNumberOfAllowedDeletionsPerUpdate = 2048;
FAutoConsoleVariableRef FFastArraySerializer::CVarMaxNumberOfAllowedDeletionsPerUpdate(
	TEXT("net.MaxNumberOfAllowedTArrayDeletionsPerUpdate"),
	FFastArraySerializer::MaxNumberOfAllowedDeletionsPerUpdate,
	TEXT("")
);