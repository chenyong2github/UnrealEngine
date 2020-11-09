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
					const int32 NumOutputFrames = SourceManager->GetNumOutputFrames();
					const int32 NumSourceSamples = NumSourceChannels * NumOutputFrames;

					// If source channels are 1 but the bus is 2 channels, we need to up-mix
					if (NumSourceChannels == 1 && NumChannels == 2)
					{
						int32 BusSampleIndex = 0;
						for (int32 SourceSampleIndex = 0; SourceSampleIndex < NumSourceSamples; ++SourceSampleIndex)
						{
							// Take half the source sample to up-mix to stereo
							const float SourceSample = 0.5f * AudioBusSend.SendLevel * SourceBufferPtr[SourceSampleIndex];

							// Mix in the source sample
							for (int32 BusChannel = 0; BusChannel < NumChannels; ++BusChannel)
							{
								BusDataBufferPtr[BusSampleIndex++] += SourceSample;
							}
						}
					}
					// If the source channels is greater than num channels
					else if (NumSourceChannels > NumChannels)
					{
						check(NumChannels == 1 || NumChannels == 2);
						Audio::AlignedFloatBuffer ChannelMap;
						SourceManager->Get2DChannelMap(AudioBusSend.SourceId, NumChannels, ChannelMap);

						Audio::AlignedFloatBuffer DownmixedBuffer;
						DownmixedBuffer.AddUninitialized(NumOutputFrames * NumChannels);
						Audio::DownmixBuffer(NumSourceChannels, NumChannels, SourceBufferPtr, DownmixedBuffer.GetData(), NumOutputFrames, ChannelMap.GetData());
						Audio::MixInBufferFast(DownmixedBuffer.GetData(), BusDataBufferPtr, DownmixedBuffer.Num(), AudioBusSend.SendLevel);
					}
					// If they're the same channels, just mix it in
					else if (ensureMsgf(NumSourceChannels == NumChannels, TEXT("NumSourceChannels=%d, NumChannels=%d"), NumSourceChannels, NumChannels))
					{
						Audio::MixInBufferFast(SourceBufferPtr, BusDataBufferPtr, NumOutputFrames * NumChannels, AudioBusSend.SendLevel);
					}

					// Push the mixed data to the patch splitter
					PatchSplitter.PushAudio(BusDataBufferPtr, NumOutputFrames * NumChannels);
				}				
			}
		}
	}

	void FMixerAudioBus::CopyCurrentBuffer(Audio::AlignedFloatBuffer& OutBuffer, int32 InNumFrames, int32 InNumOutputChannels, EMonoChannelUpmixMethod InMixMethod) const
	{
		const float* RESTRICT CurrentBuffer = GetCurrentBusBuffer();

		if (InNumOutputChannels == 2 && NumChannels == 1)
		{
			switch (InMixMethod)
			{
			case EMonoChannelUpmixMethod::EqualPower:
			{
				static constexpr float Sqrt2Over2Gains[2] = { 0.707106781f, 0.707106781f };
				MixMonoTo2ChannelsFast(CurrentBuffer, OutBuffer.GetData(), InNumFrames, Sqrt2Over2Gains);
			}
			break;

			case EMonoChannelUpmixMethod::Linear:
			{
				static constexpr float HalfGains[2] = { 0.5f, 0.5f };
				MixMonoTo2ChannelsFast(CurrentBuffer, OutBuffer.GetData(), InNumFrames, HalfGains);
			}
			break;

			case EMonoChannelUpmixMethod::FullVolume:
			default:
				MixMonoTo2ChannelsFast(CurrentBuffer, OutBuffer.GetData(), InNumFrames);
				break;
			}
		}
		else if (InNumOutputChannels == 1 && NumChannels == 2)
		{
			BufferSum2ChannelToMonoFast(CurrentBuffer, OutBuffer.GetData(), InNumFrames);
			switch (InMixMethod)
			{
			case EMonoChannelUpmixMethod::EqualPower:
			{
				static constexpr float Sqrt2Gain = 1.41421356f;
				MultiplyBufferByConstantInPlace(OutBuffer, Sqrt2Gain);
			}
			break;

			case EMonoChannelUpmixMethod::Linear:
			{
				static constexpr float DoubleGain = 2.0f;
				MultiplyBufferByConstantInPlace(OutBuffer, DoubleGain);
			}
			break;

			case EMonoChannelUpmixMethod::FullVolume:
			default:
				break;
			}
		}
		// Copy into the pre-distance attenuation buffer ptr as channel count should match
		else
		{
			check(InNumOutputChannels == NumChannels);
			FMemory::Memcpy(OutBuffer.GetData(), CurrentBuffer, sizeof(float) * InNumFrames * InNumOutputChannels);
		}
	}

	const float* FMixerAudioBus::GetCurrentBusBuffer() const
	{
		return MixedSourceData[CurrentBufferIndex].GetData();
	}

	const float* FMixerAudioBus::GetPreviousBusBuffer() const
	{
		return MixedSourceData[!CurrentBufferIndex].GetData();
	}

	FPatchOutputStrongPtr FMixerAudioBus::AddNewPatch(int32 MaxLatencyInSamples, float InGain)
	{
		return PatchSplitter.AddNewPatch(MaxLatencyInSamples, InGain);
	}


}

