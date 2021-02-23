// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modes/PlacementModeSubsystem.h"

#include "AssetPlacementSettings.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Interfaces/TypedElementAssetDataInterface.h"
#include "Factories/AssetFactoryInterface.h"

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
		for (const FPaletteItem& Item : ModeSettings->PaletteItems)
		{
			if (ReferencedAssetDatas.Find(Item.AssetData) != INDEX_NONE)
			{
				return true;
			}
		}
	}

	// The current implementation of the asset data interface for actors requires that individual actors report on assets contained within their components.
	// Not all actors do this reliably, so additionally check the supplied factory for a match. 
	for (const FPaletteItem& Item : ModeSettings->PaletteItems)
	{
		FAssetData FoundAssetDataFromFactory = Item.FactoryOverride->GetAssetDataFromElementHandle(InElementToCheck);
		if (FoundAssetDataFromFactory == Item.AssetData)
		{
			return true;
		}
	}

	return false;
}

bool UPlacementModeSubsystem::AddPaletteItem(const FPaletteItem& InPaletteItem)
{
	if (ModeSettings && !ModeSettings->PaletteItems.FindByPredicate([InPaletteItem](const FPaletteItem& ItemIter) { return ItemIter.AssetData.ObjectPath == InPaletteItem.AssetData.ObjectPath; }))
	{
		ModeSettings->PaletteItems.Add(InPaletteItem);
		return true;
	}

	return false;
}

void UPlacementModeSubsystem::ClearPalette()
{
	ModeSettings->PaletteItems.Empty();
}
