// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_AudioSynesthesiaNRTSettings.h"
#include "Developer/AssetTools/Public/AssetTypeCategories.h"
#include "AudioSynesthesiaNRT.h"

FAssetTypeActions_AudioSynesthesiaNRTSettings::FAssetTypeActions_AudioSynesthesiaNRTSettings(UAudioSynesthesiaNRTSettings* InSynesthesiaSettings)
	: SynesthesiaSettings(InSynesthesiaSettings)
{
}

FText FAssetTypeActions_AudioSynesthesiaNRTSettings::GetName() const
{
	FText AssetActionName = SynesthesiaSettings->GetAssetActionName();
	if (AssetActionName.IsEmpty())
	{
		FString ClassName;
		SynesthesiaSettings->GetClass()->GetName(ClassName);
		return FText::FromString(ClassName);
	}
	else
	{
		return AssetActionName;
	}
}

FColor FAssetTypeActions_AudioSynesthesiaNRTSettings::GetTypeColor() const 
{
	return SynesthesiaSettings->GetTypeColor();
}

const TArray<FText>& FAssetTypeActions_AudioSynesthesiaNRTSettings::GetSubMenus() const
{
	return SynesthesiaSettings->GetAssetActionSubmenus();
}

UClass* FAssetTypeActions_AudioSynesthesiaNRTSettings::GetSupportedClass() const
{
	UClass* SupportedClass = SynesthesiaSettings->GetSupportedClass();
	if (SupportedClass == nullptr)
	{
		return SynesthesiaSettings->GetClass();
	}
	else
	{
		return SupportedClass;
	}
}

uint32 FAssetTypeActions_AudioSynesthesiaNRTSettings::GetCategories() 
{
	return EAssetTypeCategories::Sounds; 
}

