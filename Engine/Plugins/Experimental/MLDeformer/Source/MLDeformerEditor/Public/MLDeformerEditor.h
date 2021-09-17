// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FMLDeformerAssetActions;

class MLDEFORMEREDITOR_API FMLDeformerEditor : public IModuleInterface
{
public:
	void StartupModule() override;
	void ShutdownModule() override;

private:
	TSharedPtr<FMLDeformerAssetActions> MLDeformerAssetActions;
};
