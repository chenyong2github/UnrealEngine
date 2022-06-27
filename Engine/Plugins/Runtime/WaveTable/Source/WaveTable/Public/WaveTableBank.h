// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Algo/Transform.h"
#include "IAudioProxyInitializer.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectSaveContext.h"
#include "WaveTable.h"
#include "WaveTableTransform.h"

#include "WaveTableBank.generated.h"


USTRUCT()
struct WAVETABLE_API FWaveTableBankEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Table)
	FWaveTableTransform Transform;
};

UCLASS(config = Engine, editinlinenew, BlueprintType)
class WAVETABLE_API UWaveTableBank : public UObject, public IAudioProxyDataFactory
{
	GENERATED_BODY()

public:
	// If set to 'None', WaveTable is not cached.  If set to
	// value, caches a sampled wavetable for all curves.
	UPROPERTY(EditAnywhere, Category = Options)
	EWaveTableResolution Resolution;

	// Determines if output from curve/wavetable are to be clamped between [-1.0f, 1.0f]
	// (i.e. for waveform generation, oscillation, etc.) or between [0.0f, 1.0f]
	// (i.e. for enveloping) when sampling curve/wavetable
	UPROPERTY(EditAnywhere, Category = Options)
	bool bBipolar = true;

	/** Tables within the given bank */
	UPROPERTY(EditAnywhere, Category = Options)
	TArray<FWaveTableBankEntry> Entries;

	/* IAudioProxyDataFactory Implementation */
	virtual TUniquePtr<Audio::IProxyData> CreateNewProxyData(const Audio::FProxyDataInitParams& InitParams) override;

#if WITH_EDITOR
	virtual void PreSave(FObjectPreSaveContext InSaveContext) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent) override;
#endif // WITH_EDITOR
};

class WAVETABLE_API FWaveTableBankAssetProxy : public Audio::TProxyData<FWaveTableBankAssetProxy>, public TSharedFromThis<FWaveTableBankAssetProxy, ESPMode::ThreadSafe>
{
public:
	IMPL_AUDIOPROXY_CLASS(FWaveTableBankAssetProxy);

	FWaveTableBankAssetProxy(const FWaveTableBankAssetProxy& InAssetProxy)
		: WaveTables(InAssetProxy.WaveTables)
	{
	}

	FWaveTableBankAssetProxy(const UWaveTableBank& InWaveTableBank)
	{
		Algo::Transform(InWaveTableBank.Entries, WaveTables, [](const FWaveTableBankEntry& Entry)
		{
			return Entry.Transform.WaveTable;
		});
	}

	virtual Audio::IProxyDataPtr Clone() const
	{
		return MakeUnique<FWaveTableBankAssetProxy>(*this);
	}

	virtual const TArray<WaveTable::FWaveTable>& GetWaveTables() const
	{
		return WaveTables;
	}

protected:
	TArray<WaveTable::FWaveTable> WaveTables;
};
using FWaveTableBankAssetProxyPtr = TSharedPtr<FWaveTableBankAssetProxy, ESPMode::ThreadSafe>;
