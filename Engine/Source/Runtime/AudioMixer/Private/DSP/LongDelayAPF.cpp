// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DSP/LongDelayAPF.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/Dsp.h"

using namespace Audio;

FLongDelayAPF::FLongDelayAPF(float InG, int32 InNumDelaySamples, int32 InMaxNumInternalBufferSamples)
:	G(InG),
	NumDelaySamples(InNumDelaySamples),
	NumInternalBufferSamples(InMaxNumInternalBufferSamples)
{
	// NumInternalBufferSamples must be less than or equal to the length of the delay
	if (NumInternalBufferSamples > NumDelaySamples)
	{
		NumInternalBufferSamples = NumDelaySamples;

		// Block size must be divisible by simd alignment to support simd operations.
		NumInternalBufferSamples -= (NumInternalBufferSamples % AUDIO_SIMD_FLOAT_ALIGNMENT);
	}

	checkf(NumInternalBufferSamples > 1, TEXT("Invalid internal buffer length"));

	// Allocate delay line
	DelayLine = MakeUnique<FAlignedBlockBuffer>(2 * NumInternalBufferSamples + NumDelaySamples, NumInternalBufferSamples);
	DelayLine->AddZeros(NumDelaySamples);

	// Allocate internal buffer
	WorkBuffer.Reset(NumInternalBufferSamples);
	WorkBuffer.AddUninitialized(NumInternalBufferSamples);
}

FLongDelayAPF::~FLongDelayAPF()
{}

void FLongDelayAPF::ProcessAudio(const AlignedFloatBuffer& InSamples, AlignedFloatBuffer& OutSamples)
{
	const float* InData = InSamples.GetData();
	const int32 InNum = InSamples.Num();
	float* OutDelayData = WorkBuffer.GetData();

	// Resize output
	OutSamples.Reset(InNum);
	OutSamples.AddUninitialized(InNum);
	float* OutData = OutSamples.GetData();


	// Process audio one block at a time.
	int32 LeftOver = InNum;
	int32 BufferIndex = 0;
	while (LeftOver != 0) 
	{
		// Determine block size for this loop.
		int32 NumToProcess = FMath::Min<int32>(NumInternalBufferSamples, LeftOver);
		const float* InDelayData = DelayLine->InspectSamples(NumToProcess);

		ProcessAudioBlock(&InData[BufferIndex], InDelayData, NumToProcess, &OutData[BufferIndex], OutDelayData);

		// Update delay line with new data.
		DelayLine->RemoveSamples(NumToProcess);
		DelayLine->AddSamples(OutDelayData, NumToProcess);

		LeftOver -= NumToProcess;
		BufferIndex += NumToProcess;
	}
}

void FLongDelayAPF::ProcessAudio(const AlignedFloatBuffer& InSamples, AlignedFloatBuffer& OutSamples, AlignedFloatBuffer& OutDelaySamples)
{
	const float* InData = InSamples.GetData();
	const int32 InNum = InSamples.Num();

	// Resize output buffers
	OutSamples.Reset(InNum);
	OutSamples.AddUninitialized(InNum);
	OutDelaySamples.Reset(InNum);
	OutDelaySamples.AddUninitialized(InNum);

	float* OutData = OutSamples.GetData();
	float* OutDelayData = OutDelaySamples.GetData();

	// Process audio one block at a time.
	int32 LeftOver = InNum;
	int32 BufferIndex = 0;
	while (LeftOver != 0) 
	{
		int32 NumToProcess = FMath::Min<int32>(NumInternalBufferSamples, LeftOver);
		const float* InDelayData = DelayLine->InspectSamples(NumToProcess);

		ProcessAudioBlock(&InData[BufferIndex], InDelayData, NumToProcess, &OutData[BufferIndex], &OutDelayData[BufferIndex]);
		
		// Update internal delay line.
		DelayLine->RemoveSamples(NumToProcess);
		DelayLine->AddSamples(&OutDelayData[BufferIndex], NumToProcess);

		LeftOver -= NumToProcess;
		BufferIndex += NumToProcess;
	}
}

void FLongDelayAPF::ProcessAudioBlock(const float* InSamples, const float* InDelaySamples, const int32 InNum, float* OutSamples, float* OutDelaySamples)
{
	// Calculate new delay line samples. "w[n] = x[n] + gw[n - d]"
	//WeightedSumBuffer(InDelaySamples, G, InSamples, OutDelaySamples, InNum);
	
	
	int32 NumToSIMD = InNum - (InNum % AUDIO_SIMD_FLOAT_ALIGNMENT);

	VectorRegister VG = MakeVectorRegister(G, G, G, G);
	VectorRegister VNG = MakeVectorRegister(-G, -G, -G, -G);
	VectorRegister VFMIN = MakeVectorRegister(FLT_MIN, FLT_MIN, FLT_MIN, FLT_MIN);
	VectorRegister VNFMIN = MakeVectorRegister(-FLT_MIN, -FLT_MIN, -FLT_MIN, -FLT_MIN);

	for (int32 i = 0; i < InNum; i += 4)
	{
		VectorRegister VInDelay = VectorLoadAligned(&InDelaySamples[i]);
		VectorRegister VInSamples = VectorLoadAligned(&InSamples[i]);
		// w[n] = x[n] + G * w[n - D]
		VectorRegister VOutDelay = VectorMultiplyAdd(VInDelay, VG, VInSamples);
		
		// Underflow clamp
		VectorRegister Mask = VectorBitwiseAnd(VectorCompareGT(VOutDelay, VNFMIN), VectorCompareLT(VOutDelay, VFMIN));
		VOutDelay = VectorSelect(Mask, GlobalVectorConstants::FloatZero, VOutDelay);
		VectorStoreAligned(VOutDelay, &OutDelaySamples[i]);

		// y[n] = -G * w[n] + w[n - D]
		VectorRegister VOut = VectorMultiplyAdd(VOutDelay, VNG, VInDelay);
		VectorStoreAligned(VOut, &OutSamples[i]);


	}

	// Calculate allpass for remaining samples that we couldn't SIMD
	for (int32 i = NumToSIMD; i < InNum; i++)
	{
		OutDelaySamples[i] = Audio::UnderflowClamp(InDelaySamples[i] * G + InSamples[i]);
		OutSamples[i] = OutDelaySamples[i] * -G + InDelaySamples[i];
	}
}

void FLongDelayAPF::Reset() 
{
	DelayLine->ClearSamples();
	DelayLine->AddZeros(NumDelaySamples);
}

int32 FLongDelayAPF::GetNumInternalBufferSamples() const
{
	return NumInternalBufferSamples;
}

