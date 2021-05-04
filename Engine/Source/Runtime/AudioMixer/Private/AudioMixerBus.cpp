// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerBus.h"
#include "AudioMixerSourceManager.h"
#include "DSP/BufferVectorOperations.h"

namespace Audio
{
	FMixerAudioBus::FMixerAudioBus(FMixerSourceManager* InSourceManager, bool bInIsAutomatic, int32 InNumChannels)
		: CurrentBufferIndex(1)
		, NumChannels(InNumChannels)
		, NumFrames(InSourceManager->GetNumOutputFrames())
		, SourceManager(InSourceManager)
		, bIsAutomatic(bInIsAutomatic)
	{
		SetNumOutputChannels(NumChannels);

		// Make a patch bus input to push mixed audio sent to audio bus from source manager
		AudioBusInput = PatchMixerSplitter.AddNewInput(4096, 1.0f);
	}

	void FMixerAudioBus::SetNumOutputChannels(int32 InNumOutputChannels)
	{
		NumChannels = InNumOutputChannels;
		const int32 NumSamples = NumChannels * NumFrames;
		for (int32 i = 0; i < 2; ++i)
		{
			MixedSourceData[i].Reset();
			MixedSourceData[i].AddZeroed(NumSamples);
		}
	}

	void FMixerAudioBus::Update()
	{
		CurrentBufferIndex = !CurrentBufferIndex;
	}

	void FMixerAudioBus::AddInstanceId(const int32 InSourceId, int32 InNumOutputChannels)
	{
		InstanceIds.Add(InSourceId);
	}

	bool FMixerAudioBus::RemoveInstanceId(const int32 InSourceId)
	{
		InstanceIds.Remove(InSourceId);

		// Return true if there is no more instances or sends
		return bIsAutomatic && !InstanceIds.Num() && !AudioBusSends[(int32)EBusSendType::PreEffect].Num() && !AudioBusSends[(int32)EBusSendType::PostEffect].Num();
	}

	void FMixerAudioBus::AddSend(EBusSendType BusSendType, const FAudioBusSend& InAudioBusSend)
	{
		// Make sure we don't have duplicates in the bus sends
		for (FAudioBusSend& BusSend : AudioBusSends[(int32)BusSendType])
		{
			// If it's already added, just update the send level
			if (BusSend.SourceId == InAudioBusSend.SourceId)
			{
				BusSend.SendLevel = InAudioBusSend.SendLevel;
				return;
			}
		}

		// It's a new source id so just add it
		AudioBusSends[(int32)BusSendType].Add(InAudioBusSend);
	}

	bool FMixerAudioBus::RemoveSend(EBusSendType BusSendType, const int32 InSourceId)
	{
		TArray<FAudioBusSend>& Sends = AudioBusSends[(int32)BusSendType];

		for (int32 i = Sends.Num() - 1; i >= 0; --i)
		{
			// Remove this source id's send
			if (Sends[i].SourceId == InSourceId)
			{
				Sends.RemoveAtSwap(i, 1, false);

				// There will only be one entry
				break;
			}
		}

		// Return true if there is no more instances or sends and this is an automatic audio bus
		return bIsAutomatic && !InstanceIds.Num() && !AudioBusSends[(int32)EBusSendType::PreEffect].Num() && !AudioBusSends[(int32)EBusSendType::PostEffect].Num();
	}

	void FMixerAudioBus::MixBuffer()
	{
		// Reset and zero the mixed source data buffer for this bus
		const int32 NumSamples = NumFrames * NumChannels;

		MixedSourceData[CurrentBufferIndex].Reset();
		MixedSourceData[CurrentBufferIndex].AddZeroed(NumSamples);

		float* BusDataBufferPtr = MixedSourceData[CurrentBufferIndex].GetData();
		const int32 NumOutputFrames = SourceManager->GetNumOutputFrames();

		for (int32 BusSendType = 0; BusSendType < (int32)EBusSendType::Count; ++BusSendType)
		{
			// Loop through the send list for this bus
			for (const FAudioBusSend& AudioBusSend : AudioBusSends[BusSendType])
			{
				const float* SourceBufferPtr = nullptr;

				// If the audio source mixing to this audio bus is itself a source bus, we need to use the previous renderer buffer to avoid infinite recursion
				if (SourceManager->IsSourceBus(AudioBusSend.SourceId))
				{
					SourceBufferPtr = SourceManager->GetPreviousSourceBusBuffer(AudioBusSend.SourceId);
				}
				// If the source mixing into this is not itself a bus, then simply mix the pre-attenuation audio of the source into the bus
				// The source will have already computed its buffers for this frame
				else if (BusSendType == (int32)EBusSendType::PostEffect)
				{
					SourceBufferPtr = SourceManager->GetPreDistanceAttenuationBuffer(AudioBusSend.SourceId);
				}
				else
				{
					SourceBufferPtr = SourceManager->GetPreEffectBuffer(AudioBusSend.SourceId);
				}

				// It's possible we may not have a source buffer ptr here, so protect against it
				if (ensure(SourceBufferPtr))
				{
					const int32 NumSourceChannels = SourceManager->GetNumChannels(AudioBusSend.SourceId);
					const int32 NumSourceSamples = NumSourceChannels * NumOutputFrames;

					// If source channels are different than bus channels, we need to up-mix or down-mix
					if (NumSourceChannels != NumChannels)
					{
						Audio::FAlignedFloatBuffer ChannelMap;
						SourceManager->Get2DChannelMap(AudioBusSend.SourceId, NumChannels, ChannelMap);

						Audio::FAlignedFloatBuffer DownmixedBuffer;
						DownmixedBuffer.AddUninitialized(NumOutputFrames * NumChannels);
						Audio::DownmixBuffer(NumSourceChannels, NumChannels, SourceBufferPtr, DownmixedBuffer.GetData(), NumOutputFrames, ChannelMap.GetData());
						Audio::MixInBufferFast(DownmixedBuffer.GetData(), BusDataBufferPtr, DownmixedBuffer.Num(), AudioBusSend.SendLevel);
					}
					else
					{
						Audio::MixInBufferFast(SourceBufferPtr, BusDataBufferPtr, NumOutputFrames * NumChannels, AudioBusSend.SendLevel);
					}

				}
			}
		}

		// Push the mixed data to the patch splitter
		AudioBusInput.PushAudio(BusDataBufferPtr, NumOutputFrames * NumChannels);

		// Process the audio in the patch mixer splitter
		PatchMixerSplitter.ProcessAudio();
	}

	void FMixerAudioBus::CopyCurrentBuffer(Audio::FAlignedFloatBuffer& InChannelMap, int32 InNumOutputChannels, FAlignedFloatBuffer& OutBuffer, int32 NumOutputFrames) const
	{
		const float* RESTRICT CurrentBuffer = GetCurrentBusBuffer();

		check(NumChannels != InNumOutputChannels);
		Audio::FAlignedFloatBuffer DownmixedBuffer;
		DownmixedBuffer.AddUninitialized(NumOutputFrames * NumChannels);

		Audio::DownmixBuffer(NumChannels, InNumOutputChannels, CurrentBuffer, DownmixedBuffer.GetData(), NumOutputFrames, InChannelMap.GetData());

		Audio::MixInBufferFast(DownmixedBuffer.GetData(), OutBuffer.GetData(), DownmixedBuffer.Num(), 1.0f);
	}

	void FMixerAudioBus::CopyCurrentBuffer(int32 InNumOutputChannels, FAlignedFloatBuffer& OutBuffer, int32 NumOutputFrames) const
	{
		const float* RESTRICT CurrentBuffer = GetCurrentBusBuffer();

		check(NumChannels == InNumOutputChannels);

		FMemory::Memcpy(OutBuffer.GetData(), CurrentBuffer, sizeof(float) * NumOutputFrames * InNumOutputChannels);
	}

	const float* FMixerAudioBus::GetCurrentBusBuffer() const
	{
		return MixedSourceData[CurrentBufferIndex].GetData();
	}

	const float* FMixerAudioBus::GetPreviousBusBuffer() const
	{
		return MixedSourceData[!CurrentBufferIndex].GetData();
	}

	void FMixerAudioBus::AddNewPatchOutput(FPatchOutputStrongPtr& InPatchOutputStrongPtr)
	{
		PatchMixerSplitter.AddNewOutput(InPatchOutputStrongPtr);
	}

	void FMixerAudioBus::AddNewPatchInput(FPatchInput& InPatchInput)
	{
		return PatchMixerSplitter.AddNewInput(InPatchInput);
	}

	void FMixerAudioBus::RemovePatchInput(const FPatchInput& PatchInput)
	{
		return PatchMixerSplitter.RemovePatch(PatchInput);
	}



}

