// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Styling/AppStyle.h"

class FClothingAssetListCommands : public TCommands<FClothingAssetListCommands>
{
public:
	FClothingAssetListCommands()
		: TCommands<FClothingAssetListCommands>(
			TEXT("ClothAssetList"), 
			NSLOCTEXT("Contexts", "ClothAssetList", "Clothing Asset List"), 
			NAME_None, 
			FAppStyle::GetAppStyleSetName())
	{}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> DeleteAsset;

	TSharedPtr<FUICommandInfo> RebuildAssetParams;
	TMap<FName, TSharedPtr<FUICommandInfo>> ExportAssets;
};
