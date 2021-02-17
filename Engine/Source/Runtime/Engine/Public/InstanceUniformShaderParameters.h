// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "SceneTypes.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "UniformBuffer.h"

class FNaniteInfo
{
public:
	uint32 RuntimeResourceID;
	uint32 HierarchyOffset_AndHasImposter;

	FNaniteInfo()
	: RuntimeResourceID(0xFFFFFFFFu)
	, HierarchyOffset_AndHasImposter(0xFFFFFFFFu)
	{
	}

	FNaniteInfo(uint32 InRuntimeResourceID, int32 InHierarchyOffset, bool bHasImposter)
	: RuntimeResourceID(InRuntimeResourceID)
	, HierarchyOffset_AndHasImposter((InHierarchyOffset << 1) | (bHasImposter ? 1u : 0u))
	{
	}
};

struct FPrimitiveInstance
{
	FMatrix  InstanceToLocal;
	FMatrix  LocalToInstance;
	FMatrix  LocalToWorld;
	FMatrix  PrevLocalToWorld;
	FVector4 NonUniformScale;
	FVector4 InvNonUniformScaleAndDeterminantSign;
	FBoxSphereBounds RenderBounds;
	FBoxSphereBounds LocalBounds;
	FVector4 LightMapAndShadowMapUVBias;
	uint32 PrimitiveId;
	FNaniteInfo NaniteInfo;
	uint32 LastUpdateSceneFrameNumber;
	float PerInstanceRandom;
	uint32 Flags;
};

/** 
 * The uniform shader parameters associated with a primitive instance.
 * Note: Must match FInstanceSceneData in shaders.
 * Note 2: Try to keep this 16 byte aligned. i.e |Matrix4x4|Vector3,float|Vector3,float|Vector4|  _NOT_  |Vector3,(waste padding)|Vector3,(waste padding)|Vector3. Or at least mark out padding if it can't be avoided.
 */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FInstanceUniformShaderParameters,ENGINE_API)
	SHADER_PARAMETER(FMatrix,  LocalToWorld)
	SHADER_PARAMETER(FMatrix,  PrevLocalToWorld)
	SHADER_PARAMETER(FVector4, NonUniformScale)
	SHADER_PARAMETER(FVector4, InvNonUniformScaleAndDeterminantSign)
	SHADER_PARAMETER(FVector,  LocalBoundsCenter)
	SHADER_PARAMETER(uint32,   PrimitiveId)
	SHADER_PARAMETER(FVector,  LocalBoundsExtent)
	SHADER_PARAMETER(uint32,   LastUpdateSceneFrameNumber)
	SHADER_PARAMETER(uint32,   NaniteRuntimeResourceID)
	SHADER_PARAMETER(uint32,   NaniteHierarchyOffset_AndHasImposter)
	SHADER_PARAMETER(float,    PerInstanceRandom)
	SHADER_PARAMETER(uint32,   Flags)			// CastShadows=1,
	SHADER_PARAMETER(FVector4, LightMapAndShadowMapUVBias)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

/** Initializes the instance uniform shader parameters. */
inline FInstanceUniformShaderParameters GetInstanceUniformShaderParameters(
	const FMatrix&  LocalToWorld,
	const FMatrix&  PrevLocalToWorld,
	const FVector&  LocalBoundsCenter,
	const FVector&  LocalBoundsExtent,
	const FVector4& NonUniformScale,
	const FVector4& InvNonUniformScaleAndDeterminantSign,
	const FVector4& LightMapAndShadowMapUVBias,
	const FNaniteInfo& NaniteInfo,
	uint32 PrimitiveId,
	uint32 LastUpdateSceneFrameNumber,
	float PerInstanceRandom,
	bool bCastShadow
)
{
	FInstanceUniformShaderParameters Result;
	Result.LocalToWorld							= LocalToWorld;
	Result.PrevLocalToWorld						= PrevLocalToWorld;
	Result.NonUniformScale						= NonUniformScale;
	Result.InvNonUniformScaleAndDeterminantSign	= InvNonUniformScaleAndDeterminantSign;
	Result.LightMapAndShadowMapUVBias			= LightMapAndShadowMapUVBias;
	Result.PrimitiveId							= PrimitiveId;
	Result.LocalBoundsCenter					= LocalBoundsCenter;
	Result.LocalBoundsExtent					= LocalBoundsExtent;
	Result.NaniteRuntimeResourceID				= NaniteInfo.RuntimeResourceID;
	Result.NaniteHierarchyOffset_AndHasImposter	= NaniteInfo.HierarchyOffset_AndHasImposter;
	Result.LastUpdateSceneFrameNumber			= LastUpdateSceneFrameNumber;
	Result.PerInstanceRandom					= PerInstanceRandom;
	Result.Flags								= 0;
	Result.Flags								|= bCastShadow ? 1 : 0;
	return Result;
}

inline TUniformBufferRef<FInstanceUniformShaderParameters> CreateInstanceUniformBufferImmediate(
	const FMatrix&  LocalToWorld,
	const FMatrix&  PrevLocalToWorld,
	const FVector&  LocalBoundsCenter,
	const FVector&  LocalBoundsExtent,
	const FVector4& NonUniformScale,
	const FVector4& InvNonUniformScaleAndDeterminantSign,
	const FVector4& LightMapAndShadowMapUVBias,
	const FNaniteInfo& NaniteInfo,
	uint32 PrimitiveId,
	uint32 LastUpdateSceneFrameNumber,
	float PerInstanceRandom,
	bool bCastShadow
)
{
	check(IsInRenderingThread());
	return TUniformBufferRef<FInstanceUniformShaderParameters>::CreateUniformBufferImmediate(
		GetInstanceUniformShaderParameters(
			LocalToWorld,
			PrevLocalToWorld,
			LocalBoundsCenter,
			LocalBoundsExtent,
			NonUniformScale,
			InvNonUniformScaleAndDeterminantSign,
			LightMapAndShadowMapUVBias,
			NaniteInfo,
			PrimitiveId,
			LastUpdateSceneFrameNumber,
			PerInstanceRandom,
			bCastShadow
		),
		UniformBuffer_MultiFrame
	);
}

struct FInstanceSceneShaderData
{
	// Must match GetInstanceData() in SceneData.ush
	enum { InstanceDataStrideInFloat4s = 12 };

	FVector4 Data[InstanceDataStrideInFloat4s];

	FInstanceSceneShaderData()
	{
		Setup(GetInstanceUniformShaderParameters(
			FMatrix::Identity,
			FMatrix::Identity,
			FVector::ZeroVector,
			FVector::ZeroVector,
			FVector4(1.0f, 1.0f, 1.0f, 1.0f),
			FVector4(1.0f, 1.0f, 1.0f, 1.0f),
			FVector4(ForceInitToZero),
			FNaniteInfo(),
			0,
			0xFFFFFFFFu,
			0.0f,
			true
		));
	}

	explicit FInstanceSceneShaderData(const FInstanceUniformShaderParameters& InstanceUniformShaderParameters)
	{
		Setup(InstanceUniformShaderParameters);
	}

	ENGINE_API FInstanceSceneShaderData(const FPrimitiveInstance& Instance);

	ENGINE_API void Setup(const FInstanceUniformShaderParameters& InstanceUniformShaderParameters);
};
