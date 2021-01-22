// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "LevelSnapshotsUserSettings.generated.h"

class ULevelSnapshotsEditorData;

UCLASS(config = EditorPerProjectUserSettings, MinimalAPI)
class ULevelSnapshotsUserSettings : public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(config)
	TSoftObjectPtr<ULevelSnapshotsEditorData> LastEditorData;
};