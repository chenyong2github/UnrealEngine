// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderResource.h"


class FMorphTargetVertexInfoBuffers : public FRenderResource
{
public:
	FMorphTargetVertexInfoBuffers() : NumTotalWorkItems(0)
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

	uint32 GetNumWorkItems(uint32 index = UINT_MAX) const
	{
		check(index == UINT_MAX || index < (uint32)WorkItemsPerMorph.Num());
		return index != UINT_MAX ? WorkItemsPerMorph[index] : NumTotalWorkItems;
	}

	uint32 GetNumMorphs() const
	{
		return WorkItemsPerMorph.Num();
	}

	uint32 GetStartOffset(uint32 Index) const
	{
		check(Index < (uint32)StartOffsetPerMorph.Num());
		return StartOffsetPerMorph[Index];
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

	uint32 GetNumSplitsPerMorph(uint32 Index) const
	{
		check(Index < (uint32)NumSplitsPerMorph.Num());
		return NumSplitsPerMorph[Index];
	}

	FVertexBufferRHIRef VertexIndicesVB;
	FShaderResourceViewRHIRef VertexIndicesSRV;

	FVertexBufferRHIRef MorphDeltasVB;
	FShaderResourceViewRHIRef MorphDeltasSRV;

	// Changes to this struct must be reflected in MorphTargets.usf
	struct FMorphDelta
	{
		FMorphDelta(FVector InPosDelta, FVector InTangentDelta)
		{
			PosDelta[0] = FFloat16(InPosDelta.X);
			PosDelta[1] = FFloat16(InPosDelta.Y);
			PosDelta[2] = FFloat16(InPosDelta.Z);

			TangentDelta[0] = FFloat16(InTangentDelta.X);
			TangentDelta[1] = FFloat16(InTangentDelta.Y);
			TangentDelta[2] = FFloat16(InTangentDelta.Z);
		}

		FFloat16 PosDelta[3];
		FFloat16 TangentDelta[3];
	};

	void Reset()
	{
		VertexIndices.Empty();
		MorphDeltas.Empty();
		MaximumValuePerMorph.Empty();
		MinimumValuePerMorph.Empty();
		StartOffsetPerMorph.Empty();
		WorkItemsPerMorph.Empty();
		NumSplitsPerMorph.Empty();
		TempStoreSize = 0;
		NumTotalWorkItems = 0;
	}

protected:

	// Transient data used while creating the vertex buffers, gets deleted as soon as VB gets initialized.
	TArray<uint32> VertexIndices;
	// Transient data used while creating the vertex buffers, gets deleted as soon as VB gets initialized.
	TArray<FMorphDelta> MorphDeltas;

	//x,y,y separate for position and shared w for tangent
	TArray<FVector4> MaximumValuePerMorph;
	TArray<FVector4> MinimumValuePerMorph;
	TArray<uint32> StartOffsetPerMorph;
	TArray<uint32> WorkItemsPerMorph;
	TArray<uint32> NumSplitsPerMorph; //splits due to too large dispatch size
	uint32 TempStoreSize;

	uint32 NumTotalWorkItems;

	friend class FSkeletalMeshLODRenderData;
};
