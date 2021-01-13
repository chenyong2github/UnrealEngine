// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaAudioDecoderOutput.h"
#include "Renderer/RendererAudio.h"

class FElectraPlayerAudioDecoderOutput : public IAudioDecoderOutput
{
public:
	FElectraPlayerAudioDecoderOutput()
	: BufferMaxSizeBytes(0)
	, BufferUsedSizeBytes(0)
	, Buffer(nullptr)
	{
	}

	~FElectraPlayerAudioDecoderOutput() override
	{
		Reserve(0);
	}

	void Reserve(uint32 InBufferSizeBytes) override
	{
		if (InBufferSizeBytes != BufferMaxSizeBytes)
		{
			if (Buffer)
			{
				FMemory::Free(Buffer);
				Buffer = nullptr;
			}
			BufferMaxSizeBytes = InBufferSizeBytes;
			if (BufferMaxSizeBytes)
			{
				Buffer = FMemory::Malloc(InBufferSizeBytes);
				check(Buffer);
			}
		}
	}

	void SetOwner(const TSharedPtr<IDecoderOutputOwner, ESPMode::ThreadSafe>& Renderer) override
	{
		OwningRenderer = Renderer;
	}

	void Initialize(ESampleFormat InFormat, uint32 InNumChannels, uint32 InSampleRate, FTimespan InDuration, const FDecoderTimeStamp & InPts, uint32 InBufferSizeBytes) override
	{
		check(BufferMaxSizeBytes >= InBufferSizeBytes);

		Format = InFormat;
		NumChannels = InNumChannels;
		SampleRate = InSampleRate;

		Time = InPts;
		Duration = InDuration;

		BufferUsedSizeBytes = InBufferSizeBytes;
	}

	const void* GetBuffer() const override
	{
		return Buffer;
	}

	uint32 GetUsedBufferBytes() const override
	{
		return BufferUsedSizeBytes;
	}

	uint32 GetReservedBufferBytes() const override
	{
		return BufferMaxSizeBytes;
	}

	uint32 GetChannels() const override
	{
		return NumChannels;
	}

	uint32 GetFrames() const override
	{
		if (Format == ESampleFormat::Undefined)
		{
			return 0;
		}

		check((int32)Format <= (int32)ESampleFormat::Int32);

		static_assert((int32)ESampleFormat::Undefined == 0, "Check order of enum");
		static_assert((int32)ESampleFormat::Double == 1, "Check order of enum");
		static_assert((int32)ESampleFormat::Float == 2, "Check order of enum");
		static_assert((int32)ESampleFormat::Int8 == 3, "Check order of enum");
		static_assert((int32)ESampleFormat::Int16 == 4, "Check order of enum");
		static_assert((int32)ESampleFormat::Int32 == 5, "Check order of enum");

		static const uint32 _SampleSize[] = {0, 8, 4, 1, 2, 4};
		return BufferUsedSizeBytes / (NumChannels * _SampleSize[(uint32)Format]);
	}

	ESampleFormat GetFormat() const override
	{
		return Format;
	}

	FDecoderTimeStamp GetTime() const override
	{
		return Time;
	}

	FTimespan GetDuration() const override
	{
		return Duration;
	}

	uint32 GetSampleRate() const override
	{
		return SampleRate;
	}

	void ShutdownPoolable() override
	{
		TSharedPtr<IDecoderOutputOwner, ESPMode::ThreadSafe> lockedAudioRenderer = OwningRenderer.Pin();
		if (lockedAudioRenderer.IsValid())
		{
			lockedAudioRenderer->SampleReleasedToPool(Duration);
		}
	}

private:
	uint32 BufferMaxSizeBytes;
	uint32 BufferUsedSizeBytes;
	void *Buffer;

	ESampleFormat Format;
	uint32 NumChannels;
	uint32 SampleRate;

	FDecoderTimeStamp Time;
	FTimespan Duration;

	TWeakPtr<IDecoderOutputOwner, ESPMode::ThreadSafe> OwningRenderer;
};

// =================================================================================================================================

/*
	Create one and the same audio sample for all platforms - we so far do not need any specialization here (otherwise this should be with the respective decoder - much like the class definition / impl above)
*/
IAudioDecoderOutput *FElectraPlayerPlatformAudioDecoderOutputFactory::Create()
{
	return new FElectraPlayerAudioDecoderOutput();
}

