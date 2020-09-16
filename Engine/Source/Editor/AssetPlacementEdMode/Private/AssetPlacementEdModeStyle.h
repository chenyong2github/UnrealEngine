// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FSlateStyleSet;

class FAssetPlacementEdModeStyle
{
public:
	~FAssetPlacementEdModeStyle();

	static void Initialize();

	static void Shutdown();

	static FName GetStyleSetName();

private:
	FAssetPlacementEdModeStyle();

	void SetupCustomStyle();

	TSharedPtr<FSlateStyleSet> StyleSet;
	static TSharedPtr<FAssetPlacementEdModeStyle> AssetPlacementEdModeStyle;
};
