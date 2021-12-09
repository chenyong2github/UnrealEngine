// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "LevelInstanceTypes.generated.h"

// FLevelInstanceID is a runtime unique id that is computed from the Hash of LevelInstance Actor Guid and all its ancestor LevelInstance Actor Guids.
// Resulting in a different ID for all instances whether they load the same level or not.
struct FLevelInstanceID
{
	FLevelInstanceID() {}
	FLevelInstanceID(class ULevelInstanceSubsystem* LevelInstanceSubsystem, class ALevelInstance* Actor);

	inline friend uint32 GetTypeHash(const FLevelInstanceID& Key)
	{
		return ::GetTypeHash(Key.GetHash());
	}

	inline bool operator!=(const FLevelInstanceID& Other) const
	{
		return !(*this == Other);
	}

	inline bool operator==(const FLevelInstanceID& Other) const
	{
		return Hash == Other.Hash && Guids == Other.Guids;
	}

	inline bool IsValid() const { return !Guids.IsEmpty(); }

	inline uint64 GetHash() const { return Hash; }

private:
	uint64 Hash = 0;
	TArray<FGuid> Guids;
};

UENUM()
enum class ELevelInstanceCreationType : uint8
{
	LevelInstance,
	PackedLevelActor
};

UENUM()
enum class ELevelInstancePivotType : uint8
{
	CenterMinZ,
	Center,
	Actor,
	WorldOrigin
};

USTRUCT()
struct FNewLevelInstanceParams
{
	GENERATED_USTRUCT_BODY()
			
	UPROPERTY(EditAnywhere, Category = Default, meta = (EditCondition = "!bHideCreationType", EditConditionHides, HideEditConditionToggle))
	ELevelInstanceCreationType Type = ELevelInstanceCreationType::LevelInstance;

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

private:
	UPROPERTY(EditAnywhere, Category = Default, meta = (EditCondition = "!bForceExternalActors", EditConditionHides, HideEditConditionToggle))
	bool bExternalActors = true;
	
	UPROPERTY()
	bool bForceExternalActors = false;

	UPROPERTY()
	bool bHideCreationType = false;

public:
	void HideCreationType() { bHideCreationType = true; }
	void SetForceExternalActors(bool bInForceExternalActors) { bForceExternalActors = bInForceExternalActors; }
	void SetExternalActors(bool bInExternalActors) { bExternalActors = bInExternalActors; }
	bool UseExternalActors() const { return bForceExternalActors || bExternalActors; }
};