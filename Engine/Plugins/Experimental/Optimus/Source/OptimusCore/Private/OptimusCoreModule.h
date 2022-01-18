// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MeshDeformerProvider.h"
#include "Delegates/IDelegateInstance.h"
#include "IOptimusCoreModule.h"

class FOptimusCoreModule : public IOptimusCoreModule, public IMeshDeformerProvider
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** IMeshDeformerProvider implementation */
	bool IsEnabled(EShaderPlatform Platform) override;
	TSoftObjectPtr<UMeshDeformer> GetDefaultMeshDeformer() override;
};

DECLARE_LOG_CATEGORY_EXTERN(LogOptimusCore, Log, All);
