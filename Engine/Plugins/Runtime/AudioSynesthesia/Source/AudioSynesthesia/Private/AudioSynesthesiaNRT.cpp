// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioSynesthesiaNRT.h"


const TArray<FText>& UAudioSynesthesiaNRTSettings::GetAssetActionSubmenus() const
{
	static const TArray<FText> SubMenus
	{
		NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AssetSoundAnalysisSubmenu", "Analysis")
	};

	return SubMenus;
}

FColor UAudioSynesthesiaNRTSettings::GetTypeColor() const
{
	return FColor(200.0f, 150.0f, 200.0f);
}

const TArray<FText>& UAudioSynesthesiaNRT::GetAssetActionSubmenus() const
{
	static const TArray<FText> SubMenus
	{
		NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AssetSoundAnalysisSubmenu", "Analysis")
	};

	return SubMenus;
}

FColor UAudioSynesthesiaNRT::GetTypeColor() const
{
	return FColor(200.0f, 150.0f, 200.0f);
}

