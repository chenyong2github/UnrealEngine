// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSnapshotsEditorFunctionLibrary.generated.h"

class ULevelSnapshot;

UCLASS()
class LEVELSNAPSHOTSEDITOR_API ULevelSnapshotsEditorFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:

	UFUNCTION(BlueprintCallable, Category = "LevelSnapshots", meta=(DevelopmentOnly))
	static void SaveLevelSnapshotToDisk(const UObject* WorldContextObject, ULevelSnapshot* LevelSnapshot, const FString FileName, const FString FolderPath);
};