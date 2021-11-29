// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "HAL/IConsoleManager.h"

class FMLDeformerAssetActions;

namespace MLDeformerCVars
{
	extern TAutoConsoleVariable<bool> DebugDraw1;
	extern TAutoConsoleVariable<bool> DebugDraw2;
	extern TAutoConsoleVariable<float> DebugDrawPointSize;
};

class MLDEFORMEREDITOR_API FMLDeformerEditor : public IModuleInterface
{
public:
	void StartupModule() override;
	void ShutdownModule() override;

private:
	TSharedPtr<FMLDeformerAssetActions> MLDeformerAssetActions;
};
