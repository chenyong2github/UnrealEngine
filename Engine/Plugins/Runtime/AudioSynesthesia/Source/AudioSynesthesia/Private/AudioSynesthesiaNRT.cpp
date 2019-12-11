// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioSynesthesiaNRT.h"


FText UAudioSynesthesiaNRTSettings::GetAssetActionName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AssetSoundSynesthesiaNRTSettings", "Synesthesia NRT Settings");
}

const TArray<FText>& UAudioSynesthesiaNRTSettings::GetAssetActionSubmenus() const
{
	static const TArray<FText> SubMenus
	{
		NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AssetSoundAnalysisSubmenu", "Analysis")
	};

	return SubMenus;
}

UClass* UAudioSynesthesiaNRTSettings::GetSupportedClass() const 
{
	return UAudioSynesthesiaNRTSettings::StaticClass();
}

FColor UAudioSynesthesiaNRTSettings::GetTypeColor() const
{
	return FColor(200.0f, 150.0f, 200.0f);
}

FText UAudioSynesthesiaNRT::GetAssetActionName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AssetSoundSynesthesiaNRT", "Synesthesia NRT");
}

const TArray<FText>& UAudioSynesthesiaNRT::GetAssetActionSubmenus() const
{
	static const TArray<FText> SubMenus
	{
		NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AssetSoundAnalysisSubmenu", "Analysis")
	};

	return SubMenus;
}

UClass* UAudioSynesthesiaNRT::GetSupportedClass() const
{
	return UAudioSynesthesiaNRT::StaticClass();
}

FColor UAudioSynesthesiaNRT::GetTypeColor() const
{
	return FColor(200.0f, 150.0f, 200.0f);
}

