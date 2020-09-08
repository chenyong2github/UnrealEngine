// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAudioFormats.h"

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundAudioBuffer.h"

struct FMyType
{
public:
	FMyType() = delete;

private:
	FMyType Get();
	friend class FMyOtherType;
};

namespace Metasound
{
	// REGISTER_METASOUND_DATATYPE(FAudioBuffer, "Audio:Buffer")
	REGISTER_METASOUND_DATATYPE(FUnformattedAudio, "Audio:Unformatted")
	REGISTER_METASOUND_DATATYPE(FMultichannelAudioFormat, "Audio:Multichannel")



	// TODO: Neither of these are trivially constructible, becuase they are actually views into a preexisting buffer.
	//REGISTER_METASOUND_DATATYPE(FMonoAudioFormat)
	//REGISTER_METASOUND_DATATYPE(FStereoAudioFormat)
	IMPL_METASOUND_DATA_TYPE(FMonoAudioFormat, "Audio:Mono")
	IMPL_METASOUND_DATA_TYPE(FStereoAudioFormat, "Audio:Stereo")

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

	FUnformattedAudio::FUnformattedAudio(int32 InInitialNumChannels, const FOperatorSettings& InSettings)
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

	FMultichannelAudioFormat::FMultichannelAudioFormat(int32 InNumChannels, const FOperatorSettings& InSettings)
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
