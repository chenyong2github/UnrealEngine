// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

class FAssetTypeActions_SoundVolumeModulatorBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundVolumeModulatorBus", "Modulation Bus (Volume)"); }
	virtual FColor GetTypeColor() const override { return FColor(33, 183, 0); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
};

class FAssetTypeActions_SoundPitchModulatorBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundPitchModulatorBus", "Modulation Bus (Pitch)"); }
	virtual FColor GetTypeColor() const override { return FColor(181, 21, 0); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
};

class FAssetTypeActions_SoundLPFModulatorBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundLPFModulatorBus", "Modulation Bus (LPF)"); }
	virtual FColor GetTypeColor() const override { return FColor(0, 156, 183); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
};

class FAssetTypeActions_SoundHPFModulatorBus : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_SoundHPFModulatorBus", "Modulation Bus (HPF)"); }
	virtual FColor GetTypeColor() const override { return FColor(94, 237, 183); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Sounds; }
};
