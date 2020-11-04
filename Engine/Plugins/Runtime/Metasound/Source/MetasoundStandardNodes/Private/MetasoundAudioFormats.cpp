// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAudioFormats.h"

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundAudioBuffer.h"

REGISTER_METASOUND_DATATYPE(Metasound::FUnformattedAudio, "Audio:Unformatted")
REGISTER_METASOUND_DATATYPE(Metasound::FMonoAudioFormat, "Audio:Mono")
REGISTER_METASOUND_DATATYPE(Metasound::FStereoAudioFormat, "Audio:Stereo")

// FMultichannelAudio cannot be used as an input type because it's channel count
// cannot be changed at runtime. Hence, it is not registered.
DEFINE_METASOUND_DATA_TYPE(Metasound::FMultichannelAudioFormat, "Audio:Multichannel")

namespace Metasound
{

	/* FUnformattedAudio */
	FUnformattedAudio::FUnformattedAudio(int32 InNumFrames, int32 InNumChannels, int32 InMaxNumChannels)
	:	NumFrames(InNumFrames)
	,	NumChannels(0)
	,	MaxNumChannels(InMaxNumChannels)
	{
		NumFrames = FMath::Max(NumFrames, 0);
		MaxNumChannels = FMath::Max(MaxNumChannels, 0);

		for (int32 i = 0; i < MaxNumChannels; i++)
		{
			FAudioBufferWriteRef Audio = FAudioBufferWriteRef::CreateNew(NumFrames);
			Audio->Zero();

			WritableBufferStorage.Add(Audio);
			ReadableBufferStorage.Add(Audio);
		}

		SetNumChannels(InNumChannels);
	}

	FUnformattedAudio::FUnformattedAudio(const FOperatorSettings& InSettings, int32 InInitialNumChannels)
		: FUnformattedAudio(InSettings.GetNumFramesPerBlock(), InInitialNumChannels, 8)
	{
	}

	int32 FUnformattedAudio::SetNumChannels(int32 InNumChannels)
	{
		NumChannels = FMath::Max(0, FMath::Min(InNumChannels, MaxNumChannels));

		ReadableBuffers = TArrayView<const FAudioBufferReadRef>(ReadableBufferStorage.GetData(), NumChannels);
		WritableBuffers = TArrayView<const FAudioBufferWriteRef>(WritableBufferStorage.GetData(), NumChannels);

		return NumChannels;
	}


	/* FMultichannelAudioFormat */

	FMultichannelAudioFormat::FMultichannelAudioFormat()
	:	NumChannels(0)
	{
	}

	FMultichannelAudioFormat::FMultichannelAudioFormat(int32 InNumFrames, int32 InNumChannels)
	:	NumChannels(InNumChannels)
	{
		NumChannels = FMath::Max(0, NumChannels);
		InNumFrames = FMath::Max(0, InNumFrames);

		for (int32 i = 0; i < NumChannels; i++)
		{
			FAudioBufferWriteRef Audio = FAudioBufferWriteRef::CreateNew(InNumFrames);
			Audio->Zero();

			WritableBufferStorage.Add(Audio);
			ReadableBufferStorage.Add(Audio);
		}

		WritableBuffers = WritableBufferStorage;
		ReadableBuffers = ReadableBufferStorage;
	}

	FMultichannelAudioFormat::FMultichannelAudioFormat(const FOperatorSettings& InSettings, int32 InNumChannels)
		: FMultichannelAudioFormat(InSettings.GetNumFramesPerBlock(), InNumChannels)
	{}


	FMultichannelAudioFormat::FMultichannelAudioFormat(TArrayView<const FAudioBufferWriteRef> InWriteRefs)
	:	NumChannels(InWriteRefs.Num())
	{
		if (NumChannels > 0)
		{
			const int32 NumFrames = InWriteRefs[0]->Num();

			for (const FAudioBufferWriteRef& Ref : InWriteRefs)
			{
				checkf(NumFrames == Ref->Num(), TEXT("All buffers must have same number of frames (%d != %d)"), NumFrames, Ref->Num());

				WritableBufferStorage.Add(Ref);
				ReadableBufferStorage.Add(Ref);
			}

			WritableBuffers = WritableBufferStorage;
			ReadableBuffers = ReadableBufferStorage;
		}
	}

	FMultichannelAudioFormat::FMultichannelAudioFormat(TArrayView<const FAudioBufferReadRef> InReadRefs)
	:	NumChannels(InReadRefs.Num())
	{
		if (NumChannels > 0)
		{
			const int32 NumFrames = InReadRefs[0]->Num();

			for (const FAudioBufferReadRef& Ref : InReadRefs)
			{
				checkf(NumFrames == Ref->Num(), TEXT("All buffers must have same number of frames (%d != %d)"), NumFrames, Ref->Num());

				WritableBufferStorage.Add(WriteCast(Ref));
				ReadableBufferStorage.Add(Ref);
			}

			WritableBuffers = WritableBufferStorage;
			ReadableBuffers = ReadableBufferStorage;
		}
	}

}
