// Copyright Epic Games, Inc. All Rights Reserved.
#include "WaveTableBank.h"

#define LOCTEXT_NAMESPACE "WaveTable"


TUniquePtr<Audio::IProxyData> UWaveTableBank::CreateNewProxyData(const Audio::FProxyDataInitParams& InitParams)
{
	return MakeUnique<FWaveTableBankAssetProxy>(*this);
}

#if WITH_EDITOR
void UWaveTableBank::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	FProperty* Property = InPropertyChangedEvent.Property;
	if (Property && InPropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		for (FWaveTableBankEntry& Entry : Entries)
		{
			Entry.Transform.CacheWaveTable(Resolution, bBipolar);
		}
	}

	Super::PostEditChangeProperty(InPropertyChangedEvent);
}

void UWaveTableBank::PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent)
{
	FProperty* Property = InPropertyChangedEvent.Property;
	if (Property && InPropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		for (FWaveTableBankEntry& Entry : Entries)
		{
			Entry.Transform.CacheWaveTable(Resolution, bBipolar);
		}
	}

	Super::PostEditChangeChainProperty(InPropertyChangedEvent);
}

void UWaveTableBank::PreSave(FObjectPreSaveContext InSaveContext)
{
	if (!InSaveContext.IsCooking())
	{
		for (FWaveTableBankEntry& Entry : Entries)
		{
			Entry.Transform.CacheWaveTable(Resolution, bBipolar);
		}
	}

	Super::PreSave(InSaveContext);
}
#endif // WITH_EDITOR
#undef LOCTEXT_NAMESPACE // WaveTable
