// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MeshDeformerProvider.h"
#include "Modules/ModuleInterface.h"

class FOptimusSettingsModule : public IModuleInterface, public IMeshDeformerProvider
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** IMeshDeformerProvider implementation */
	TSoftObjectPtr<UMeshDeformer> GetDefaultMeshDeformer() override;
};
