// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "AudioModulationStyle.h"


class FAssetTypeActions_SoundVolumeControlBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundVolumeControlBus", "Control Bus (Volume)"); }
	virtual FColor GetTypeColor() const override { return FAudioModulationStyle::GetVolumeBusColor(); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }

	virtual const TArray<FText>& GetSubMenus() const override;
};

class FAssetTypeActions_SoundPitchControlBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundPitchControlBus", "Control Bus (Pitch)"); }
	virtual FColor GetTypeColor() const override { return FAudioModulationStyle::GetPitchBusColor(); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }

	virtual const TArray<FText>& GetSubMenus() const override;
};

class FAssetTypeActions_SoundLPFControlBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundLPFControlBus", "Control Bus (LPF)"); }
	virtual FColor GetTypeColor() const override { return FAudioModulationStyle::GetLPFBusColor(); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }

	virtual const TArray<FText>& GetSubMenus() const override;
};

class FAssetTypeActions_SoundHPFControlBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundHPFControlBus", "Control Bus (HPF)"); }
	virtual FColor GetTypeColor() const override { return FAudioModulationStyle::GetHPFBusColor(); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }

	virtual const TArray<FText>& GetSubMenus() const override;
};

class FAssetTypeActions_SoundControlBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundControlBus", "Control Bus"); }
	virtual FColor GetTypeColor() const override { return FAudioModulationStyle::GetControlBusColor(); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }

	virtual const TArray<FText>& GetSubMenus() const override;
};
