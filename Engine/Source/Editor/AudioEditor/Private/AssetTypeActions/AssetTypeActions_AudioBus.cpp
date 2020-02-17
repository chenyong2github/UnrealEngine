// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_AudioBus.h"
#include "Misc/PackageName.h"
#include "AssetData.h"
#include "EditorStyleSet.h"
#include "Factories/AudioBusFactory.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "AudioEditorModule.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_AudioBus::GetSupportedClass() const
{
	return UAudioBus::StaticClass();
}

const TArray<FText>& FAssetTypeActions_AudioBus::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetAudioBusMenu", "Mix"))
	};

	return SubMenus;
}
#undef LOCTEXT_NAMESPACE
