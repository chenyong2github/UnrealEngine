// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Halton.h: RenderResource interface for storing Halton sequence permutations
	on the GPU.
=============================================================================*/

#pragma once

#include "HaltonUtilities.h"
#include "UniformBuffer.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FHaltonIteration, )
SHADER_PARAMETER(int, Dimensions)
SHADER_PARAMETER(int, SequenceRowCount)
SHADER_PARAMETER(int, SequenceColumnCount)
SHADER_PARAMETER(int, IterationCount)
SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SequenceIteration)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

/**
 * This resource stores a number of iterations of Halton sequences, up to the specified dimensionality.
 */
class FHaltonSequenceIteration : public FRenderResource
{
public:
	FHaltonSequenceIteration(
		const FHaltonSequence& HaltonSequenceLocal,
		uint32 IterationCountLocal,
		uint32 SequenceCountLocal,
		uint32 DimensionCount,
		uint32 IterationLocal
	);

	virtual ~FHaltonSequenceIteration();

	// FRenderResource interface.
	virtual void InitRHI() override;

	virtual void ReleaseRHI() override
	{
	}

	virtual FString GetFriendlyName() const override
	{
		return TEXT("Halton Sequence Iteration");
	}

	// Accessors
	uint32 GetSequenceCount() const
	{
		return SequenceCount;
	}

	uint32 GetIterationCount() const
	{
		return IterationCount;
	}

	uint32 GetDimensionCount() const
	{
		return DimensionCount;
	}

	uint32 GetIteration() const
	{
		return Iteration;
	}

	FBufferRHIRef SequenceIteration;

private:
	void InitializeSequence();

	const FHaltonSequence& HaltonSequence;
	TArray<int> Sequence;

	uint32 IterationCount;
	uint32 SequenceCount;
	uint32 DimensionCount;
	uint32 Iteration;
};

inline void InitializeHaltonSequenceIteration(const FHaltonSequenceIteration& HaltonSequenceIteration, FHaltonIteration& HaltonIteration)
{
	HaltonIteration.Dimensions = HaltonSequenceIteration.GetDimensionCount() / 3u;
	HaltonIteration.SequenceRowCount = FMath::Sqrt(static_cast<float>(HaltonSequenceIteration.GetSequenceCount()));
	HaltonIteration.SequenceColumnCount = HaltonSequenceIteration.GetSequenceCount() / HaltonIteration.SequenceRowCount;
	HaltonIteration.IterationCount = HaltonSequenceIteration.GetIterationCount();
	HaltonIteration.SequenceIteration = RHICreateShaderResourceView(HaltonSequenceIteration.SequenceIteration);
}

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FHaltonPrimes, )
SHADER_PARAMETER(int, Dimensions)
SHADER_PARAMETER_SRV(StructuredBuffer<uint4>, Primes)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

/**
* This resource stores a number of iterations of Halton sequences, up to the specified dimensionality.
*/
class FHaltonPrimesResource : public FRenderResource
{
public:
	FHaltonPrimesResource();
	virtual ~FHaltonPrimesResource() {}

	// FRenderResource interface.
	virtual void InitRHI() override;

	virtual void ReleaseRHI() override
	{
	}

	virtual FString GetFriendlyName() const override
	{
		return TEXT("Halton Sequence Iteration");
	}

	// Accessors
	uint32 GetDimensionCount() const
	{
		return DimensionCount;
	}

	FBufferRHIRef PrimesBuffer;

private:
	TArray<int> Primes;
	uint32 DimensionCount;
};

inline void InitializeHaltonPrimes(const FHaltonPrimesResource& HaltonPrimeResource, FHaltonPrimes& HaltonPrimes)
{
	HaltonPrimes.Dimensions = HaltonPrimeResource.GetDimensionCount();
	HaltonPrimes.Primes = RHICreateShaderResourceView(HaltonPrimeResource.PrimesBuffer);
}