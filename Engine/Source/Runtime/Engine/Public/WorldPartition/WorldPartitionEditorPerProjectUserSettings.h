// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorldPartitionEditorPerProjectUserSettings.generated.h"

USTRUCT()
struct FWorldPartitionPerWorldEditorGridSettings
{
	GENERATED_BODY();

#if WITH_EDITOR
	FWorldPartitionPerWorldEditorGridSettings()
	{}

	FWorldPartitionPerWorldEditorGridSettings(TArray<FVector>& InLastLoadedCells)
		: LastLoadedCells(InLastLoadedCells)
	{}
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FVector> LastLoadedCells;
#endif
};

UCLASS(MinimalAPI, config = EditorPerProjectUserSettings)
class UWorldPartitionEditorPerProjectUserSettings : public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	uint32 GetEditorGridConfigHash() const
	{ 
		return EditorGridConfigHash;
	}

	void SetEditorGridConfigHash(uint32 InEditorGridConfigHash)
	{ 
		if (EditorGridConfigHash != InEditorGridConfigHash)
		{
			EditorGridConfigHash = InEditorGridConfigHash;
			PerWorldEditorGridSettings.Empty();
			SaveConfig();
		}
	}

	const TArray<FVector>& GetEditorGridLastLoadedCells(UWorld* InWorld)
	{ 
		return PerWorldEditorGridSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld)).LastLoadedCells;
	}

	void SetEditorGridLastLoadedCells(UWorld* InWorld, TArray<FVector>& InEditorGridLastLoadedCells)
	{
		PerWorldEditorGridSettings.Add(TSoftObjectPtr<UWorld>(InWorld), InEditorGridLastLoadedCells);
		SaveConfig();
	}
#endif

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY(config)
	uint32 EditorGridConfigHash;

	UPROPERTY(config)
	TMap<TSoftObjectPtr<UWorld>, FWorldPartitionPerWorldEditorGridSettings> PerWorldEditorGridSettings;
#endif
};