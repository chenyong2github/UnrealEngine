// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "RenderGraphResources.h"

struct FHairStrandsInterpolationInput
{
	struct FHairGroup
	{
		FRWBuffer*	RenderRestPosePositionBuffer = nullptr;
		FRWBuffer*	RenderAttributeBuffer = nullptr;
		uint32		RenderVertexCount = 0;

		FRWBuffer*	SimRestPosePositionBuffer = nullptr;
		FRWBuffer*	SimAttributeBuffer = nullptr;
		uint32		SimVertexCount = 0;

		FRWBuffer*	Interpolation0Buffer = nullptr;
		FRWBuffer*	Interpolation1Buffer = nullptr;

		// For debug purpose only
		FRWBuffer*	SimRootPointIndexBuffer = nullptr;

		#if RHI_RAYTRACING
		FRayTracingGeometry*	RaytracingGeometry = nullptr;
		FRWBuffer*				RaytracingPositionBuffer = nullptr;
		uint32					RaytracingVertexCount = 0;
		bool					bIsRTGeometryInitialized = false;
		#endif

		bool bIsSimulationEnable = false;
		float HairRadius = 0;
		float HairRaytracingRadiusScale = 0;
		FVector InRenderHairPositionOffset = FVector::ZeroVector;
		FVector InSimHairPositionOffset = FVector::ZeroVector;
		FVector OutHairPositionOffset = FVector::ZeroVector;
		FVector OutHairPreviousPositionOffset = FVector::ZeroVector;

		inline bool IsValid() const
		{
			return
				(RenderRestPosePositionBuffer && RenderRestPosePositionBuffer->SRV) &&
				(RenderAttributeBuffer && RenderAttributeBuffer->SRV) &&
				(SimRestPosePositionBuffer && SimRestPosePositionBuffer->SRV) &&
				(SimAttributeBuffer && SimAttributeBuffer->SRV) &&
				(Interpolation0Buffer && Interpolation0Buffer->SRV) &&
				(Interpolation1Buffer && Interpolation1Buffer->SRV) &&
				RenderVertexCount != 0 &&
				SimVertexCount != 0;
		}
	};
	TArray<FHairGroup> HairGroups;
};

struct FHairStrandsInterpolationOutput
{
	// Input to the strand vertex factory. This allows to 
	// abstract resources generation when debug mode are used
	struct VertexFactoryInput
	{
		FShaderResourceViewRHIRef HairPositionBuffer = nullptr;
		FShaderResourceViewRHIRef HairPreviousPositionBuffer = nullptr;
		FShaderResourceViewRHIRef HairTangentBuffer = nullptr;
		FShaderResourceViewRHIRef HairAttributeBuffer = nullptr;
		FShaderResourceViewRHIRef HairMaterialBuffer = nullptr;
		FVector HairPositionOffset = FVector::ZeroVector;
		FVector HairPreviousPositionOffset = FVector::ZeroVector;
		uint32 VertexCount = 0;

		inline void Reset()
		{
			HairPositionBuffer = nullptr;
			HairPreviousPositionBuffer = nullptr;
			HairTangentBuffer = nullptr;
			HairAttributeBuffer = nullptr;
			HairMaterialBuffer = nullptr;
			HairPositionOffset = FVector::ZeroVector;
			HairPreviousPositionOffset = FVector::ZeroVector;
			VertexCount = 0;
		}
	};

	struct HairGroup
	{
		FRWBuffer* SimDeformedPositionBuffer[2] = { nullptr, nullptr };
		FRWBuffer* RenderDeformedPositionBuffer[2] = { nullptr, nullptr };
		uint32 CurrentIndex = 0;

		FRWBuffer* RenderTangentBuffer = nullptr;
		FRWBuffer* RenderAttributeBuffer = nullptr;
		FRWBuffer* RenderMaterialBuffer = nullptr;

		FRWBuffer* SimTangentBuffer = nullptr;

		// Debug buffers (allocated on-the-fly if used)
		FRWBuffer RenderPatchedAttributeBuffer;

		VertexFactoryInput VFInput;

		inline bool IsValid() const
		{
			return
				(SimDeformedPositionBuffer[0] && SimDeformedPositionBuffer[0]->SRV) &&
				(SimDeformedPositionBuffer[1] && SimDeformedPositionBuffer[1]->SRV) &&
				(RenderDeformedPositionBuffer[0] && RenderDeformedPositionBuffer[0]->SRV) &&
				(RenderDeformedPositionBuffer[1] && RenderDeformedPositionBuffer[1]->SRV) &&
				(RenderTangentBuffer && RenderTangentBuffer->SRV) &&
				(RenderAttributeBuffer && RenderAttributeBuffer->SRV);
				(RenderMaterialBuffer && RenderMaterialBuffer->SRV);
		}
	};
	TArray<HairGroup> HairGroups;
};

void ComputeHairStrandsInterpolation(
	FRHICommandListImmediate& RHICmdList,
	FHairStrandsInterpolationInput* Input,
	FHairStrandsInterpolationOutput* Output, 
	struct FHairStrandsProjectionHairData& RenHairDatas,
	struct FHairStrandsProjectionHairData& SimHairDatas,
	int32 LODIndex);

