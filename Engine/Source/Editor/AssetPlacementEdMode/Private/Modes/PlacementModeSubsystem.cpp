// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modes/PlacementModeSubsystem.h"

#include "AssetPlacementSettings.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Interfaces/TypedElementAssetDataInterface.h"
#include "Factories/AssetFactoryInterface.h"

#include "Subsystems/PlacementSubsystem.h"
#include "Editor.h"

void UPlacementModeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	ModeSettings = NewObject<UAssetPlacementSettings>(this);
	ModeSettings->LoadConfig();
}

void UPlacementModeSubsystem::Deinitialize()
{
	ModeSettings->SaveConfig();
	ModeSettings = nullptr;
}

const UAssetPlacementSettings* UPlacementModeSubsystem::GetModeSettingsObject() const
{
	return ModeSettings;
}

bool UPlacementModeSubsystem::DoesCurrentPaletteSupportElement(const FTypedElementHandle& InElementToCheck) const
{
	if (!ModeSettings)
	{
		return false;
	}

	if (TTypedElement<UTypedElementAssetDataInterface> AssetDataInterface = UTypedElementRegistry::GetInstance()->GetElement<UTypedElementAssetDataInterface>(InElementToCheck))
	{
		TArray<FAssetData> ReferencedAssetDatas = AssetDataInterface.GetAllReferencedAssetDatas();
		for (const TSharedPtr<FPaletteItem>& Item : ModeSettings->PaletteItems)
		{
			if (!Item)
			{
				continue;
			}

			if (ReferencedAssetDatas.Find(Item->AssetData) != INDEX_NONE)
			{
				return true;
			}
		}
	}

	// The current implementation of the asset data interface for actors requires that individual actors report on assets contained within their components.
	// Not all actors do this reliably, so additionally check the supplied factory for a match. 
	for (const TSharedPtr<FPaletteItem>& Item : ModeSettings->PaletteItems)
	{
		if (!Item)
		{
			continue;
		}

		FAssetData FoundAssetDataFromFactory = Item->AssetFactoryInterface->GetAssetDataFromElementHandle(InElementToCheck);
		if (FoundAssetDataFromFactory == Item->AssetData)
		{
			return true;
		}
	}

	return false;
}

TSharedPtr<FPaletteItem> UPlacementModeSubsystem::AddPaletteItem(const FAssetData& InAssetData)
{
	TSharedPtr<FPaletteItem> NewPaletteItem;
	if (!InAssetData.IsValid())
	{
		return NewPaletteItem;
	}

	if (InAssetData.GetClass()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_NotPlaceable))
	{
		return NewPaletteItem;
	}

	if (ModeSettings && !ModeSettings->PaletteItems.FindByPredicate([InAssetData](const TSharedPtr<FPaletteItem>& ItemIter) { return ItemIter ? (ItemIter->AssetData.ObjectPath == InAssetData.ObjectPath) : false; }))
	{
		if (UPlacementSubsystem* PlacementSubystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
		{
			if (TScriptInterface<IAssetFactoryInterface> AssetFactory = PlacementSubystem->FindAssetFactoryFromAssetData(InAssetData))
			{
				NewPaletteItem = MakeShared<FPaletteItem>();
				NewPaletteItem->AssetData = InAssetData;
				NewPaletteItem->AssetFactoryInterface = AssetFactory;
				ModeSettings->PaletteItems.Add(NewPaletteItem);
			}
		}
	}

	return NewPaletteItem;
}

void UPlacementModeSubsystem::ClearPalette()
{
	ModeSettings->PaletteItems.Empty();
}

void UPlacementModeSubsystem::SetUseContentBrowserAsPalette(bool bInUseContentBrowser)
{
	ModeSettings->bUseContentBrowserSelection = bInUseContentBrowser;
}
