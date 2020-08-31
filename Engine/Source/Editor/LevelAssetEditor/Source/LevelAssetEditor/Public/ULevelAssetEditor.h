// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UAssetEditor.h"

#include "ULevelAssetEditor.generated.h"

class UInteractiveToolsContext;
class FBaseAssetToolkit;

UCLASS(Transient)
class ULevelAssetEditor : public UAssetEditor
{
	GENERATED_BODY()

public:
	void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit) override;
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

protected:
	UPROPERTY()
	UInteractiveToolsContext* InteractiveToolsContext;
};
