// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "PresetAssetSubsystem.generated.h"

class UInteractiveToolsPresetCollectionAsset;

/**
 * Using an editor subsystem allows us to make sure that we have a default preset asset whenever the editor exists
 *  (and to avoid accidentally trying to make one when it doesn't, such as when running cooking scripts).
 */
UCLASS()
class PRESETASSET_API UPresetAssetSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UInteractiveToolsPresetCollectionAsset* GetDefaultCollection();
	bool SaveDefaultCollection();


protected:
	void InitializeDefaultCollection();

	UPROPERTY()
	TObjectPtr<UInteractiveToolsPresetCollectionAsset> DefaultCollection;
};