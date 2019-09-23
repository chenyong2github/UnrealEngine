// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"


class FAssetTypeActions_SoundVolumeControlBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundVolumeControlBus", "Control Bus (Volume)"); }
	virtual FColor GetTypeColor() const override { return FColor(33, 183, 0); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }

	virtual const TArray<FText>& GetSubMenus() const override;
};

class FAssetTypeActions_SoundPitchControlBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundPitchControlBus", "Control Bus (Pitch)"); }
	virtual FColor GetTypeColor() const override { return FColor(181, 21, 0); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }

	virtual const TArray<FText>& GetSubMenus() const override;
};

class FAssetTypeActions_SoundLPFControlBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundLPFControlBus", "Control Bus (LPF)"); }
	virtual FColor GetTypeColor() const override { return FColor(0, 156, 183); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }

	virtual const TArray<FText>& GetSubMenus() const override;
};

class FAssetTypeActions_SoundHPFControlBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundHPFControlBus", "Control Bus (HPF)"); }
	virtual FColor GetTypeColor() const override { return FColor(94, 237, 183); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }

	virtual const TArray<FText>& GetSubMenus() const override;
};
