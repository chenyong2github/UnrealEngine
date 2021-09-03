// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementSettings.h"
#include "PlacementPaletteAsset.h"
#include "PackageTools.h"
#include "PlacementPaletteItem.h"

bool UAssetPlacementSettings::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	const FName PropertyName = InProperty->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeScale))
	{
		return bUseRandomScale;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeRotationX))
	{
		return bUseRandomRotationX;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeRotationY))
	{
		return bUseRandomRotationY;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bAllowNegativeRotationZ))
	{
		return bUseRandomRotationZ;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAssetPlacementSettings, bInvertNormalAxis))
	{
		return bAlignToNormal;
	}

	return true;
}

void UAssetPlacementSettings::SetPaletteAsset(UPlacementPaletteAsset* InPaletteAsset)
{
	if (!InPaletteAsset)
	{
		ActivePalette = UserPalette;
	}
	else
	{
		ActivePalette = InPaletteAsset;
	}

	LastActivePalettePath = FSoftObjectPath(InPaletteAsset);
}

void UAssetPlacementSettings::AddItemToActivePalette(const FPaletteItem& InPaletteItem)
{
	ActivePalette->Modify();
	ActivePalette->PaletteItems.Add(InPaletteItem);
}

TArrayView<const FPaletteItem> UAssetPlacementSettings::GetActivePaletteItems() const
{
	return ActivePalette ? MakeArrayView(ActivePalette->PaletteItems) : TArrayView<const FPaletteItem>();
}

FSoftObjectPath UAssetPlacementSettings::GetActivePalettePath() const
{
	return FSoftObjectPath(ActivePalette);
}

const FGuid UAssetPlacementSettings::GetActivePaletteGuid() const
{
	if (ActivePalette)
	{
		return ActivePalette->GridGuid;
	}

	return FGuid();
}

void UAssetPlacementSettings::ClearActivePaletteItems()
{
	ActivePalette->Modify();
	ActivePalette->PaletteItems.Empty();
}

void UAssetPlacementSettings::LoadSettings()
{
	LoadConfig();

	UserPalette = NewObject<UPlacementPaletteAsset>(this);
	if (!UserGridGuid.IsValid())
	{
		FGuid::Parse(FPlatformMisc::GetLoginId(), UserGridGuid);
	}

	UserPalette->GridGuid = UserGridGuid;

	ActivePalette = Cast<UPlacementPaletteAsset>(LastActivePalettePath.TryLoad());
	if (!ActivePalette)
	{
		ActivePalette = UserPalette;
	}
}

void UAssetPlacementSettings::SaveActivePalette()
{
	if (ActivePalette != UserPalette)
	{
		UPackageTools::SavePackagesForObjects(TArray<UObject*>({ ActivePalette }));
	}
	else
	{
		UserPalette->SaveConfig();
	}
}

void UAssetPlacementSettings::SaveSettings()
{
	UPackageTools::SavePackagesForObjects(TArray<UObject*>({ActivePalette}));
	UserPalette->SaveConfig();
	SaveConfig();
}
