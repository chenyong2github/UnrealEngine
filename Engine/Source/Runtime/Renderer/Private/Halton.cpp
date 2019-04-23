// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Halton.cpp: RenderResource interface for storing Halton sequence
	permutations on the GPU.
=============================================================================*/

#include "Halton.h"
#include "Math/PackedVector.h"
#include "SceneUtils.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FHaltonIteration, "HaltonIteration");

// http://burtleburtle.net/bob/hash/integer.html
// Bob Jenkins integer hashing function in 6 shifts.
static uint32 IntegerHash(uint32 a)
{
	a = (a + 0x7ed55d16) + (a << 12);
	a = (a ^ 0xc761c23c) ^ (a >> 19);
	a = (a + 0x165667b1) + (a << 5);
	a = (a + 0xd3a2646c) ^ (a << 9);
	a = (a + 0xfd7046c5) + (a << 3);
	a = (a ^ 0xb55a4f09) ^ (a >> 16);
	return a;
}

FHaltonSequenceIteration::FHaltonSequenceIteration(const FHaltonSequence& HaltonSequenceLocal, uint32 IterationCountLocal, uint32 SequenceCountLocal, uint32 DimensionCount, uint32 IterationLocal)
	: HaltonSequence(HaltonSequenceLocal)
	, IterationCount(IterationCountLocal)
	, SequenceCount(SequenceCountLocal)
	, DimensionCount(FMath::Min(DimensionCount, FHaltonSequence::GetNumberOfDimensions()))
	, Iteration(IterationLocal)
{
	InitResource();
}

FHaltonSequenceIteration::~FHaltonSequenceIteration()
{
	ReleaseResource();
}

DECLARE_GPU_STAT_NAMED(HaltonSequence, TEXT("Halton Sequence"));

void FHaltonSequenceIteration::InitRHI()
{
	SCOPED_GPU_STAT(FRHICommandListExecutor::GetImmediateCommandList(), HaltonSequence);
	InitializeSequence();

	TArray<float> RandomSamples;
	RandomSamples.SetNum(SequenceCount * IterationCount * DimensionCount);
	for (uint32 SequenceIndex = 0; SequenceIndex < SequenceCount; ++SequenceIndex)
	{
		uint32 SequenceValue = Sequence[SequenceIndex] + Iteration * IterationCount;
		uint32 SequenceOffset = SequenceIndex * IterationCount * DimensionCount;
		for (uint32 IterationIndex = 0; IterationIndex < IterationCount; ++IterationIndex)
		{
			uint32 IterationOffset = IterationIndex * DimensionCount;
			for (uint32 Dimension = 0; Dimension < DimensionCount; Dimension++)
			{
				RandomSamples[SequenceOffset + IterationOffset + Dimension] = HaltonSequence.Sample(Dimension, SequenceValue + IterationIndex);
			}
		}
	}

	FRHIResourceCreateInfo CreateInfo;
	{
		CreateInfo.DebugName = TEXT("HaltonSequenceIteration");
		SequenceIteration.VertexBufferRHI = RHICreateVertexBuffer(RandomSamples.Num() * sizeof(float), BUF_Transient | BUF_FastVRAM | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);
		uint32 Offset = 0;
		void* BasePtr = RHILockVertexBuffer(SequenceIteration.VertexBufferRHI, 0, RandomSamples.Num() * sizeof(float), RLM_WriteOnly);
		FPlatformMemory::Memcpy(BasePtr, RandomSamples.GetData(), RandomSamples.Num() * sizeof(float));
		RHIUnlockVertexBuffer(SequenceIteration.VertexBufferRHI);
	}
}

void FHaltonSequenceIteration::InitializeSequence()
{
	Sequence.SetNum(SequenceCount);
	for (uint32 Index = 0; Index < SequenceCount; ++Index)
	{
		// Use hashing to provide scrambling
		Sequence[Index] = IntegerHash(Index);
	}
}
