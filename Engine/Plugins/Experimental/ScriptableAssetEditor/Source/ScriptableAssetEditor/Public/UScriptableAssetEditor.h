// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UAssetEditor.h"

#include "UScriptableAssetEditor.generated.h"

class UInteractiveToolsContext;
class FBaseAssetToolkit;

UCLASS(Transient)
class UScriptableAssetEditor : public UAssetEditor
{
	GENERATED_BODY()

public:
	void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit) override;
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

protected:
	UPROPERTY()
	UInteractiveToolsContext* InteractiveToolsContext;
};
