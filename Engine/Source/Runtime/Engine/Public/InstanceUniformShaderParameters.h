// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "SceneTypes.h"
#include "RenderResource.h"
#include "RenderTransform.h"
#include "ShaderParameters.h"
#include "UniformBuffer.h"
#include "Containers/StaticArray.h"

#define INSTANCE_SCENE_DATA_FLAG_CAST_SHADOWS			0x1
#define INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN		0x2
#define INSTANCE_SCENE_DATA_FLAG_HAS_IMPOSTER			0x4

#define INVALID_LAST_UPDATE_FRAME 0xFFFFFFFFu

struct FPrimitiveInstance
{
	FRenderTransform		LocalToPrimitive;
	FRenderTransform		PrevLocalToPrimitive;
	FRenderBounds			LocalBounds;
	float					PerInstanceRandom;
	FVector4				LightMapAndShadowMapUVBias;
	uint32					NaniteHierarchyOffset;
	uint32					Flags;

	// Should always use this accessor so shearing is properly
	// removed from the concatenated transform.
	FORCEINLINE FRenderTransform ComputeLocalToWorld(const FRenderTransform& PrimitiveToWorld) const
	{
		FRenderTransform LocalToWorld = LocalToPrimitive * PrimitiveToWorld;

	// TODO: Enable when scale is decomposed within FRenderTransform, so we don't have 3x
	// length calls to retrieve the scale when checking for non-uniform scaling.
	#if 0
		// Shearing occurs when applying a rotation and then non-uniform scaling. It is much more likely that 
		// an instance would be non-uniformly scaled than the primitive, so we'll check if the primitive has 
		// non-uniform scaling, and orthonormalize in that case.
		if (PrimitiveToWorld.IsScaleNonUniform())
	#endif
		{
			LocalToWorld.Orthonormalize();
		}

		return LocalToWorld;
	}

	// Should always use this accessor so shearing is properly
	// removed from the concatenated transform.
	FORCEINLINE FRenderTransform ComputePrevLocalToWorld(const FRenderTransform& PrevPrimitiveToWorld) const
	{
		FRenderTransform PrevLocalToWorld = PrevLocalToPrimitive * PrevPrimitiveToWorld;

	// TODO: Enable when scale is decomposed within FRenderTransform, so we don't have 3x
	// length calls to retrieve the scale when checking for non-uniform scaling.
	#if 0
		// Shearing occurs when applying a rotation and then non-uniform scaling. It is much more likely that 
		// an instance would be non-uniformly scaled than the primitive, so we'll check if the primitive has 
		// non-uniform scaling, and orthonormalize in that case.
		if (PrevPrimitiveToWorld.IsScaleNonUniform())
	#endif
		{
			PrevLocalToWorld.Orthonormalize();
		}

		return PrevLocalToWorld;
	}
};

FORCEINLINE FPrimitiveInstance ConstructPrimitiveInstance(
	const FRenderBounds& LocalBounds,
	const FVector4& LightMapAndShadowMapUVBias,
	const uint32 NaniteHierarchyOffset,
	uint32 Flags,
	float PerInstanceRandom
)
{
	FPrimitiveInstance Result;
	Result.LocalToPrimitive.SetIdentity();
	Result.PrevLocalToPrimitive.SetIdentity();
	Result.LightMapAndShadowMapUVBias			= LightMapAndShadowMapUVBias;
	Result.LocalBounds							= LocalBounds;
	Result.NaniteHierarchyOffset				= NaniteHierarchyOffset;
	Result.PerInstanceRandom					= PerInstanceRandom;
	Result.Flags								= Flags;

	return Result;
}

struct FInstanceSceneShaderData
{
	// Must match GetInstanceData() in SceneData.ush
	enum { InstanceDataStrideInFloat4s = 10 };

	TStaticArray<FVector4, InstanceDataStrideInFloat4s> Data;

	FInstanceSceneShaderData()
		: Data(InPlace, NoInit)
	{
		// TODO: Should look into skipping default initialization here - likely unneeded, and just wastes CPU time.
		Setup(
			ConstructPrimitiveInstance(
				FRenderBounds(FVector3f::ZeroVector, FVector3f::ZeroVector),
				FVector4(ForceInitToZero),
				0xFFFFFFFFu, /* Nanite Hierarchy Offset */
				0u, /* Instance Flags */
				0.0f /* Per Instance Random */
			),
			0, /* Primitive Id */
			FRenderTransform::Identity,  /* LocalToWorld */
			FRenderTransform::Identity,  /* PrevLocalToWorld */
			INVALID_LAST_UPDATE_FRAME,
			false /* Has Previous Transform */
		);
	}

	ENGINE_API FInstanceSceneShaderData(
		const FPrimitiveInstance& Instance,
		uint32 PrimitiveId,
		const FRenderTransform& PrimitiveLocalToWorld,
		const FRenderTransform& PrimitivePrevLocalToWorld,
		uint32 LastUpdateFrame,
		bool bHasPreviousTransform
	);

	ENGINE_API void Setup(
		const FPrimitiveInstance& Instance,
		uint32 PrimitiveId,
		const FRenderTransform& PrimitiveLocalToWorld,
		const FRenderTransform& PrimitivePrevLocalToWorld,
		uint32 LastUpdateFrame,
		bool bHasPreviousTransform
	);
};

ENGINE_API const FPrimitiveInstance& GetDummyPrimitiveInstance();
ENGINE_API const FInstanceSceneShaderData& GetDummyInstanceSceneShaderData();
