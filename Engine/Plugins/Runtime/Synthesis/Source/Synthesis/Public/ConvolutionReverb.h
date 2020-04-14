// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/ConvolutionAlgorithm.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/AlignedBlockBuffer.h"

namespace Audio
{
	struct SYNTHESIS_API FConvolutionReverbSettings
	{
		FConvolutionReverbSettings();

		/* Used to account for energy added by convolution with "loud" Impulse Responses.  Not meant to be updated dynamically (linear gain)*/
		float NormalizationVolume;

		/* Amout of audio to be sent to rear channels in quad/surround configurations (linear gain, < 0 = phase inverted) */
		float RearChannelBleed;

		/* If true, send Surround Rear Channel Bleed Amount sends front left to back right and vice versa */
		bool bRearChannelFlip;

		static const FConvolutionReverbSettings DefaultSettings;
	};

	class SYNTHESIS_API FConvolutionReverb 
	{
		FConvolutionReverb() = delete;
		FConvolutionReverb(const FConvolutionReverb&) = delete;
		FConvolutionReverb& operator=(const FConvolutionReverb&) = delete;
		FConvolutionReverb(const FConvolutionReverb&&) = delete;

	public:
		FConvolutionReverb(TUniquePtr<IConvolutionAlgorithm> InAlgorithm, const FConvolutionReverbSettings& InSettings = FConvolutionReverbSettings::DefaultSettings);
		
		void SetSettings(const FConvolutionReverbSettings& InSettings);

		const FConvolutionReverbSettings& GetSettings() const;

		void SetConvolutionAlgorithm(TUniquePtr<IConvolutionAlgorithm> InAlgorithm);

		// If the number of input frames changes between callbacks, the output may contain discontinuities.
		void ProcessAudio(int32 InNumInputChannels, AlignedFloatBuffer& InputAudio, int32 InNumOutputChannels, AlignedFloatBuffer& OutputAudio);

		int32 GetNumInputChannels() const;
		int32 GetNumOutputChannels() const;

		static void InterleaveBuffer(AlignedFloatBuffer& OutBuffer, const TArray<AlignedFloatBuffer>& InputBuffers, const int32 NumChannels);
		static void DeinterleaveBuffer(TArray<AlignedFloatBuffer>& OutputBuffers, TArrayView<const float> InputBuffer, const int32 NumChannels);

	private:
		void UpdateBlockBuffers();

		void ProcessAudioBlock(const float* InputAudio, int32 InNumInputChannels, AlignedFloatBuffer& OutputAudio, int32 InNumOutputChannels);

		void Update();

		FConvolutionReverbSettings Settings;
		TUniquePtr<IConvolutionAlgorithm> ConvolutionAlgorithm;
		
		// data is passed to the convolution algorithm as 2D arrays
		TArray<AlignedFloatBuffer> InputDeinterleaveBuffers;

		// data is recieved from the convolution algorithm as 2D arrays
		TArray<AlignedFloatBuffer> OutputDeinterleaveBuffers;

		TArray<float*> InputBufferPtrs;
		TArray<float*> OutputBufferPtrs;

		TUniquePtr<FAlignedBlockBuffer> InputBlockBuffer;
		TUniquePtr<FAlignedBlockBuffer> OutputBlockBuffer;
		AlignedFloatBuffer InterleavedOutputBlock;

		float OutputGain;
		bool bIsSurroundOutput;
		bool bHasSurroundBleed;
		int32 ExpectedNumFramesPerCallback;
		int32 NumInputSamplesPerBlock;
		int32 NumOutputSamplesPerBlock;
	};
}
