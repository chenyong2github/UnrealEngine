// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "EditorSubsystem.h"
#include "Engine/EngineTypes.h"
#include "TickableEditorObject.h"
#include "WaterEditorSubsystem.generated.h"

class AWaterMeshActor;
class UTexture2D;
class UTextureRenderTarget2D;
class UWorld;
class UWaterSubsystem;
class UMaterialParameterCollection;

UCLASS()
class UWaterEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void UpdateWaterTextures(
		UWorld* World,
		UTextureRenderTarget2D* SourceVelocityTarget,
		UTexture2D*& OutWaterVelocityTexture);

	UMaterialParameterCollection* GetLandscapeMaterialParameterCollection() const {	return LandscapeMaterialParameterCollection; }

private:
	UPROPERTY()
	UMaterialParameterCollection* LandscapeMaterialParameterCollection;

	TWeakObjectPtr<AWaterMeshActor> WaterMeshActor;
};
