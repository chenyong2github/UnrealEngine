// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "StateTreeEditorSettings.generated.h"

UENUM()
enum class EStateTreeSaveOnCompile : uint8
{
	Never UMETA(DisplayName = "Never"),
	SuccessOnly UMETA(DisplayName = "On Success Only"),
	Always UMETA(DisplayName = "Always"),
};

UCLASS(config = EditorPerProjectUserSettings)
class STATETREEEDITORMODULE_API UStateTreeEditorSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()
public:
	/** Determines when to save StateTrees post-compile */
	UPROPERTY(EditAnywhere, config, Category = "Compiler")
	EStateTreeSaveOnCompile SaveOnCompile = EStateTreeSaveOnCompile::Never;
};
