// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LandmassBPEditorExtension.h"
#include "CoreMinimal.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"

ULandmassBlueprintFunctionLibrary::ULandmassBlueprintFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

bool ULandmassBlueprintFunctionLibrary::GetCursorWorldRay(FVector& CameraLocation, FVector& RayOrigin, FVector& RayDirection)
{
	FViewportCursorLocation CursorLocation = GCurrentLevelEditingViewportClient->GetCursorWorldLocationFromMousePos();

	CameraLocation = CursorLocation.GetOrigin();
	RayOrigin = CursorLocation.GetOrigin();
	RayDirection = CursorLocation.GetDirection();

	return true;
}