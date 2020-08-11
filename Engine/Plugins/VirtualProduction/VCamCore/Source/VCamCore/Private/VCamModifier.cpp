// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamModifier.h"


void UVCamBlueprintModifier::Initialize(UVCamModifierContext* Context)
{
	// Forward the Initialize call to the Blueprint Event
	{
		FEditorScriptExecutionGuard ScriptGuard;

		OnInitialize(Context);
	}

	UVCamModifier::Initialize(Context);
}

void UVCamBlueprintModifier::Apply(UVCamModifierContext* Context, const FLiveLinkCameraBlueprintData& InitialLiveLinkData,
        UCineCameraComponent* CameraComponent, const float DeltaTime)
{
	// Forward the Apply call to the Blueprint Event
	{
		FEditorScriptExecutionGuard ScriptGuard;

		OnApply(Context, InitialLiveLinkData, CameraComponent, DeltaTime);
	}
}

void UVCamModifier::Initialize(UVCamModifierContext* Context)
{
	bRequiresInitialization = false; 
}

void UVCamModifier::PostLoad()
{
	Super::PostLoad();

	bRequiresInitialization = true;
}
