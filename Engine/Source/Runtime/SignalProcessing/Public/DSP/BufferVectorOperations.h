// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_SWITCH
// Switch uses page alignment for submitted buffers
#define AUDIO_BUFFER_ALIGNMENT 4096
#else
#define AUDIO_BUFFER_ALIGNMENT 16
#endif

#define AUDIO_SIMD_BYTE_ALIGNMENT (16)
#define AUDIO_SIMD_FLOAT_ALIGNMENT (4)

namespace Audio
{
	/** Aligned allocator used for fast operations. */	
	using FAudioBufferAlignedAllocator = TAlignedHeapAllocator<AUDIO_BUFFER_ALIGNMENT>;

	using FAlignedByteBuffer  = TArray<uint8, FAudioBufferAlignedAllocator>;
	using FAlignedFloatBuffer = TArray<float, FAudioBufferAlignedAllocator>;
	using FAlignedInt32Buffer = TArray<int32, FAudioBufferAlignedAllocator>;

	// Deprecated in favor of versions above
	typedef TArray<uint8, FAudioBufferAlignedAllocator> AlignedByteBuffer;
	typedef TArray<float, FAudioBufferAlignedAllocator> AlignedFloatBuffer;
	typedef TArray<int32, FAudioBufferAlignedAllocator> AlignedInt32Buffer;


	/** CHANNEL-AGNOSTIC OPERATIONS */

	/* Sets a values to zero if value is denormal. Denormal numbers significantly slow down floating point operations. */
	SIGNALPROCESSING_API void BufferUnderflowClampFast(FAlignedFloatBuffer& InOutBuffer);

	/* Sets a values to zero if value is denormal. Denormal numbers significantly slow down floating point operations. */
	SIGNALPROCESSING_API void BufferUnderflowClampFast(float* RESTRICT InOutBuffer, const int32 InNum);
	
	/* Clamps the values in a buffer between a min and max value.*/
	SIGNALPROCESSING_API void BufferRangeClampFast(FAlignedFloatBuffer& InOutBuffer, float InMinValue, float InMaxValue);
	SIGNALPROCESSING_API void BufferRangeClampFast(float* RESTRICT InOutBuffer, const int32 InNum, float InMinValue, float InMaxValue);
	
	/** Multiplies the input aligned float buffer with the given value. */
	SIGNALPROCESSING_API void BufferMultiplyByConstant(const FAlignedFloatBuffer& InFloatBuffer, float InValue, FAlignedFloatBuffer& OutFloatBuffer);
	SIGNALPROCESSING_API void BufferMultiplyByConstant(const float* RESTRICT InFloatBuffer, float InValue, float* RESTRICT OutFloatBuffer, const int32 InNumSamples);
	SIGNALPROCESSING_API void BufferMultiplyByConstant(const FAlignedFloatBuffer& InFloatBuffer, float InValue, FAlignedFloatBuffer& OutFloatBuffer);

	/** Similar to BufferMultiplyByConstant, but (a) assumes a buffer length divisible by 4 and (b) performs the multiply in place. */
	SIGNALPROCESSING_API void MultiplyBufferByConstantInPlace(FAlignedFloatBuffer& InBuffer, float InGain);
	SIGNALPROCESSING_API void MultiplyBufferByConstantInPlace(float* RESTRICT InBuffer, int32 NumSamples, float InGain);

	/** Adds a constant to a buffer (useful for DC offset removal) */
	SIGNALPROCESSING_API void AddConstantToBufferInplace(FAlignedFloatBuffer& InBuffer, float Constant);
	SIGNALPROCESSING_API void AddConstantToBufferInplace(float* RESTRICT InBuffer, int32 NumSamples, float Constant);

	/** Sets a constant to a buffer (useful for DC offset application) */
	SIGNALPROCESSING_API void BufferSetToConstantInplace(FAlignedFloatBuffer& InBuffer, float Constant);
	SIGNALPROCESSING_API void BufferSetToConstantInplace(float* RESTRICT InBuffer, int32 NumSamples, float Constant);

	/* Performs an element-wise weighted sum OutputBuffer = (InBuffer1 x InGain1) + (InBuffer2 x InGain2) */
	SIGNALPROCESSING_API void BufferWeightedSumFast(const FAlignedFloatBuffer& InBuffer1, float InGain1, const FAlignedFloatBuffer& InBuffer2, float InGain2, FAlignedFloatBuffer& OutBuffer);

	/* Performs an element-wise weighted sum OutputBuffer = (InBuffer1 x InGain1) + InBuffer2 */
	SIGNALPROCESSING_API void BufferWeightedSumFast(const FAlignedFloatBuffer& InBuffer1, float InGain1, const FAlignedFloatBuffer& InBuffer2, FAlignedFloatBuffer& OutBuffer);

	/* Performs an element-wise weighted sum OutputBuffer = (InBuffer1 x InGain1) + (InBuffer2 x InGain2) */
	SIGNALPROCESSING_API void BufferWeightedSumFast(const float* RESTRICT InBuffer1, float InGain1, const float* RESTRICT InBuffer2, float InGain2, float* RESTRICT OutBuffer, int32 InNum);

	/* Performs an element-wise weighted sum OutputBuffer = (InBuffer1 x InGain1) + InBuffer2 */
	SIGNALPROCESSING_API void BufferWeightedSumFast(const float* RESTRICT InBuffer1, float InGain1, const float* RESTRICT InBuffer2, float* RESTRICT OutBuffer, int32 InNum);
	SIGNALPROCESSING_API void MultiplyBufferByConstantInPlace(FAlignedFloatBuffer& InBuffer, float InGain);
	SIGNALPROCESSING_API void MultiplyBufferByConstantInPlace(float* RESTRICT InBuffer, int32 NumSamples, float InGain);

	/* Takes a float buffer and quickly interpolates it's gain from StartValue to EndValue. */
	/* This operation completely ignores channel counts, so avoid using this function on buffers that are not mono, stereo or quad */
	/* if the buffer needs to fade all channels uniformly. */
	SIGNALPROCESSING_API void FadeBufferFast(FAlignedFloatBuffer& OutFloatBuffer, const float StartValue, const float EndValue);
	SIGNALPROCESSING_API void FadeBufferFast(float* RESTRICT OutFloatBuffer, int32 NumSamples, const float StartValue, const float EndValue);

	/** Takes buffer InFloatBuffer, optionally multiplies it by Gain, and adds it to BufferToSumTo. */
	SIGNALPROCESSING_API void MixInBufferFast(const FAlignedFloatBuffer& InFloatBuffer, FAlignedFloatBuffer& BufferToSumTo, const float Gain);
	SIGNALPROCESSING_API void MixInBufferFast(const float* RESTRICT InFloatBuffer, float* RESTRICT BufferToSumTo, int32 NumSamples, const float Gain);
	SIGNALPROCESSING_API void MixInBufferFast(const FAlignedFloatBuffer& InFloatBuffer, FAlignedFloatBuffer& BufferToSumTo, const float StartGain, const float EndGain);
	SIGNALPROCESSING_API void MixInBufferFast(const float* RESTRICT InFloatBuffer, float* RESTRICT BufferToSumTo, int32 NumSamples, const float StartGain, const float EndGain);
	SIGNALPROCESSING_API void MixInBufferFast(const FAlignedFloatBuffer& InFloatBuffer, FAlignedFloatBuffer& BufferToSumTo);
	SIGNALPROCESSING_API void MixInBufferFast(const float* RESTRICT InFloatBuffer, float* RESTRICT BufferToSumTo, int32 NumSamples);

	/* Subtracts two buffers together element-wise. */
	SIGNALPROCESSING_API void BufferSubtractFast(const FAlignedFloatBuffer& InMinuend, const FAlignedFloatBuffer& InSubtrahend, FAlignedFloatBuffer& OutputBuffer);

	/* Subtracts two buffers together element-wise. */
	SIGNALPROCESSING_API void BufferSubtractFast(const float* RESTRICT InMinuend, const float* RESTRICT InSubtrahend, float* RESTRICT OutputBuffer, int32 NumSamples);

	/* Performs element-wise in-place subtraction placing the result in the subtrahend. InOutSubtrahend = InMinuend - InOutSubtrahend */
	SIGNALPROCESSING_API void BufferSubtractInPlace1Fast(const FAlignedFloatBuffer& InMinuend, FAlignedFloatBuffer& InOutSubtrahend);

	/* Performs element-wise in-place subtraction placing the result in the subtrahend. InOutSubtrahend = InMinuend - InOutSubtrahend */
	SIGNALPROCESSING_API void BufferSubtractInPlace1Fast(const float* RESTRICT InMinuend, float* RESTRICT InOutSubtrahend, int32 NumSamples);


	/* Performs element-wise in-place subtraction placing the result in the minuend. InOutMinuend = InOutMinuend - InSubtrahend */
	SIGNALPROCESSING_API void BufferSubtractInPlace2Fast(FAlignedFloatBuffer& InOutMinuend, const FAlignedFloatBuffer& InSubtrahend);

	/* Performs element-wise in-place subtraction placing the result in the minuend. InOutMinuend = InOutMinuend - InSubtrahend */
	SIGNALPROCESSING_API void BufferSubtractInPlace2Fast(float* RESTRICT InOutMinuend, const float* RESTRICT InSubtrahend, int32 NumSamples);

	/** This version of MixInBufferFast will fade from StartGain to EndGain. */
	SIGNALPROCESSING_API void MixInBufferFast(const FAlignedFloatBuffer& InFloatBuffer, FAlignedFloatBuffer& BufferToSumTo, const float StartGain, const float EndGain);
	SIGNALPROCESSING_API void MixInBufferFast(const float* RESTRICT InFloatBuffer, float* RESTRICT BufferToSumTo, int32 NumSamples, const float StartGain, const float EndGain);

	/** Sums two buffers together and places the result in the resulting buffer. */
	SIGNALPROCESSING_API void SumBuffers(const FAlignedFloatBuffer& InFloatBuffer1, const FAlignedFloatBuffer& InFloatBuffer2, FAlignedFloatBuffer& OutputBuffer);
	SIGNALPROCESSING_API void SumBuffers(const float* RESTRICT InFloatBuffer1, const float* RESTRICT InFloatBuffer2, float* RESTRICT OutputBuffer, int32 NumSamples);

	/** Multiply the second buffer in place by the first buffer. */
	SIGNALPROCESSING_API void MultiplyBuffersInPlace(const FAlignedFloatBuffer& InFloatBuffer, FAlignedFloatBuffer& BufferToMultiply);
	SIGNALPROCESSING_API void MultiplyBuffersInPlace(const float* RESTRICT InFloatBuffer, float* RESTRICT BufferToMultiply, int32 NumSamples);

	/** CHANNEL-AGNOSTIC ANALYSIS OPERATIONS */

	/** Takes an audio buffer and returns the magnitude across that buffer. */
	SIGNALPROCESSING_API float GetMagnitude(const FAlignedFloatBuffer& Buffer);
	SIGNALPROCESSING_API float GetMagnitude(const float* RESTRICT Buffer, int32 NumSamples);

	/** Takes an audio buffer and gets the average absolute amplitude across that buffer. */
	SIGNALPROCESSING_API float GetAverageAmplitude(const FAlignedFloatBuffer& Buffer);
	SIGNALPROCESSING_API float GetAverageAmplitude(const float* RESTRICT Buffer, int32 NumSamples);

	/** CHANNEL-SPECIFIC OPERATIONS */

	/** Takes a 2 channel interleaved buffer and applies Gains to it. Gains is expected to point to a 2 float long buffer.
	 *  StereoBuffer must have an even number of frames.
	 *  If StartGains and EndGains are provided, this function will interpolate between the two across the buffer.
	*/
	SIGNALPROCESSING_API void Apply2ChannelGain(FAlignedFloatBuffer& StereoBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Apply2ChannelGain(float* RESTRICT StereoBuffer, int32 NumSamples, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Apply2ChannelGain(FAlignedFloatBuffer& StereoBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void Apply2ChannelGain(float* RESTRICT StereoBuffer, int32 NumSamples, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** Takes a 1 channel buffer and mixes it to a stereo buffer using Gains. Gains is expected to point to a 2 float long buffer.
	 *  these buffers must have an even number of frames.
	 *  If StartGains and EndGains are provided, this function will interpolate between the two across the buffer.
	*/
	SIGNALPROCESSING_API void MixMonoTo2ChannelsFast(const FAlignedFloatBuffer& MonoBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void MixMonoTo2ChannelsFast(const float* RESTRICT MonoBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void MixMonoTo2ChannelsFast(const FAlignedFloatBuffer& MonoBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void MixMonoTo2ChannelsFast(const float* RESTRICT MonoBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void MixMonoTo2ChannelsFast(const FAlignedFloatBuffer& MonoBuffer, FAlignedFloatBuffer& DestinationBuffer);
	SIGNALPROCESSING_API void MixMonoTo2ChannelsFast(const float* RESTRICT MonoBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames);

	/** Takes a 2 channel buffer and mixes it to an 2 channel interleaved buffer using Gains. Gains is expected to point to a 16 float long buffer.
	*  Output gains for the left input channel should be the first 8 values in Gains, and Output gains for the right input channel should be rest.
	*  NumFrames must be a multiple of 4.
	*  If StartGains and EndGains are provided, this function will interpolate between the two across the buffer.
	*/
	SIGNALPROCESSING_API void Mix2ChannelsTo2ChannelsFast(const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Mix2ChannelsTo2ChannelsFast(const float* RESTRICT SourceBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Mix2ChannelsTo2ChannelsFast(const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void Mix2ChannelsTo2ChannelsFast(const float* RESTRICT SourceBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** Takes a 4 channel interleaved buffer and applies Gains to it. Gains is expected to point to a 2 float long buffer.
	 *  If StartGains and EndGains are provided, this function will interpolate between the two across the buffer.
	*/
	SIGNALPROCESSING_API void Apply4ChannelGain(FAlignedFloatBuffer& InterleavedBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Apply4ChannelGain(float* RESTRICT InterleavedBuffer, int32 NumSamples, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Apply4ChannelGain(FAlignedFloatBuffer& InterleavedBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void Apply4ChannelGain(float* RESTRICT InterleavedBuffer, int32 NumSamples, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** Takes a 1 channel buffer and mixes it to an 8 channel interleaved buffer using Gains. Gains is expected to point to a 8 float long buffer.
	*  these buffers must have an even number of frames.
	*  If StartGains and EndGains are provided, this function will interpolate between the two across the buffer.
	*/
	SIGNALPROCESSING_API void MixMonoTo4ChannelsFast(const FAlignedFloatBuffer& MonoBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void MixMonoTo4ChannelsFast(const float* RESTRICT MonoBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void MixMonoTo4ChannelsFast(const FAlignedFloatBuffer& MonoBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void MixMonoTo4ChannelsFast(const float* RESTRICT MonoBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** Takes a 2 channel buffer and mixes it to an 8 channel interleaved buffer using Gains. Gains is expected to point to a 16 float long buffer.
	*  Output gains for the left input channel should be the first 8 values in Gains, and Output gains for the right input channel should be rest.
	*  NumFrames must be a multiple of 4.
	*  If StartGains and EndGains are provided, this function will interpolate between the two across the buffer.
	*/
	SIGNALPROCESSING_API void Mix2ChannelsTo4ChannelsFast(const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Mix2ChannelsTo4ChannelsFast(const float* RESTRICT SourceBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Mix2ChannelsTo4ChannelsFast(const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void Mix2ChannelsTo4ChannelsFast(const float* RESTRICT SourceBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** Takes a 6 channel interleaved buffer and applies Gains to it. Gains is expected to point to a 2 float long buffer.
	 *  InterleavedBuffer must have an even number of frames.
	*/
	SIGNALPROCESSING_API void Apply6ChannelGain(FAlignedFloatBuffer& InterleavedBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Apply6ChannelGain(float* RESTRICT InterleavedBuffer, int32 NumSamples, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Apply6ChannelGain(FAlignedFloatBuffer& InterleavedBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void Apply6ChannelGain(float* RESTRICT InterleavedBuffer, int32 NumSamples, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** Takes a 1 channel buffer and mixes it to an 8 channel interleaved buffer using Gains. Gains is expected to point to a 8 float long buffer.
	 *  these buffers must have an even number of frames.
	 *  If StartGains and EndGains are provided, this function will interpolate between the two across the buffer.
	*/
	SIGNALPROCESSING_API void MixMonoTo6ChannelsFast(const FAlignedFloatBuffer& MonoBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void MixMonoTo6ChannelsFast(const float* RESTRICT MonoBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void MixMonoTo6ChannelsFast(const FAlignedFloatBuffer& MonoBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void MixMonoTo6ChannelsFast(const float* RESTRICT MonoBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** Takes a 2 channel buffer and mixes it to an 8 channel interleaved buffer using Gains. Gains is expected to point to a 16 float long buffer.
	 *  Output gains for the left input channel should be the first 8 values in Gains, and Output gains for the right input channel should be rest.
	 *  NumFrames must be a multiple of 4.
	 *  If StartGains and EndGains are provided, this function will interpolate between the two across the buffer.
	*/
	SIGNALPROCESSING_API void Mix2ChannelsTo6ChannelsFast(const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Mix2ChannelsTo6ChannelsFast(const float* RESTRICT SourceBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Mix2ChannelsTo6ChannelsFast(const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void Mix2ChannelsTo6ChannelsFast(const float* RESTRICT SourceBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** Takes an 8 channel interleaved buffer and applies Gains to it. Gains is expected to point to an 8 float long buffer. */
	SIGNALPROCESSING_API void Apply8ChannelGain(FAlignedFloatBuffer& InterleavedBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Apply8ChannelGain(float* RESTRICT InterleavedBuffer, int32 NumSamples, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Apply8ChannelGain(FAlignedFloatBuffer& InterleavedBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void Apply8ChannelGain(float* RESTRICT InterleavedBuffer, int32 NumSamples, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** Takes a 1 channel buffer and mixes it to an 8 channel interleaved buffer using Gains. Gains is expected to point to a 8 float long buffer.
	 *  these buffers must have an even number of frames.
	 *  If StartGains and EndGains are provided, this function will interpolate between the two across the buffer.
	*/
	SIGNALPROCESSING_API void MixMonoTo8ChannelsFast(const FAlignedFloatBuffer& MonoBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void MixMonoTo8ChannelsFast(const float* RESTRICT MonoBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void MixMonoTo8ChannelsFast(const FAlignedFloatBuffer& MonoBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void MixMonoTo8ChannelsFast(const float* RESTRICT MonoBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** Takes a 2 channel buffer and mixes it to an 8 channel interleaved buffer using Gains. Gains is expected to point to a 16 float long buffer.
	 *  Output gains for the left input channel should be the first 8 values in Gains, and Output gains for the right input channel should be rest.
	 *  these buffers must have an even number of frames.
	 *  If StartGains and EndGains are provided, this function will interpolate between the two across the buffer.
	*/
	SIGNALPROCESSING_API void Mix2ChannelsTo8ChannelsFast(const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Mix2ChannelsTo8ChannelsFast(const float* RESTRICT SourceBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void Mix2ChannelsTo8ChannelsFast(const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void Mix2ChannelsTo8ChannelsFast(const float* RESTRICT SourceBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/** This is a generalized operation that uses the channel gain matrix provided in Gains to mix an interleaved source buffer to the interleaved downmix buffer.
	 *  This operation is not explicitly vectorized and will almost always be slower than using one of the functions above.
	*/
	SIGNALPROCESSING_API void DownmixBuffer(int32 NumSourceChannels, int32 NumDestinationChannels, const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& DestinationBuffer, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void DownmixBuffer(int32 NumSourceChannels, int32 NumDestinationChannels, const float* RESTRICT SourceBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void DownmixBuffer(int32 NumSourceChannels, int32 NumDestinationChannels, const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& DestinationBuffer, float* RESTRICT StartGains, const float* RESTRICT EndGains);
	SIGNALPROCESSING_API void DownmixBuffer(int32 NumSourceChannels, int32 NumDestinationChannels, const float* RESTRICT SourceBuffer, float* RESTRICT DestinationBuffer, int32 NumFrames, float* RESTRICT StartGains, const float* RESTRICT EndGains);

	/**
	 * This is similar to DownmixBuffer, except that it sums into DestinationBuffer rather than overwriting it.
	 */
	SIGNALPROCESSING_API void DownmixAndSumIntoBuffer(int32 NumSourceChannels, int32 NumDestinationChannels, const FAlignedFloatBuffer& SourceBuffer, FAlignedFloatBuffer& BufferToSumTo, const float* RESTRICT Gains);
	SIGNALPROCESSING_API void DownmixAndSumIntoBuffer(int32 NumSourceChannels, int32 NumDestinationChannels, const float* RESTRICT SourceBuffer, float* RESTRICT BufferToSumTo, int32 NumFrames, const float* RESTRICT Gains);

	/** Interleaves samples from two input buffers */
	SIGNALPROCESSING_API void BufferInterleave2ChannelFast(const FAlignedFloatBuffer& InBuffer1, const FAlignedFloatBuffer& InBuffer2, FAlignedFloatBuffer& OutBuffer);

	/** Interleaves samples from two input buffers */
	SIGNALPROCESSING_API void BufferInterleave2ChannelFast(const float* RESTRICT InBuffer1, const float* RESTRICT InBuffer2, float* RESTRICT OutBuffer, const int32 InNum);

	/** Deinterleaves samples from a 2 channel input buffer */
	SIGNALPROCESSING_API void BufferDeinterleave2ChannelFast(const FAlignedFloatBuffer& InBuffer, FAlignedFloatBuffer& OutBuffer1, FAlignedFloatBuffer& OutBuffer2);

	/** Deinterleaves samples from a 2 channel input buffer */
	SIGNALPROCESSING_API void BufferDeinterleave2ChannelFast(const float* RESTRICT InBuffer, float* RESTRICT OutBuffer1, float* RESTRICT OutBuffer2, const int32 InNum);

	/** Sums 2 channel interleaved input samples. OutSamples[n] = InSamples[2n] + InSamples[2n + 1] */
	SIGNALPROCESSING_API void BufferSum2ChannelToMonoFast(const FAlignedFloatBuffer& InSamples, FAlignedFloatBuffer& OutSamples);

	/** Sums 2 channel interleaved input samples. OutSamples[n] = InSamples[2n] + InSamples[2n + 1] */
	SIGNALPROCESSING_API void BufferSum2ChannelToMonoFast(const float* RESTRICT InSamples, float* RESTRICT OutSamples, const int32 InNumFrames);

	/** Compute power of complex data. Out[i] = Real[i] * Real[i] + Imaginary[i] * Imaginary[i] */
	SIGNALPROCESSING_API void BufferComplexToPowerFast(const FAlignedFloatBuffer& InRealSamples, const FAlignedFloatBuffer& InImaginarySamples, FAlignedFloatBuffer& OutPowerSamples);

	/** Compute power of complex data. Out[i] = Real[i] * Real[i] + Imaginary[i] * Imaginary[i] */
	SIGNALPROCESSING_API void BufferComplexToPowerFast(const float* RESTRICT InRealSamples, const float* RESTRICT InImaginarySamples, float* RESTRICT OutPowerSamples, const int32 InNum);

	/** Compute magnitude of complex data. Out[i] = Sqrt(Real[i] * Real[i] + Imaginary[i] * Imaginary[i]) */
	SIGNALPROCESSING_API void BufferComplexToMagnitudeFast(const FAlignedFloatBuffer& InRealSamples, const FAlignedFloatBuffer& InImaginarySamples, FAlignedFloatBuffer& OutPowerSamples);

	/** Compute magnitude of complex data. Out[i] = Sqrt(Real[i] * Real[i] + Imaginary[i] * Imaginary[i]) */
	SIGNALPROCESSING_API void BufferComplexToMagnitudeFast(const float* RESTRICT InRealSamples, const float* RESTRICT InImaginarySamples, float* RESTRICT OutPowerSamples, const int32 InNum);

	/** Class which handles a vectorized interpolation of an entire buffer to the values of a target buffer */
	class SIGNALPROCESSING_API FBufferLinearEase
	{
	public:
		FBufferLinearEase();
		FBufferLinearEase(const FAlignedFloatBuffer& InSourceValues, const FAlignedFloatBuffer& InTargetValues, int32 InLerpLength);
		~FBufferLinearEase();

		/** will cache SourceValues ptr and manually update SourceValues on Update() */
		void Init(const FAlignedFloatBuffer& InSourceValues, const FAlignedFloatBuffer& InTargetValues, int32 InLerpLength);

		/** Performs Vectorized update of SourceValues float buffer. Returns true if interpolation is complete */
		bool Update(FAlignedFloatBuffer& InSourceValues);

		/** Update overloaded to let you jump forward more than a single time-step */
		bool Update(uint32 StepsToJumpForward, FAlignedFloatBuffer& InSourceValues);

		/** returns const reference to the deltas buffer for doing interpolation elsewhere */
		const FAlignedFloatBuffer& GetDeltaBuffer();

	private:
		int32 BufferLength {0};
		int32 LerpLength {0};
		int32 CurrentLerpStep{0};
		FAlignedFloatBuffer DeltaBuffer;

	}; // class BufferLerper
}
