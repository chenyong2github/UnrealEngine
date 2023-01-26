// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageAppLibrary.h"
#include "StageAppVersion.h"

FString UStageAppFunctionLibrary::GetAPIVersion()
{
	return FString::Printf(TEXT("%u.%u.%u"),
		FEpicStageAppAPIVersion::Major,
		FEpicStageAppAPIVersion::Minor,
		FEpicStageAppAPIVersion::Patch);
}
