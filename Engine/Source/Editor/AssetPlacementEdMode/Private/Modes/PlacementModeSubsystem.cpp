// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modes/PlacementModeSubsystem.h"

#include "AssetPlacementSettings.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Interfaces/TypedElementAssetDataInterface.h"
#include "Factories/AssetFactoryInterface.h"
#include "PlacementPaletteAsset.h"
#include "PlacementPaletteItem.h"

#include "Subsystems/PlacementSubsystem.h"
#include "Editor.h"

void UPlacementModeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	ModeSettings = NewObject<UAssetPlacementSettings>(this);
	ModeSettings->LoadSettings();

	FCoreDelegates::OnEnginePreExit.AddUObject(this, &UPlacementModeSubsystem::SaveSettings);
}

void UPlacementModeSubsystem::Deinitialize()
{
	ModeSettings = nullptr;
}

const UAssetPlacementSettings* UPlacementModeSubsystem::GetModeSettingsObject() const
{
	return ModeSettings;
}

UAssetPlacementSettings* UPlacementModeSubsystem::GetMutableModeSettingsObject()
{
	return ModeSettings;
}

bool UPlacementModeSubsystem::DoesActivePaletteSupportElement(const FTypedElementHandle& InElementToCheck) const
{
	if (TTypedElement<UTypedElementAssetDataInterface> AssetDataInterface = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementAssetDataInterface>(InElementToCheck))
	{
		TArray<FAssetData> ReferencedAssetDatas = AssetDataInterface.GetAllReferencedAssetDatas();
		for (const FPaletteItem& Item : ModeSettings->GetActivePaletteItems())
		{
			if (ReferencedAssetDatas.FindByPredicate([&Item](const FAssetData& ReferencedAssetData){ return (ReferencedAssetData.ToSoftObjectPath() == Item.AssetPath); }))
			{
				return true;
			}

			// The current implementation of the asset data interface for actors requires that individual actors report on assets contained within their components.
			// Not all actors do this reliably, so additionally check the supplied factory for a match. 
			if (!Item.AssetFactoryInterface)
			{
				continue;
			}

			FAssetData FoundAssetDataFromFactory = Item.AssetFactoryInterface->GetAssetDataFromElementHandle(InElementToCheck);
			if (FoundAssetDataFromFactory.ToSoftObjectPath() == Item.AssetPath)
			{
				return true;
			}
		}
	}

	return false;
}

FPaletteItem UPlacementModeSubsystem::CreatePaletteItem(const FAssetData& InAssetData)
{
	FPaletteItem NewPaletteItem;
	if (!InAssetData.IsValid())
	{
		return NewPaletteItem;
	}

	if (InAssetData.GetClass()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_NotPlaceable))
	{
		return NewPaletteItem;
	}

	if (!ModeSettings->GetActivePaletteItems().FindByPredicate([InAssetData](const FPaletteItem& ItemIter) { return (ItemIter.AssetPath == InAssetData.ToSoftObjectPath()); }))
	{
		if (UPlacementSubsystem* PlacementSubystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
		{
			if (TScriptInterface<IAssetFactoryInterface> AssetFactory = PlacementSubystem->FindAssetFactoryFromAssetData(InAssetData))
			{
				NewPaletteItem.ItemGuid = FGuid::NewGuid();
				NewPaletteItem.AssetPath = InAssetData.ToSoftObjectPath();
				NewPaletteItem.AssetFactoryInterface = AssetFactory;
				ModeSettings->AddItemToActivePalette(NewPaletteItem);
			}
		}
	}

	return NewPaletteItem;
}

void UPlacementModeSubsystem::SetUseContentBrowserAsPalette(bool bInUseContentBrowser)
{
	ModeSettings->bUseContentBrowserSelection = bInUseContentBrowser;
}

void UPlacementModeSubsystem::SaveSettings() const
{
	if (ModeSettings)
	{
		ModeSettings->SaveSettings();
	}
}
