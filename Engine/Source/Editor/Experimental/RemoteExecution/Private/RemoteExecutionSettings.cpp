// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteExecutionSettings.h"
#include "Misc/ConfigCacheIni.h"

URemoteExecutionSettings::URemoteExecutionSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PreferredRemoteExecutor = TEXT("Bazel");
}
