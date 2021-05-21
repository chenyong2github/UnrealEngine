// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include "DerivedDataBuildWorkerInterface.h"
#include "LaunchEngineLoop.h"
#include "Modules/ModuleManager.h"
#include "TextureBuildFunction.h"

IMPLEMENT_APPLICATION(BaseTextureBuildWorker, "BaseTextureBuildWorker");

void DerivedDataBuildWorkerInit()
{
	static FTextureBuildFunction UncompressedTextureBuildFunction;
	UncompressedTextureBuildFunction.SetName(TEXT("UncompressedTexture"));
	UE::DerivedData::RegisterWorkerBuildFunction(&UncompressedTextureBuildFunction);

	static FTextureBuildFunction OodleTextureBuildFunction;
	OodleTextureBuildFunction.SetName(TEXT("OodleTexture"));
	UE::DerivedData::RegisterWorkerBuildFunction(&OodleTextureBuildFunction);
}

