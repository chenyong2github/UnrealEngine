// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ConvolutionReverbComponent.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/ConvolutionAlgorithm.h"
#include "DSP/Dsp.h"

FConvolutionReverbSettings::FConvolutionReverbSettings()
	: ImpulseResponse(nullptr)
	, AllowHardwareAcceleration(true)
	, PostNormalizationVolume_decibels(-24.0f)
	, SurroundRearChannelBleedAmount(0.0f)
	, bSurroundRearChannelFlip(false)
{
}

FConvolutionReverb::FConvolutionReverb()
	: CachedIrAssetPtr(nullptr)
	, bIrIsDirty(false)
	, bSettingsAreDirty(false)
	, bIrBeingRebuilt(false)
	, SampleRate(0.0f)
	, CachedOutputGain(1.0f)
	, RearChanBleed(0.0f)
	, bRearChanFlip(false)
	, bCachedEnableHardwareAcceleration(true)
{
	
}

FConvolutionReverb::~FConvolutionReverb()
{
	UImpulseResponse* IrPtr = CachedIrAssetPtr.Get();
	if (IrPtr)
	{
		IrPtr->UnsubscribeFlagFromIrChanges(&bIrIsDirty);
	}
}

void FConvolutionReverb::Init(const FSoundEffectSubmixInitData& InitData)
{
	// Do any initializations here
	SampleRate = InitData.SampleRate;
	bSettingsAreDirty = true;
}

void FConvolutionReverb::OnPresetChanged()
{
	GET_EFFECT_SETTINGS(ConvolutionReverb);
	bIrBeingRebuilt = false;

	// SURROUND
	RearChanBleed = Settings.SurroundRearChannelBleedAmount;
	bRearChanFlip = Settings.bSurroundRearChannelFlip;

	// OUTPUT GAIN
	const float NewOutputGain = Audio::ConvertToLinear(Settings.PostNormalizationVolume_decibels);
	if (!FMath::IsNearlyEqual(CachedOutputGain, NewOutputGain))
	{
		CachedOutputGain = NewOutputGain;
		bSettingsAreDirty = true;
	}

	// HARDWARE ACCELERATION
	if (bCachedEnableHardwareAcceleration != Settings.AllowHardwareAcceleration)
	{
		bCachedEnableHardwareAcceleration = Settings.AllowHardwareAcceleration;
		bSettingsAreDirty = true;
	}


	// IR ASSET UPDATE
	UImpulseResponse* NewIrAssetPtr = Settings.ImpulseResponse;
	UImpulseResponse* IrAssetPtrOld = CachedIrAssetPtr.Get();

	// IR Asset hasn't been changed
	if (NewIrAssetPtr == IrAssetPtrOld)
	{
		return;
	}
	// IR Asset was removed/deleted
	else if (!NewIrAssetPtr)
	{
		if (IrAssetPtrOld)
		{
			IrAssetPtrOld->UnsubscribeFlagFromIrChanges(&bIrIsDirty);
		}
		ConvolutionAlgorithm.Reset();
		CachedIrAssetPtr = NewIrAssetPtr;
	}
	else // received new IR Asset, swap subscriptions and update weak ptr
	{
		bIrIsDirty.AtomicSet(false);
		if (IrAssetPtrOld)
		{
			IrAssetPtrOld->UnsubscribeFlagFromIrChanges(&bIrIsDirty);
		}

		NewIrAssetPtr->SubscribeFlagToIrChanges(&bIrIsDirty);

		CachedIrAssetPtr = MakeWeakObjectPtr(NewIrAssetPtr);

		// will prepare IR data async if its not already ready.
		// IR asset will set to true when the data is ready
		bIrBeingRebuilt = true;
		NewIrAssetPtr->PrepareIRsForConvolutionAlgorithm(SampleRate);
	}
}

void FConvolutionReverb::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
	// Here, we get min of num channels of input and output (note that submix processing output channels can be different than input)
	const int32 NumChannels = FMath::Min(InData.NumChannels, OutData.NumChannels);

	const float* InputBuffer = InData.AudioBuffer->GetData();
	float* OutputBuffer = OutData.AudioBuffer->GetData();

	const int32 InputBufferSize = InData.AudioBuffer->Num();
	const int32 OutputBufferSize = OutData.AudioBuffer->Num();
	const int32 NumFrames = InData.NumFrames;
	const int32 NumInputChannels = InData.NumChannels;
	const int32 NumOutputChannels = OutData.NumChannels;
	const int32 NumInputSamples = InData.AudioBuffer->Num();
	const int32 NumOutputSamples = OutData.AudioBuffer->Num();


	// Do we need to update the Convolution object?
	// the IR Asset itself may have raised bIrIsDirty flag if it was async prepping the IR data
	if (bSettingsAreDirty || (bIrBeingRebuilt && bIrIsDirty))
	{
		// get IR data
		if (auto IrPtr = CachedIrAssetPtr.Get())
		{
			const Audio::AlignedFloatBuffer& IrDataRef = IrPtr->GetDeinterleavedIRData();

			// build new settings struct
			Audio::FConvolutionSettings NewSettings;

			const int32 NumIrChannels = IrPtr->GetNumChannels();
			NewSettings.bEnableHardwareAcceleration = bCachedEnableHardwareAcceleration;
			NewSettings.BlockNumSamples = NumFrames;
			NewSettings.MaxNumImpulseResponseSamples = IrDataRef.Num() / NumIrChannels;
			NewSettings.NumImpulseResponses = NumIrChannels;
			NewSettings.NumInputChannels = NumInputChannels;
			NewSettings.NumOutputChannels = NumOutputChannels;

			// update settings and IR data
			if (bIrBeingRebuilt && bIrIsDirty)
			{
				ConvolutionAlgorithm.UpdateConvolutionObject(NewSettings, &IrDataRef);

				bIrIsDirty.AtomicSet(false);
				bIrBeingRebuilt = false;
				bSettingsAreDirty = false;
			}
			else
			{
				// Update settings only
				ConvolutionAlgorithm.UpdateConvolutionObject(NewSettings);
				bSettingsAreDirty = false;
			}
		}
	}


	// generate output audio
	ConvolutionAlgorithm.ProcessAudio(NumInputChannels, *InData.AudioBuffer, NumOutputChannels, *OutData.AudioBuffer);

	const bool bIsSurroundOutput = (NumOutputChannels >= 4);
	const bool bHasSurroundBleed = (!FMath::IsNearlyZero(RearChanBleed));
	const float G = FMath::Sqrt(1.0f/2.0f);


	float FinalOutputGain = CachedOutputGain;
	
	// Modify the output level if we are using surround bleed.
	// The gain scalar is applied to compensate for louder output
	// if our surround output signal is folded down to stereo
	if (bIsSurroundOutput && bHasSurroundBleed && bRearChanFlip)
	{
		FinalOutputGain *= G;
	}
	else if (bIsSurroundOutput && bHasSurroundBleed)
	{
		FinalOutputGain *= 0.5f;
	}

	Audio::MultiplyBufferByConstantInPlace(*OutData.AudioBuffer, FinalOutputGain);

	// surround bleed?
	if (bIsSurroundOutput && bHasSurroundBleed)
	{
		float* OutPtr = OutData.AudioBuffer->GetData();

		const int32 OutputFrameOffset = NumOutputChannels - 2;

		if (!bRearChanFlip)
		{
			for (int32 i = 0; i < NumOutputSamples; i += NumOutputChannels)
			{
				const int32 LeftRear = i + OutputFrameOffset;
				OutPtr[LeftRear] = RearChanBleed * OutPtr[i];
				OutPtr[LeftRear + 1] = RearChanBleed * OutPtr[i + 1];
			}
		}
		else
		{
			for (int32 i = 0; i < NumOutputSamples; i += NumOutputChannels)
			{
				const int32 LeftRear = i + OutputFrameOffset;
				OutPtr[LeftRear + 1] = RearChanBleed * OutPtr[i];
				OutPtr[LeftRear] = RearChanBleed * OutPtr[i + 1];
			}
		}
	}
}

uint32 FConvolutionReverb::GetDesiredInputChannelCountOverride() const
{
	return 2;
}

void UConvolutionReverbPreset::SetSettings(const FConvolutionReverbSettings& InSettings)
{
	UpdateSettings(InSettings);
}
