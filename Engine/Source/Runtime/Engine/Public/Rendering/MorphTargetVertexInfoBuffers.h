// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderResource.h"

struct FSkelMeshRenderSection;
class UMorphTarget;

class FMorphTargetVertexInfoBuffers : public FRenderResource
{
public:
	FMorphTargetVertexInfoBuffers() : NumTotalBatches(0)
	{
	}

	void InitMorphResources(EShaderPlatform ShaderPlatform, const TArray<FSkelMeshRenderSection>& RenderSections, const TArray<UMorphTarget*>& MorphTargets, int NumVertices, int32 LODIndex, float TargetPositionErrorTolerance);

	inline bool IsMorphResourcesInitialized() const { return bResourcesInitialized; }
	inline bool IsRHIIntialized() const { return bRHIIntialized; }
	inline bool IsMorphCPUDataValid() const{ return bIsMorphCPUDataValid; }

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

	const FVector4f& GetMaximumMorphScale(uint32 Index) const
	{
		check(Index < (uint32)MaximumValuePerMorph.Num());
		return MaximumValuePerMorph[Index];
	}

	const FVector4f& GetMinimumMorphScale(uint32 Index) const
	{
		check(Index < (uint32)MinimumValuePerMorph.Num());
		return MinimumValuePerMorph[Index];
	}

	const float GetPositionPrecision() const
	{
		return PositionPrecision;
	}

	static const float CalculatePositionPrecision(float TargetPositionErrorTolerance);

	const float GetTangentZPrecision() const
	{
		return TangentZPrecision;
	}

	static bool IsPlatformShaderSupported(EShaderPlatform ShaderPlatform);

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
		bResourcesInitialized = false;
		bRHIIntialized = false;
		bIsMorphCPUDataValid = false;
	}

protected:

	void ValidateVertexBuffers(bool bMorphTargetsShouldBeValid);
	void Serialize(FArchive& Ar);

	// Transient data. Gets deleted as soon as the GPU resource has been initialized.
	TArray<uint32> MorphData;

	//x,y,y separate for position and shared w for tangent
	TArray<FVector4f> MaximumValuePerMorph;
	TArray<FVector4f> MinimumValuePerMorph;
	TArray<uint32> BatchStartOffsetPerMorph;
	TArray<uint32> BatchesPerMorph;
	
	uint32 NumTotalBatches;
	float PositionPrecision;
	float TangentZPrecision;

	bool bIsMorphCPUDataValid = false;
	bool bResourcesInitialized = false;
	bool bRHIIntialized = false;

	friend class FSkeletalMeshLODRenderData;
	friend FArchive& operator<<(FArchive& Ar, FMorphTargetVertexInfoBuffers& MorphTargetVertexInfoBuffers);
};

FArchive& operator<<(FArchive& Ar, FMorphTargetVertexInfoBuffers& MorphTargetVertexInfoBuffers);
