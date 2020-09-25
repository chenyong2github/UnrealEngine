// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UAssetEditor.h"

#include "DisplayClusterConfiguratorEditor.generated.h"

class UDisplayClusterConfiguratorEditorData;

/**
 * Configurator asset editor
 */
UCLASS()
class UDisplayClusterConfiguratorEditor
	: public UAssetEditor
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

	virtual void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit) override;
	UDisplayClusterConfiguratorEditorData* GetEditingObject() const;

	// Load with OpenFileDialog
	UFUNCTION(BlueprintCallable, Category = "nDisplay")
	bool LoadWithOpenFileDialog();

	// Load from specified file
	UFUNCTION(BlueprintCallable, Category = "nDisplay")
	bool LoadFromFile(UDisplayClusterConfiguratorEditorData* InConfiguratorEditorData, const FString& FilePath);

	// Save to the same file the config data was read from
	UFUNCTION(BlueprintCallable, Category = "nDisplay")
	bool Save();

	// Save to a specified file
	UFUNCTION(BlueprintCallable, Category = "nDisplay")
	bool SaveToFile(const FString& FilePath);

	// Save with SaveFileDialog
	UFUNCTION(BlueprintCallable, Category = "nDisplay")
	bool SaveWithOpenFileDialog();

	void SetObjectsToEdit(const TArray<UObject*>& InObjects);

protected:
	TArray<UDisplayClusterConfiguratorEditorData*> ObjectsToEdit;
};
