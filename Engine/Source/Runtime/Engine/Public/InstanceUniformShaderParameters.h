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
#define INSTANCE_SCENE_DATA_FLAG_HAS_EDITOR_DATA			0x80
#define INSTANCE_SCENE_DATA_FLAG_IS_RAYTRACING_FAR_FIELD	0x100

#define INSTANCE_SCENE_DATA_FLAG_PAYLOAD_MASK ( \
	INSTANCE_SCENE_DATA_FLAG_HAS_RANDOM \
	| INSTANCE_SCENE_DATA_FLAG_HAS_CUSTOM_DATA \
	| INSTANCE_SCENE_DATA_FLAG_HAS_DYNAMIC_DATA \
	| INSTANCE_SCENE_DATA_FLAG_HAS_LIGHTSHADOW_UV_BIAS \
	| INSTANCE_SCENE_DATA_FLAG_HAS_HIERARCHY_OFFSET \
	| INSTANCE_SCENE_DATA_FLAG_HAS_LOCAL_BOUNDS \
	| INSTANCE_SCENE_DATA_FLAG_HAS_EDITOR_DATA \
)

#define INVALID_PRIMITIVE_ID 0x000FFFFFu

#define INVALID_LAST_UPDATE_FRAME 0xFFFFFFFFu

#define INSTANCE_SCENE_DATA_COMPRESSED_TRANSFORMS	1

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

struct FInstanceSceneShaderData
{
	// Must match GetInstanceSceneData() in SceneData.ush
#if INSTANCE_SCENE_DATA_COMPRESSED_TRANSFORMS
	// Compressed transform
	enum { DataStrideInFloat4s = 5 }; // TODO: Temporary PrevVelocityHack
#else
	enum { DataStrideInFloat4s = 4 };
#endif

	TStaticArray<FVector4f, DataStrideInFloat4s> Data;

	FInstanceSceneShaderData() : Data(InPlace, NoInit)
	{
	}

	ENGINE_API void Build
	(
		uint32 PrimitiveId,
		uint32 RelativeId,
		uint32 InstanceFlags,
		uint32 LastUpdateFrame,
		uint32 CustomDataCount,
		float RandomID
	);

	ENGINE_API void Build
	(
		uint32 PrimitiveId,
		uint32 RelativeId,
		uint32 InstanceFlags,
		uint32 LastUpdateFrame,
		uint32 CustomDataCount,
		float RandomID,
		const FRenderTransform& LocalToPrimitive,
		const FRenderTransform& PrimitiveToWorld,
		const FRenderTransform& PrevPrimitiveToWorld // TODO: Temporary PrevVelocityHack
	);

	ENGINE_API void BuildInternal
	(
		uint32 PrimitiveId,
		uint32 RelativeId,
		uint32 InstanceFlags,
		uint32 LastUpdateFrame,
		uint32 CustomDataCount,
		float RandomID,
		const FRenderTransform& LocalToWorld,
		const FRenderTransform& PrevLocalToWorld // Assumes shear has been removed already // TODO: Temporary PrevVelocityHack
	);
};
