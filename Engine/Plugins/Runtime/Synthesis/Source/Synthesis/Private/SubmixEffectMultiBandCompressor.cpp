// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmixEffects/SubmixEffectMultiBandCompressor.h"
#include "DSP/BufferVectorOperations.h"

void FSubmixEffectMultibandCompressor::Init(const FSoundEffectSubmixInitData& InitData)
{
	NumChannels = 0;
	SampleRate = InitData.SampleRate;

	AudioOutputFrame.Reset();
	MultiBandBuffer.Init(4, 2 * 1024);
}

void FSubmixEffectMultibandCompressor::OnPresetChanged()
{
	GET_EFFECT_SETTINGS(SubmixEffectMultibandCompressor);

	if (Settings.Bands.Num() == 0 || NumChannels == 0)
	{
		return;
	}

	bool bNeedsReinit = (Settings.Bands.Num() != PrevNumBands 
						 || Settings.Bands.Num() - 1 != PrevCrossovers.Num()
						 || Settings.bFourPole != bPrevFourPole
						 || bInitialized == false);

	// check if crossovers have changed
	// if so, update before potentially initializing
	bool bCrossoversChanged = false;
	for (int32 CrossoverId = 0; CrossoverId < PrevCrossovers.Num(); CrossoverId++)
	{
		float CrossoverFrequency = Settings.Bands[CrossoverId].CrossoverTopFrequency;
		if (CrossoverFrequency != PrevCrossovers[CrossoverId])
		{
			PrevCrossovers.Reset(Settings.Bands.Num());
			for (int32 BandId = 0; BandId < Settings.Bands.Num(); ++BandId)
			{
				PrevCrossovers.Add(Settings.Bands[BandId].CrossoverTopFrequency);
			}

			bCrossoversChanged = true;
			break;
		}
	}

	if (bNeedsReinit)
	{
		// only necessary when # of bands or filters is changed
		Initialize(Settings);
	}
	else if (bCrossoversChanged)
	{
		// lighter way to update crossovers if nothing else needs to be reinit
		BandSplitter.SetCrossovers(PrevCrossovers);
	}

	Audio::EDynamicsProcessingMode::Type TypeToSet;
	switch (Settings.DynamicsProcessorType)
	{
	default:
	case ESubmixEffectDynamicsProcessorType::Compressor:
		TypeToSet = Audio::EDynamicsProcessingMode::Compressor;
		break;

	case ESubmixEffectDynamicsProcessorType::Limiter:
		TypeToSet = Audio::EDynamicsProcessingMode::Limiter;
		break;

	case ESubmixEffectDynamicsProcessorType::Expander:
		TypeToSet = Audio::EDynamicsProcessingMode::Expander;
		break;

	case ESubmixEffectDynamicsProcessorType::Gate:
		TypeToSet = Audio::EDynamicsProcessingMode::Gate;
		break;
	}

	Audio::EPeakMode::Type PeakModeToSet;
	switch (Settings.PeakMode)
	{
	default:
	case ESubmixEffectDynamicsPeakMode::MeanSquared:
		PeakModeToSet = Audio::EPeakMode::MeanSquared;
		break;

	case ESubmixEffectDynamicsPeakMode::RootMeanSquared:
		PeakModeToSet = Audio::EPeakMode::RootMeanSquared;
		break;

	case ESubmixEffectDynamicsPeakMode::Peak:
		PeakModeToSet = Audio::EPeakMode::Peak;
		break;
	}

	for (int32 BandId = 0; BandId < DynamicsProcessors.Num(); ++BandId)
	{
		Audio::FDynamicsProcessor& DynamicsProcessor = DynamicsProcessors[BandId];

		Audio::EDynamicsProcessorChannelLinkMode ChannelLinkMode = Settings.bLinkChannels ?
			Audio::EDynamicsProcessorChannelLinkMode::Peak
			: Audio::EDynamicsProcessorChannelLinkMode::Disabled;

		DynamicsProcessor.SetChannelLinkMode(ChannelLinkMode);

		DynamicsProcessor.SetLookaheadMsec(Settings.LookAheadMsec);
		DynamicsProcessor.SetAnalogMode(Settings.bAnalogMode);

		DynamicsProcessor.SetAttackTime(Settings.Bands[BandId].AttackTimeMsec);
		DynamicsProcessor.SetReleaseTime(Settings.Bands[BandId].ReleaseTimeMsec);
		DynamicsProcessor.SetThreshold(Settings.Bands[BandId].ThresholdDb);
		DynamicsProcessor.SetRatio(Settings.Bands[BandId].Ratio);
		DynamicsProcessor.SetKneeBandwidth(Settings.Bands[BandId].KneeBandwidthDb);
		DynamicsProcessor.SetInputGain(Settings.Bands[BandId].InputGainDb);
		DynamicsProcessor.SetOutputGain(Settings.Bands[BandId].OutputGainDb);

		DynamicsProcessor.SetProcessingMode(TypeToSet);
		DynamicsProcessor.SetPeakMode(PeakModeToSet);
	}
}

void FSubmixEffectMultibandCompressor::Initialize(FSubmixEffectMultibandCompressorSettings& Settings)
{
	const int32 NumBands = Settings.Bands.Num();
	FrameSize = sizeof(float) * NumChannels;

	PrevCrossovers.Reset(NumBands - 1);
	for (int32 BandId = 0; BandId < NumBands - 1; BandId++)
	{
		PrevCrossovers.Add(Settings.Bands[BandId].CrossoverTopFrequency);
	}

	Audio::EFilterOrder CrossoverMode = Settings.bFourPole ? Audio::EFilterOrder::FourPole : Audio::EFilterOrder::TwoPole;
	BandSplitter.Init(NumChannels, SampleRate, CrossoverMode, PrevCrossovers);

	MultiBandBuffer.SetBands(NumBands);

	DynamicsProcessors.Reset(NumBands);
	DynamicsProcessors.AddDefaulted(NumBands);
	for (int32 BandId = 0; BandId < NumBands; ++BandId)
	{
		DynamicsProcessors[BandId].Init(SampleRate, NumChannels);
	}

	PrevNumBands = Settings.Bands.Num();
	bPrevFourPole = Settings.bFourPole;

	bInitialized = true;
}

void FSubmixEffectMultibandCompressor::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
	if (NumChannels != InData.NumChannels)
	{
		GET_EFFECT_SETTINGS(SubmixEffectMultibandCompressor);
		NumChannels = InData.NumChannels;

		Initialize(Settings);
		OnPresetChanged();
	}

	const int32 NumSamples = InData.NumFrames * NumChannels;

	if ((bInitialized == false) || (DynamicsProcessors.Num() == 0))
	{
		//passthrough
		FMemory::Memcpy(OutData.AudioBuffer->GetData(), InData.AudioBuffer->GetData(), FrameSize * InData.NumFrames);
		return;
	}

	const Audio::AlignedFloatBuffer& InBuffer = *InData.AudioBuffer;
	Audio::AlignedFloatBuffer& OutBuffer = *OutData.AudioBuffer;

	FMemory::Memzero(OutBuffer.GetData(), FrameSize * InData.NumFrames);

	Audio::FStackSampleBuffer ScratchBuffer;
	const int32 BlockSize = FMath::Min(ScratchBuffer.Max(), NumSamples);

	if (BlockSize > MultiBandBuffer.NumSamples)
	{
		MultiBandBuffer.SetSamples(NumSamples);
	}

	for (int32 SampleIdx = 0; SampleIdx < NumSamples; SampleIdx += BlockSize)
	{
		const float* InPtr = &InBuffer[SampleIdx];
		float* OutPtr = &OutBuffer[SampleIdx];

		ScratchBuffer.SetNumUninitialized(BlockSize);

		BandSplitter.ProcessAudioBuffer(InPtr, MultiBandBuffer, BlockSize / NumChannels);

		for (int32 Band = 0; Band < DynamicsProcessors.Num(); ++Band)
		{
			DynamicsProcessors[Band].ProcessAudio(MultiBandBuffer[Band], BlockSize, ScratchBuffer.GetData());
			Audio::MixInBufferFast(ScratchBuffer.GetData(), OutPtr, BlockSize);
		}
	}
}

void USubmixEffectMultibandCompressorPreset::SetSettings(const FSubmixEffectMultibandCompressorSettings& InSettings)
{
	UpdateSettings(InSettings);
}
