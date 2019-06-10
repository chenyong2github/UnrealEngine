// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "EditorUtilityWidget.h"

#include "VPUtilitiesEditorSettings.generated.h"

/**
 * Virtual Production utilities settings for editor
 */
UCLASS(config=Editor)
class VPUTILITIESEDITOR_API UVPUtilitiesEditorSettings : public UObject
{
	GENERATED_BODY()

public:

	/** The default user interface that we'll use for virtual scouting */
	UPROPERTY(EditAnywhere, config, Category = "Virtual Production", meta = (DisplayName = "Virtual Scouting User Interface"))
	TSoftClassPtr<UEditorUtilityWidget> VirtualScoutingUI;
};