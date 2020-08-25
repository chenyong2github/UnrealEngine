// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLightGizmosModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void OnPostEngineInit();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Identifiers for the different light gizmo types*/
	static FString PointLightGizmoType;
	static FString SpotLightGizmoType;
	static FString ScalableConeGizmoType;
	static FString DirectionalLightGizmoType;
};
