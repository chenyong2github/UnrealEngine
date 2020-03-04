// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Engine/EngineTypes.h"
#include "Engine/DataTable.h"
#include "AudioEffect.h"
#include "ImpulseResponseAsset.generated.h"


// ========================================================================
// ImpulseResponesConverter:
//
// Asynchronously prepare IR data for Audio Render Thread
// It owns no data.
// It is owned by UImpulseResponse asset, so it can safely assume it
//	hasn't been GC'd.
// ========================================================================
class UImpulseResponse;

class SYNTHESIS_API ImpulseResponesConverter : public FNonAbandonableTask
{
public:
	ImpulseResponesConverter(UImpulseResponse* InIrAsset, int32 InSampleRate)
		: IrAssetPtr(InIrAsset)
		, TargetSampleRate(InSampleRate)
	{
	}

private:
	void DoWork();

	// Pointer to the UImpulseResponse asset
	UImpulseResponse* IrAssetPtr;

	// needed to perform SRC in this task
	int32 TargetSampleRate;

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(ImpulseResponesConverter, STATGROUP_ThreadPoolAsyncTasks);
	}

	friend class FAsyncTask<ImpulseResponesConverter>;
};


// ========================================================================
// UImpulseResponse
// UAsset used to represent Imported Impulse Responses
// ========================================================================
UCLASS()
class SYNTHESIS_API UImpulseResponse : public UObject
{
	GENERATED_BODY()

public:

	UImpulseResponse();

	~UImpulseResponse();

	void SubscribeFlagToIrChanges(FThreadSafeBool* ExternalThreadSafeBool);

	void UnsubscribeFlagFromIrChanges(FThreadSafeBool* ExternalThreadSafeBool);

	const Audio::AlignedFloatBuffer& GetDeinterleavedIRData() const { return DeinterleavedIR; }

	// If the data needs de-interleave/SRC, it will do so in an async task
	// The async task will call PrepareIRsForConvolutionAlgorithmInternal() on another thread...
	// and call SetSubscriberFlags(true) when it is done.
	// If the data is already ready, SetSubscriberFlags(true) will be called synchronously
	void PrepareIRsForConvolutionAlgorithm(const int32 InSampleRate);

	// returns true if the cache for the Convolution Algorithm object is already warm (no async task needed)
	// reset task pointer if it is pointing to an old task
	bool IsDataPrepared(const int32 InSampleRate);

	// functions that get info for convolution algorithm based in IR format
	int32 GetNumChannels() { return NumChannels; }

	int32 GetNumFrames() { return NumFrames; }

protected:
	void PrepareIRsForConvolutionAlgorithmInternal(const int32 InSampleRate);

	void SetSubscriberFlags(bool bInDataIsReady);

	void PerformAssetIrSRC(int32 InSampleRate);

	// flip these to true when new IR data is ready
	TArray<FThreadSafeBool*> SubscriberFlags;

	FCriticalSection SubscriberFlagsCritSec;

	TUniquePtr<FAsyncTask<ImpulseResponesConverter>> TaskPtr;

	// Buffer that gets copied by the ConvolutionAlgorithmWrapper
	Audio::AlignedFloatBuffer DeinterleavedIR;

	// Sample rate of the DeinteleavedIR buffer
	int32 CurrentSampleRate{ 0 };

	// data about the IR asset (filled out by the factory)
	UPROPERTY()
	TArray<float> IRData;

	UPROPERTY()
	int32 NumChannels;

	UPROPERTY()
	int32 NumFrames;

	// sample rate of the SERIALIZED data, not unnecessarily the SR of current project
	UPROPERTY()
	int32 AssetSampleRate;

	friend class UImpulseResponseFactory; // allow factory to fill the internal buffer
	friend class ImpulseResponesConverter; // allow async worker to raise flags upon completion
};