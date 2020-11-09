// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "DataLayerEditorPerProjectUserSettings.generated.h"

USTRUCT()
struct FDataLayerNames
{
	GENERATED_BODY();

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FName> DataLayers;
#endif
};

UCLASS(MinimalAPI, config = EditorPerProjectUserSettings)
class UDataLayerEditorPerProjectUserSettings : public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR	
	bool GetShowDataLayerContent() const { return bShowDataLayerContent; }
	void SetShowDataLayerContent(bool bInShowDataLayerContent)
	{
		if (bShowDataLayerContent != bInShowDataLayerContent)
		{
			bShowDataLayerContent = bInShowDataLayerContent;
			SaveConfig();
		}
	}

	const FDataLayerNames* GetWorldDataLayersNotLoadedInEditor(UWorld* InWorld) const
	{
		return InWorld ? WorldDataLayersNotLoadedInEditor.Find(TSoftObjectPtr<UWorld>(InWorld)) : nullptr;
	}

	void SetWorldDataLayersNotLoadedInEditor(UWorld* InWorld, const TArray<FName>& InDataLayersNotLoadedInEditor)
	{
		if (InWorld)
		{
			FDataLayerNames& DataLayersNotLoadedInEditor = WorldDataLayersNotLoadedInEditor.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld));
			if (DataLayersNotLoadedInEditor.DataLayers != InDataLayersNotLoadedInEditor)
			{
				DataLayersNotLoadedInEditor.DataLayers = InDataLayersNotLoadedInEditor;
				SaveConfig();
			}
		}
	}
#endif

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY(config)
	uint32 bShowDataLayerContent : 1;

	UPROPERTY(config)
	TMap<TSoftObjectPtr<UWorld>, FDataLayerNames> WorldDataLayersNotLoadedInEditor;
#endif
};