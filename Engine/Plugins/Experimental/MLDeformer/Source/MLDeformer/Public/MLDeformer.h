// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

MLDEFORMER_API DECLARE_LOG_CATEGORY_EXTERN(LogMLDeformer, Log, All);

class MLDEFORMER_API FMLDeformerModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation. */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
