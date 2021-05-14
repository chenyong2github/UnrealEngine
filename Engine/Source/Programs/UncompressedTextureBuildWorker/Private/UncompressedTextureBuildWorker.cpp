// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include "DerivedDataBuildWorkerInterface.h"
#include "LaunchEngineLoop.h"
#include "Modules/ModuleManager.h"
#include "TextureBuildFunction.h"

IMPLEMENT_APPLICATION(UncompressedTextureBuildWorker, "UncompressedTextureBuildWorker");


void DerivedDataBuildWorkerInit()
{
	static FTextureBuildFunction TextureBuildFunction;
	TextureBuildFunction.SetName(TEXT("UncompressedTexture"));
	UE::DerivedData::RegisterWorkerBuildFunction(&TextureBuildFunction);
}

