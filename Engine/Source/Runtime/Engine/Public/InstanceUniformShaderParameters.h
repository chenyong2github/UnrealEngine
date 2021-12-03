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

// Must match SceneData.ush
#define INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN			0x1
#define INSTANCE_SCENE_DATA_FLAG_HAS_RANDOM					0x2
#define INSTANCE_SCENE_DATA_FLAG_HAS_CUSTOM_DATA			0x4
#define INSTANCE_SCENE_DATA_FLAG_HAS_DYNAMIC_DATA			0x8
#define INSTANCE_SCENE_DATA_FLAG_HAS_LIGHTSHADOW_UV_BIAS	0x10
#define INSTANCE_SCENE_DATA_FLAG_HAS_HIERARCHY_OFFSET		0x20
#define INSTANCE_SCENE_DATA_FLAG_HAS_LOCAL_BOUNDS			0x40

#define INVALID_PRIMITIVE_ID 0x000FFFFFu

#define INVALID_LAST_UPDATE_FRAME 0xFFFFFFFFu

#define INSTANCE_COMPRESSED_TRANSFORM	0

// TODO: Rename to FInstanceSceneData
struct FPrimitiveInstance
{
	FRenderTransform LocalToPrimitive;

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
		// non-uniform scaling, and orthogonalize in that case.
		if (PrimitiveToWorld.IsScaleNonUniform())
	#endif
		{
			LocalToWorld.Orthogonalize();
		}

		return LocalToWorld;
	}
};

// TODO: Rename to FInstanceDynamicData
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
		// non-uniform scaling, and orthogonalize in that case.
		if (PrevPrimitiveToWorld.IsScaleNonUniform())
	#endif
		{
			PrevLocalToWorld.Orthogonalize();
		}

		return PrevLocalToWorld;
	}
};

FORCEINLINE FPrimitiveInstance ConstructPrimitiveInstance()
{
	FPrimitiveInstance Result;
	Result.LocalToPrimitive.SetIdentity();
	return Result;
}

struct FInstanceSceneShaderData
{
	// Must match GetInstanceSceneData() in SceneData.ush
#if INSTANCE_COMPRESSED_TRANSFORM
	enum { DataStrideInFloat4s = 8 };
#else
	enum { DataStrideInFloat4s = 10 };
#endif

	TStaticArray<FVector4f, DataStrideInFloat4s> Data;

	FInstanceSceneShaderData()
		: Data(InPlace, NoInit)
	{
		// TODO: Should look into skipping default initialization here - likely unneeded, and just wastes CPU time.
		Setup(
			ConstructPrimitiveInstance(),
			0, /* Primitive Id */
			FRenderTransform::Identity,  /* LocalToWorld */
			FRenderTransform::Identity,  /* PrevLocalToWorld */
			FRenderTransform::Identity, /* PrevLocalToPrimitive */ // TODO: Temporary
			FRenderBounds(FVector3f::ZeroVector, FVector3f::ZeroVector), /* Local Bounds */ // TODO: Temporary
			0xFFFFFFFFu, /* Nanite Hierarchy Offset */ // TODO: Temporary
			FVector4f(ForceInitToZero), /* Lightmap and Shadowmap UV Bias */ // TODO: Temporary
			0.0f, /* Per Instance Random */ // TODO: Temporary
			0.0f, /* Custom Data Float0 */ // TODO: Temporary Hack!
			INVALID_LAST_UPDATE_FRAME,
			0 /* Instance Flags */ // TODO: Temporary
		);
	}

	ENGINE_API FInstanceSceneShaderData(
		const FPrimitiveInstance& Instance,
		uint32 PrimitiveId,
		const FRenderTransform& PrimitiveLocalToWorld,
		const FRenderTransform& PrimitivePrevLocalToWorld,
		const FRenderTransform& PrevLocalToPrimitive, // TODO: Temporary
		const FRenderBounds& LocalBounds, // TODO: Temporary
		const uint32 HierarchyOffset, // TODO: Temporary
		const FVector4f& LightMapShadowMapUVBias, // TODO: Temporary
		float RandomID, // TODO: Temporary
		float CustomDataFloat0, // TODO: Temporary Hack!
		uint32 LastUpdateFrame,
		uint32 InstanceFlags // TODO: Temporary
	);

	ENGINE_API FInstanceSceneShaderData(
		const FPrimitiveInstance& Instance,
		uint32 PrimitiveId,
		const FMatrix& PrimitiveLocalToWorld,
		const FMatrix& PrimitivePrevLocalToWorld,
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
		const FRenderBounds& LocalBounds, // TODO: Temporary
		const uint32 HierarchyOffset, // TODO: Temporary
		const FVector4f& LightMapShadowMapUVBias, // TODO: Temporary
		float RandomID, // TODO: Temporary
		float CustomDataFloat0, // TODO: Temporary Hack!
		uint32 LastUpdateFrame,
		uint32 InstanceFlags // TODO: Temporary
	);
};

ENGINE_API const FInstanceSceneShaderData& GetDummyInstanceSceneShaderData();
