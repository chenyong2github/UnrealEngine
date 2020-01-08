// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
//#if PLATFORM_WINDOWS
#include "CoreMinimal.h"

#if PLATFORM_SWITCH
// Switch uses page alignment for submitted buffers
#define AUDIO_BUFFER_ALIGNMENT 4096
#else
#define AUDIO_BUFFER_ALIGNMENT 16
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogAudioResampler, Warning, All);

namespace Audio
{
	typedef TArray<float, TAlignedHeapAllocator<AUDIO_BUFFER_ALIGNMENT>> AlignedFloatBuffer;
	typedef TArray<uint8, TAlignedHeapAllocator<AUDIO_BUFFER_ALIGNMENT>> AlignedByteBuffer;

	enum class EResamplingMethod : uint8
	{
		BestSinc = 0,
		ModerateSinc = 1,
		FastSinc = 2,
		ZeroOrderHold = 3,
		Linear = 4
	};

	struct FResamplingParameters
	{
		EResamplingMethod ResamplerMethod;
		int32 NumChannels;
		float SourceSampleRate;
		float DestinationSampleRate;
		AlignedFloatBuffer& InputBuffer;
	};

	struct FResamplerResults
	{
		AlignedFloatBuffer* OutBuffer;

		float ResultingSampleRate;

		int32 InputFramesUsed;

		int32 OutputFramesGenerated;

		FResamplerResults()
			: OutBuffer(nullptr)
			, ResultingSampleRate(0.0f)
			, InputFramesUsed(0)
			, OutputFramesGenerated(0)
		{}
	};

	// Get how large the output buffer should be for a resampling operation.
	AUDIOPLATFORMCONFIGURATION_API int32 GetOutputBufferSize(const FResamplingParameters& InParameters);

	// Simple, inline resampler. Returns true on success, false otherwise.
	AUDIOPLATFORMCONFIGURATION_API bool Resample(const FResamplingParameters& InParameters, FResamplerResults& OutData);


	class FResamplerImpl;

	class AUDIOPLATFORMCONFIGURATION_API FResampler
	{
	public:
		FResampler();
		~FResampler();

		void Init(EResamplingMethod ResamplingMethod, float StartingSampleRateRatio, int32 InNumChannels);
		void SetSampleRateRatio(float InRatio);
		int32 ProcessAudio(float* InAudioBuffer, int32 InSamples, bool bEndOfInput, float* OutAudioBuffer, int32 MaxOutputFrames, int32& OutNumFrames);

	private:
		TUniquePtr<FResamplerImpl> CreateImpl();
		TUniquePtr<FResamplerImpl> Impl;
	};
	
}

//#endif //PLATFORM_WINDOWS