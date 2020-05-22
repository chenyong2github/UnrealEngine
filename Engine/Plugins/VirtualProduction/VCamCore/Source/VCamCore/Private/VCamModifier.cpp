// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamModifier.h"

void UVCamBlueprintModifier::Initialize()
{
	// Forward the Initialize call to the Blueprint Event
	{
		FEditorScriptExecutionGuard ScriptGuard;

		OnInitialize();
	}
}

void UVCamBlueprintModifier::Apply(const FLiveLinkCameraBlueprintData& InitialLiveLinkData,
        UCineCameraComponent* CameraComponent, const float DeltaTime)
{
	// Forward the Apply call to the Blueprint Event
	{
		FEditorScriptExecutionGuard ScriptGuard;

		OnApply(InitialLiveLinkData, CameraComponent, DeltaTime);
	}
}