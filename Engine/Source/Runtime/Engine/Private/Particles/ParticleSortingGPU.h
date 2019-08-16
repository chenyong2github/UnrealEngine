// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	ParticleSortingGPU.h: Interface for sorting GPU particles.
==============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"

struct FGPUSortBuffers;

/**
 * Buffers in GPU memory used to sort particles.
 */
class ENGINE_API FParticleSortBuffers : public FRenderResource
{
public:

	/** Initialization constructor. */
	explicit FParticleSortBuffers(bool InAsInt32 = false)
		: BufferSize(0)
		, bAsInt32(InAsInt32)
	{
	}

	void SetBufferSize(int32 InBufferSize)
	{
		BufferSize = InBufferSize;
	}

	/**
	 * Initialize RHI resources.
	 */
	virtual void InitRHI() override;

	/**
	 * Release RHI resources.
	 */
	virtual void ReleaseRHI() override;

	/**
	 * Retrieve the UAV for writing particle sort keys.
	 */
	FRHIUnorderedAccessView* GetKeyBufferUAV(int32 BufferIndex)
	{
		check((BufferIndex & 0xFFFFFFFE) == 0);
		return KeyBufferUAVs[BufferIndex];
	}

	/**
	 * Retrieve the UAV for writing particle vertices.
	 * bAsUint : whether to return a G16R16 view or a Uint32 view.
	 */
	FORCEINLINE FRHIUnorderedAccessView* GetVertexBufferUAV(int32 BufferIndex)
	{
		check((BufferIndex & 0xFFFFFFFE) == 0);
		return VertexBufferUAVs[BufferIndex];
	}

	/**
	 * Retrieve buffers needed to sort on the GPU.
	 */
	FGPUSortBuffers GetSortBuffers();

	/**
	 * Retrieve the sorted vertex buffer that results will always be located at.
	 */
	FRHIVertexBuffer* GetSortedVertexBufferRHI(int32 BufferIndex)
	{
		check((BufferIndex & 0xFFFFFFFE) == 0);
		return VertexBuffers[BufferIndex];
	}

	/**
	 * Retrieve the SRV that sort results will always be located at.
	 */
	FRHIShaderResourceView* GetSortedVertexBufferSRV(int32 BufferIndex)
	{
		check((BufferIndex & 0xFFFFFFFE) == 0);
		return VertexBufferSRVs[BufferIndex];
	}
	   
	/**
	 * Retrieve the UAV for the sorted vertex buffer at the given index.
	 */
	FRHIUnorderedAccessView* GetSortedVertexBufferUAV(int32 BufferIndex)
	{
		check((BufferIndex & 0xFFFFFFFE) == 0);
		return VertexBufferUAVs[BufferIndex];
	}


	/**
	 * Get the size allocated for sorted vertex buffers.
	 */
	int32 GetSize() { return BufferSize; }

private:

	/** Vertex buffer storage for particle sort keys. */
	FVertexBufferRHIRef KeyBuffers[2];
	/** Shader resource view for particle sort keys. */
	FShaderResourceViewRHIRef KeyBufferSRVs[2];
	/** Unordered access view for particle sort keys. */
	FUnorderedAccessViewRHIRef KeyBufferUAVs[2];

	/** Vertex buffer containing sorted particle vertices. */
	FVertexBufferRHIRef VertexBuffers[2];
	/** Shader resource view for reading particle vertices out of the sorting buffer. */
	FShaderResourceViewRHIRef VertexBufferSRVs[2];
	/** Unordered access view for writing particle vertices in to the sorting buffer. */
	FUnorderedAccessViewRHIRef VertexBufferUAVs[2];
	/** Shader resource view for sorting particle vertices. */
	FShaderResourceViewRHIRef VertexBufferSortSRVs[2];
	/** Unordered access view for sorting particle vertices. */
	FUnorderedAccessViewRHIRef VertexBufferSortUAVs[2];

	/** Size allocated for buffers. */
	int32 BufferSize;
	/** Whether to allocate UAV and SRV as Int32 instead of G16R16F. */
	bool bAsInt32;
};

/**
 * The information required to sort particles belonging to an individual simulation.
 */
struct FParticleSimulationSortInfo
{
	/** Vertex buffer containing indices in to the particle state texture. */
	FRHIShaderResourceView* VertexBufferSRV;
	/** World space position from which to sort. */
	FVector ViewOrigin;
	/** The number of particles in the simulation. */
	uint32 ParticleCount;
};

/**
 * Sort particles on the GPU.
 * @param ParticleSortBuffers - Buffers to use while sorting GPU particles.
 * @param PositionTextureRHI - Texture containing world space position for all particles.
 * @param SimulationsToSort - A list of simulations that must be sorted.
 */
void SortParticlesGPU(
	FRHICommandListImmediate& RHICmdList,
	FParticleSortBuffers& ParticleSortBuffers,
	FRHITexture2D* PositionTextureRHI,
	const TArray<FParticleSimulationSortInfo>& SimulationsToSort,
	ERHIFeatureLevel::Type FeatureLevel
	);
