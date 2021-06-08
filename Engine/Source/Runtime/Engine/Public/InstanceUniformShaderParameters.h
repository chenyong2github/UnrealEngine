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

struct FPrimitiveInstance
{
	FRenderTransform		InstanceToLocal;
	FRenderTransform		PrevInstanceToLocal;
	FRenderTransform		LocalToWorld;
	FRenderTransform		PrevLocalToWorld;
	uint32					LastUpdateSceneFrameNumber;
	FRenderBounds			LocalBounds;
	float					PerInstanceRandom;
	FVector4				LightMapAndShadowMapUVBias;
	uint32					NaniteHierarchyOffset;
	uint32					Flags;

	FORCEINLINE void Orthonormalize()
	{
		// Remove shear
		LocalToWorld.Orthonormalize();
		PrevLocalToWorld.Orthonormalize();

		if (LocalToWorld.RotDeterminant() < 0.0f)
		{
			Flags |= INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN;
		}
		else
		{
			Flags &= ~INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN;
		}
	}

	FORCEINLINE FVector4 GetNonUniformScale() const
	{
		const FVector3f Scale = LocalToWorld.GetScale();
		return FVector4(
			Scale.X, Scale.Y, Scale.Z,
			FMath::Max3(FMath::Abs(Scale.X), FMath::Abs(Scale.Y), FMath::Abs(Scale.Z))
		);
	}

	FORCEINLINE FVector4 GetInvNonUniformScale() const
	{
		const FVector3f Scale = LocalToWorld.GetScale();
		return FVector3f(
			Scale.X > KINDA_SMALL_NUMBER ? 1.0f / Scale.X : 0.0f,
			Scale.Y > KINDA_SMALL_NUMBER ? 1.0f / Scale.Y : 0.0f,
			Scale.Z > KINDA_SMALL_NUMBER ? 1.0f / Scale.Z : 0.0f
		);
	}
};

FORCEINLINE FPrimitiveInstance ConstructPrimitiveInstance(
	const FRenderTransform& LocalToWorld,
	const FRenderTransform& PrevLocalToWorld,
	const FRenderBounds& LocalBounds,
	const FVector4& LightMapAndShadowMapUVBias,
	const uint32 NaniteHierarchyOffset,
	uint32 Flags,
	uint32 LastUpdateSceneFrameNumber,
	float PerInstanceRandom
)
{
	FPrimitiveInstance Result;
	Result.LocalToWorld							= LocalToWorld;
	Result.PrevLocalToWorld						= PrevLocalToWorld;
	Result.LightMapAndShadowMapUVBias			= LightMapAndShadowMapUVBias;
	Result.LocalBounds							= LocalBounds;
	Result.NaniteHierarchyOffset				= NaniteHierarchyOffset;
	Result.LastUpdateSceneFrameNumber			= LastUpdateSceneFrameNumber;
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
				FRenderTransform::Identity,
				FRenderTransform::Identity,
				FRenderBounds(FVector3f::ZeroVector, FVector3f::ZeroVector),
				FVector4(ForceInitToZero),
				0xFFFFFFFFu, /* Nanite Hierarchy Offset */
				0u, /* Instance Flags */
				0xFFFFFFFFu, /* Last Scene Update */
				0.0f /* Per Instance Random */
			),
			0 /* Primitive Id */
		);
	}

	ENGINE_API FInstanceSceneShaderData(const FPrimitiveInstance& Instance, uint32 PrimitiveId);

	ENGINE_API void Setup(const FPrimitiveInstance& Instance, uint32 PrimitiveId);
};

ENGINE_API const FPrimitiveInstance& GetDummyPrimitiveInstance();
ENGINE_API const FInstanceSceneShaderData& GetDummyInstanceSceneShaderData();
