// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Editor/UnrealEdEngine.h"
#include "FoundationEditorSettings.generated.h"

UCLASS(config = Editor)
class UFoundationEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	UFoundationEditorSettings();

	/** List of info for all known foundation template maps */
	UPROPERTY(config)
	TArray<FTemplateMapInfo> TemplateMapInfos;
};