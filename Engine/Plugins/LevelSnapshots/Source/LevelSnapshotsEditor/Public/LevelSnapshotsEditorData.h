// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "LevelSnapshotsEditorData.generated.h"

UCLASS()
class LEVELSNAPSHOTSEDITOR_API ULevelSnapshotsEditorDataResults : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "EditorSnapshots")
	TArray<FString> Tags;
};

UCLASS()
class LEVELSNAPSHOTSEDITOR_API ULevelSnapshotsEditorData : public UObject
{
	GENERATED_BODY()

public:

	/** Instanced, for testing now*/
	UPROPERTY(Instanced)
	ULevelSnapshotsEditorDataResults* EditorDataResults;
};