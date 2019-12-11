// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_AudioSynesthesiaNRT.h"
#include "Developer/AssetTools/Public/AssetTypeCategories.h"
#include "AudioSynesthesiaNRT.h"

FAssetTypeActions_AudioSynesthesiaNRT::FAssetTypeActions_AudioSynesthesiaNRT(UAudioSynesthesiaNRT* InSynesthesia)
	: Synesthesia(InSynesthesia)
{
}

FText FAssetTypeActions_AudioSynesthesiaNRT::GetName() const
{
	FText AssetActionName = Synesthesia->GetAssetActionName();
	if (AssetActionName.IsEmpty())
	{
		FString ClassName;
		Synesthesia->GetClass()->GetName(ClassName);
		return FText::FromString(ClassName);
	}
	else
	{
		return AssetActionName;
	}
}

const TArray<FText>& FAssetTypeActions_AudioSynesthesiaNRT::GetSubMenus() const
{
	return Synesthesia->GetAssetActionSubmenus();
}

FColor FAssetTypeActions_AudioSynesthesiaNRT::GetTypeColor() const 
{
	return Synesthesia->GetTypeColor(); 
}

UClass* FAssetTypeActions_AudioSynesthesiaNRT::GetSupportedClass() const
{
	UClass* SupportedClass = Synesthesia->GetSupportedClass();
	if (SupportedClass == nullptr)
	{
		return Synesthesia->GetClass();
	}
	else
	{
		return SupportedClass;
	}
}

uint32 FAssetTypeActions_AudioSynesthesiaNRT::GetCategories() 
{
	return EAssetTypeCategories::Sounds; 
}
