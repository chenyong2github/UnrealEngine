// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

class FLevelInstanceEditorModeToolkit : public FModeToolkit
{
public:
	FLevelInstanceEditorModeToolkit();

	// IToolkit interface 
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;

	virtual void SetModeUILayer(const TSharedPtr<FAssetEditorModeUILayer> InLayer) override;
};