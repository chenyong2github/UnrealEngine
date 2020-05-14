// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UAssetEditor.h"

#include "UScriptableAssetEditor.generated.h"

UCLASS(Transient)
class UScriptableAssetEditor : public UAssetEditor
{
	GENERATED_BODY()

public:
	void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit) override;
};
