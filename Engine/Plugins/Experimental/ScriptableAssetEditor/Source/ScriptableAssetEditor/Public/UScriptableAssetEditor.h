// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UAssetEditor.h"

#include "UScriptableAssetEditor.generated.h"

UCLASS(Transient)
class UScriptableAssetEditor : public UAssetEditor
{
	GENERATED_BODY()

private:
	void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit) override;
	TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;
};
