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

class FNaniteInfo
{
public:
	uint32 RuntimeResourceID;
	uint32 HierarchyOffset;
	uint8  bHasImposter : 1;

	FNaniteInfo()
	: RuntimeResourceID(0xFFFFFFFFu)
	, HierarchyOffset(0xFFFFFFFFu)
	, bHasImposter(false)
	{
	}

	FNaniteInfo(uint32 InRuntimeResourceID, int32 InHierarchyOffset, bool bInHasImposter)
	: RuntimeResourceID(InRuntimeResourceID)
	, HierarchyOffset(InHierarchyOffset)
	, bHasImposter(bInHasImposter)
	{
	}
};

struct FPrimitiveInstance
{
	FRenderTransform		InstanceToLocal;
	FRenderTransform		PrevInstanceToLocal;
	FRenderTransform		LocalToWorld;
	FRenderTransform		PrevLocalToWorld;
	FVector4				NonUniformScale;
	FVector3f				InvNonUniformScale;
	FBoxSphereBounds		RenderBounds;
	uint32					LastUpdateSceneFrameNumber;
	FBoxSphereBounds		LocalBounds;
	float					PerInstanceRandom;
	FVector4				LightMapAndShadowMapUVBias;
	FNaniteInfo				NaniteInfo;
	uint32					PrimitiveId;
	uint32					Flags;

	FORCEINLINE void OrthonormalizeAndUpdateScale()
	{
		// Remove shear
		const FVector3f Scale = LocalToWorld.Orthonormalize();
		PrevLocalToWorld.Orthonormalize();

		NonUniformScale = FVector4(
			Scale.X, Scale.Y, Scale.Z,
			FMath::Max3(FMath::Abs(Scale.X), FMath::Abs(Scale.Y), FMath::Abs(Scale.Z))
		);

		InvNonUniformScale = FVector3f(
			Scale.X > KINDA_SMALL_NUMBER ? 1.0f / Scale.X : 0.0f,
			Scale.Y > KINDA_SMALL_NUMBER ? 1.0f / Scale.Y : 0.0f,
			Scale.Z > KINDA_SMALL_NUMBER ? 1.0f / Scale.Z : 0.0f
		);

		if (LocalToWorld.RotDeterminant() < 0.0f)
		{
			Flags |= INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN;
		}
		else
		{
			Flags &= ~INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN;
		}
	}
};

FORCEINLINE FPrimitiveInstance ConstructPrimitiveInstance(
	const FRenderTransform& LocalToWorld,
	const FRenderTransform& PrevLocalToWorld,
	const FVector3f& LocalObjectBoundsMin,
	const FVector3f& LocalObjectBoundsMax,
	const FVector4& NonUniformScale,
	const FVector3f& InvNonUniformScale,
	const FVector4& LightMapAndShadowMapUVBias,
	const FNaniteInfo& NaniteInfo,
	uint32 Flags,
	uint32 PrimitiveId,
	uint32 LastUpdateSceneFrameNumber,
	float PerInstanceRandom
)
{
	const FBox LocalObjectBounds = FBox(LocalObjectBoundsMin, LocalObjectBoundsMax);

	FPrimitiveInstance Result;
	Result.LocalToWorld							= LocalToWorld;
	Result.PrevLocalToWorld						= PrevLocalToWorld;
	Result.NonUniformScale						= NonUniformScale;
	Result.InvNonUniformScale					= InvNonUniformScale;
	Result.LightMapAndShadowMapUVBias			= LightMapAndShadowMapUVBias;
	Result.PrimitiveId							= PrimitiveId;
	Result.LocalBounds							= LocalObjectBounds;
	Result.RenderBounds							= Result.LocalBounds;
	Result.NaniteInfo							= NaniteInfo;
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
		Setup(ConstructPrimitiveInstance(
			FRenderTransform::Identity,
			FRenderTransform::Identity,
			FVector3f::ZeroVector,
			FVector3f::ZeroVector,
			FVector4(1.0f, 1.0f, 1.0f, 1.0f),
			FVector3f(1.0f, 1.0f, 1.0f),
			FVector4(ForceInitToZero),
			FNaniteInfo(),
			0u, /* Instance Flags */
			0,
			0xFFFFFFFFu,
			0.0f
		));
	}

	ENGINE_API FInstanceSceneShaderData(const FPrimitiveInstance& Instance);

	ENGINE_API void Setup(const FPrimitiveInstance& Instance);
};

ENGINE_API const FPrimitiveInstance& GetDummyPrimitiveInstance();
ENGINE_API const FInstanceSceneShaderData& GetDummyInstanceSceneShaderData();
