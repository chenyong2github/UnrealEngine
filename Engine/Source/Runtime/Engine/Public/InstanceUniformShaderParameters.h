// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "SceneTypes.h"
#include "RenderResource.h"
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

FORCEINLINE FVector OrthonormalizeTransform(FMatrix& Matrix)
{
	FVector X, Y, Z, Origin;
	Matrix.GetScaledAxes(X, Y, Z);
	Origin = Matrix.GetOrigin();

	// Modified Gram-Schmidt orthogonalization
	Y -= (Y | X) / (X | X) * X;
	Z -= (Z | X) / (X | X) * X;
	Z -= (Z | Y) / (Y | Y) * Y;

	Matrix = FMatrix(X, Y, Z, Origin);

	// Extract per axis scales
	FVector Scale;
	Scale.X = X.Size();
	Scale.Y = Y.Size();
	Scale.Z = Z.Size();
	return Scale;
}

struct FPrimitiveInstance
{
	FMatrix44f			InstanceToLocal; 
	FMatrix44f			PrevInstanceToLocal;
	FMatrix44f			LocalToWorld;
	FMatrix44f			PrevLocalToWorld;
	FVector4			NonUniformScale;
	FVector3f			InvNonUniformScale;
	FBoxSphereBounds	RenderBounds;
	uint32				LastUpdateSceneFrameNumber;
	FBoxSphereBounds	LocalBounds;
	float				PerInstanceRandom;
	FVector4			LightMapAndShadowMapUVBias;
	FNaniteInfo			NaniteInfo;
	uint32				PrimitiveId;
	uint32				Flags;

	FORCEINLINE void OrthonormalizeAndUpdateScale()
	{
		// Remove shear
		const FVector Scale = OrthonormalizeTransform(LocalToWorld);
		OrthonormalizeTransform(PrevLocalToWorld);

		NonUniformScale = FVector4(
			Scale.X, Scale.Y, Scale.Z,
			FMath::Max3(FMath::Abs(Scale.X), FMath::Abs(Scale.Y), FMath::Abs(Scale.Z))
		);

		InvNonUniformScale = FVector3f(
			Scale.X > KINDA_SMALL_NUMBER ? 1.0f / Scale.X : 0.0f,
			Scale.Y > KINDA_SMALL_NUMBER ? 1.0f / Scale.Y : 0.0f,
			Scale.Z > KINDA_SMALL_NUMBER ? 1.0f / Scale.Z : 0.0f
		);

		if (LocalToWorld.RotDeterminant() < 0.0)
		{
			Flags |= INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN;
		}
		else
		{
			Flags &= ~INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN;
		}
	}
};

FORCEINLINE uint32 GetDeterminantSignFlag(float DeterminantSign)
{
	return DeterminantSign < 0.0f ? INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN : 0u;
}

FORCEINLINE FPrimitiveInstance ConstructPrimitiveInstance(
	const FMatrix& LocalToWorld,
	const FMatrix& PrevLocalToWorld,
	const FVector& LocalObjectBoundsMin,
	const FVector& LocalObjectBoundsMax,
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
	Result.LocalToWorld							= (FMatrix44f)LocalToWorld;
	Result.PrevLocalToWorld						= (FMatrix44f)PrevLocalToWorld;
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
			FMatrix::Identity,
			FMatrix::Identity,
			FVector::ZeroVector,
			FVector::ZeroVector,
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
