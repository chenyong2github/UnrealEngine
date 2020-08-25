// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Editor/UnrealEdEngine.h"
#include "LevelInstanceEditorSettings.generated.h"

UCLASS(config = Editor)
class ULevelInstanceEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	ULevelInstanceEditorSettings();

	/** List of info for all known LevelInstance template maps */
	UPROPERTY(config)
	TArray<FTemplateMapInfo> TemplateMapInfos;
};