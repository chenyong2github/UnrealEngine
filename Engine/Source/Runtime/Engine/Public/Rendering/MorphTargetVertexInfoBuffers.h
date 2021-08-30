// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderResource.h"


class FMorphTargetVertexInfoBuffers : public FRenderResource
{
public:
	FMorphTargetVertexInfoBuffers() : NumTotalBatches(0)
	{
	}

	ENGINE_API virtual void InitRHI() override;
	ENGINE_API virtual void ReleaseRHI() override;

	static uint32 GetMaximumThreadGroupSize()
	{
		//D3D11 there can be at most 65535 Thread Groups in each dimension of a Dispatch call.
		uint64 MaximumThreadGroupSize = uint64(GMaxComputeDispatchDimension) * 32ull;
		return uint32(FMath::Min<uint64>(MaximumThreadGroupSize, UINT32_MAX));
	}

	uint32 GetNumBatches(uint32 index = UINT_MAX) const
	{
		check(index == UINT_MAX || index < (uint32)BatchesPerMorph.Num());
		return index != UINT_MAX ? BatchesPerMorph[index] : NumTotalBatches;
	}

	uint32 GetNumMorphs() const
	{
		return BatchesPerMorph.Num();
	}

	uint32 GetBatchStartOffset(uint32 Index) const
	{
		check(Index < (uint32)BatchStartOffsetPerMorph.Num());
		return BatchStartOffsetPerMorph[Index];
	}

	const FVector4& GetMaximumMorphScale(uint32 Index) const
	{
		check(Index < (uint32)MaximumValuePerMorph.Num());
		return MaximumValuePerMorph[Index];
	}

	const FVector4& GetMinimumMorphScale(uint32 Index) const
	{
		check(Index < (uint32)MinimumValuePerMorph.Num());
		return MinimumValuePerMorph[Index];
	}

	const float GetPositionPrecision() const
	{
		return PositionPrecision;
	}

	const float GetTangentZPrecision() const
	{
		return TangentZPrecision;
	}

	FBufferRHIRef MorphDataBuffer;
	FShaderResourceViewRHIRef MorphDataSRV;

	void Reset()
	{
		MorphData.Empty();
		MaximumValuePerMorph.Empty();
		MinimumValuePerMorph.Empty();
		BatchStartOffsetPerMorph.Empty();
		BatchesPerMorph.Empty();		
		NumTotalBatches = 0;
		PositionPrecision = 0.0f;
		TangentZPrecision = 0.0f;
	}

protected:

	// Transient data. Gets deleted as soon as the GPU resource has been initialized.
	TArray<uint32> MorphData;

	//x,y,y separate for position and shared w for tangent
	TArray<FVector4> MaximumValuePerMorph;
	TArray<FVector4> MinimumValuePerMorph;
	TArray<uint32> BatchStartOffsetPerMorph;
	TArray<uint32> BatchesPerMorph;
	
	uint32 NumTotalBatches;
	float PositionPrecision;
	float TangentZPrecision;

	friend class FSkeletalMeshLODRenderData;
};
