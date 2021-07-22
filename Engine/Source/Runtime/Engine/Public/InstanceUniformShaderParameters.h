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

#define INSTANCE_SCENE_DATA_FLAG_CAST_SHADOWS				0x1
#define INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN			0x2
#define INSTANCE_SCENE_DATA_FLAG_HAS_IMPOSTER				0x4
#define INSTANCE_SCENE_DATA_FLAG_HAS_RANDOM					0x8
#define INSTANCE_SCENE_DATA_FLAG_HAS_CUSTOM_DATA			0x10
#define INSTANCE_SCENE_DATA_FLAG_HAS_DYNAMIC_DATA			0x20
#define INSTANCE_SCENE_DATA_FLAG_HAS_LIGHTSHADOW_UV_BIAS	0x40
#define INSTANCE_SCENE_DATA_FLAG_HAS_HIERARCHY_OFFSET		0x80

#define INVALID_LAST_UPDATE_FRAME 0xFFFFFFFFu

struct FPrimitiveInstance
{
	FRenderTransform		LocalToPrimitive;
	FRenderBounds			LocalBounds;  // TODO: Move to another data stream (only if proxies like geometry collection require it).
	uint32					NaniteHierarchyOffset; // TODO: Move to another data stream (only if proxies like geometry collection require it).
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
};

struct FPrimitiveInstanceDynamicData
{
	FRenderTransform PrevLocalToPrimitive;

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
	uint32 NaniteHierarchyOffset,
	uint32 Flags
)
{
	FPrimitiveInstance Result;
	Result.LocalToPrimitive.SetIdentity();
	Result.LocalBounds							= LocalBounds;
	Result.NaniteHierarchyOffset				= NaniteHierarchyOffset;
	Result.Flags								= Flags;

	return Result;
}

struct FInstanceSceneShaderData
{
	// Must match GetInstanceSceneData() in SceneData.ush
	enum { DataStrideInFloat4s = 10 };

	TStaticArray<FVector4, DataStrideInFloat4s> Data;

	FInstanceSceneShaderData()
		: Data(InPlace, NoInit)
	{
		// TODO: Should look into skipping default initialization here - likely unneeded, and just wastes CPU time.
		Setup(
			ConstructPrimitiveInstance(
				FRenderBounds(FVector3f::ZeroVector, FVector3f::ZeroVector),
				0xFFFFFFFFu, /* Nanite Hierarchy Offset */
				0u /* Instance Flags */
			),
			0, /* Primitive Id */
			FRenderTransform::Identity,  /* LocalToWorld */
			FRenderTransform::Identity,  /* PrevLocalToWorld */
			FRenderTransform::Identity, /* PrevLocalToPrimitive */ // TODO: Temporary
			FVector4(ForceInitToZero), /* Lightmap and Shadowmap UV Bias */ // TODO: Temporary
			0.0f, /* Per Instance Random */ // TODO: Temporary
			0.0f, /* Custom Data Float0 */ // TODO: Temporary Hack!
			INVALID_LAST_UPDATE_FRAME
		);
	}

	ENGINE_API FInstanceSceneShaderData(
		const FPrimitiveInstance& Instance,
		uint32 PrimitiveId,
		const FRenderTransform& PrimitiveLocalToWorld,
		const FRenderTransform& PrimitivePrevLocalToWorld,
		const FRenderTransform& PrevLocalToPrimitive, // TODO: Temporary
		const FVector4& LightMapShadowMapUVBias, // TODO: Temporary
		float RandomID, // TODO: Temporary
		float CustomDataFloat0, // TODO: Temporary Hack!
		uint32 LastUpdateFrame
	);

	ENGINE_API void Setup(
		const FPrimitiveInstance& Instance,
		uint32 PrimitiveId,
		const FRenderTransform& PrimitiveLocalToWorld,
		const FRenderTransform& PrimitivePrevLocalToWorld,
		const FRenderTransform& PrevLocalToPrimitive, // TODO: Temporary
		const FVector4& LightMapShadowMapUVBias, // TODO: Temporary
		float RandomID, // TODO: Temporary
		float CustomDataFloat0, // TODO: Temporary Hack!
		uint32 LastUpdateFrame
	);
};

ENGINE_API const FInstanceSceneShaderData& GetDummyInstanceSceneShaderData();
