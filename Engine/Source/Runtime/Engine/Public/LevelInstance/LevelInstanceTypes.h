// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LevelInstanceTypes.generated.h"

// FLevelInstanceID is a runtime unique id that is computed from the Hash of LevelInstance Actor Guid and all its ancestor LevelInstance Actor Guids.
// Resulting in a different ID for all instances whether they load the same level or not.
using FLevelInstanceID = uint32;

static constexpr FLevelInstanceID InvalidLevelInstanceID = 0;

UENUM()
enum class ELevelInstanceCreationType : uint8
{
	LevelInstance,
	PackedLevelInstance,
	PackedLevelInstanceBlueprint
};

UENUM()
enum class ELevelInstancePivotType : uint8
{
	CenterMinZ,
	Center,
	Actor
};

USTRUCT()
struct FNewLevelInstanceParams
{
	GENERATED_USTRUCT_BODY()
			
	UPROPERTY(EditAnywhere, Category = Default)
	ELevelInstanceCreationType Type = ELevelInstanceCreationType::LevelInstance;

	UPROPERTY(EditAnywhere, Category = Default)
	bool bExternalActors = true;

	UPROPERTY(EditAnywhere, Category = Pivot)
	ELevelInstancePivotType PivotType = ELevelInstancePivotType::CenterMinZ;

	UPROPERTY(EditAnywhere, Category = Pivot)
	TObjectPtr<AActor> PivotActor = nullptr;

	UPROPERTY()
	TObjectPtr<UWorld> TemplateWorld = nullptr;
		
	UPROPERTY()
	FString LevelPackageName = TEXT("");

	UPROPERTY()
	bool bPromptForSave = false;
};