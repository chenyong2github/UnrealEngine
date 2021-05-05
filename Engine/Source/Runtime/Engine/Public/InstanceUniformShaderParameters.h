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
	FMatrix	 PrevInstanceToLocal;
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
 * Use to initialize the required fields in an FPrimitiveInstance, much of the data is derived at upload time and need not be set (should be refactored).
 */
FORCEINLINE void SetupPrimitiveInstance(FPrimitiveInstance& PrimitiveInstance,
	const FMatrix& LocalToPrimitive /* Transform from the mesh-local space to the primitive/component local space, AKA the instance transform. */,
	const FBoxSphereBounds& LocalInstanceBounds /* Local bounds of the instanced mesh. */,
	const FVector4 &LightMapAndShadowMapUVBias,
	float PerInstanceRandom,
	const FNaniteInfo &NaniteInfo = FNaniteInfo())
{
	PrimitiveInstance.InstanceToLocal = LocalToPrimitive;
	PrimitiveInstance.RenderBounds = LocalInstanceBounds;
	PrimitiveInstance.LightMapAndShadowMapUVBias = LightMapAndShadowMapUVBias;
	PrimitiveInstance.PerInstanceRandom = PerInstanceRandom;
	PrimitiveInstance.NaniteInfo = NaniteInfo;
}

/** 
 * The uniform shader parameters associated with a primitive instance.
 * Note: Must match FInstanceSceneData in shaders.
 * Note 2: Try to keep this 16 byte aligned. i.e |Matrix4x4|Vector3,float|Vector3,float|Vector4|  _NOT_  |Vector3,(waste padding)|Vector3,(waste padding)|Vector3. Or at least mark out padding if it can't be avoided.
 */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FInstanceUniformShaderParameters,ENGINE_API)
	SHADER_PARAMETER(FMatrix44f,  LocalToWorld)
	SHADER_PARAMETER(FMatrix44f,  PrevLocalToWorld)
	SHADER_PARAMETER(FVector4, NonUniformScale)
	SHADER_PARAMETER(FVector4, InvNonUniformScaleAndDeterminantSign)
	SHADER_PARAMETER(FVector3f,  LocalBoundsCenter)
	SHADER_PARAMETER(uint32,   PrimitiveId)
	SHADER_PARAMETER(FVector3f,  LocalBoundsExtent)
	SHADER_PARAMETER(uint32,   LastUpdateSceneFrameNumber)
	SHADER_PARAMETER(uint32,   NaniteRuntimeResourceID)
	SHADER_PARAMETER(uint32,   NaniteHierarchyOffset)
	SHADER_PARAMETER(float,    PerInstanceRandom)
	SHADER_PARAMETER(uint32,   Flags)
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
	Result.LocalToWorld							= (FMatrix44f)LocalToWorld;
	Result.PrevLocalToWorld						= (FMatrix44f)PrevLocalToWorld;
	Result.NonUniformScale						= NonUniformScale;
	Result.InvNonUniformScaleAndDeterminantSign	= InvNonUniformScaleAndDeterminantSign;
	Result.LightMapAndShadowMapUVBias			= LightMapAndShadowMapUVBias;
	Result.PrimitiveId							= PrimitiveId;
	Result.LocalBoundsCenter					= (FVector3f)LocalBoundsCenter;
	Result.LocalBoundsExtent					= (FVector3f)LocalBoundsExtent;
	Result.NaniteRuntimeResourceID				= NaniteInfo.RuntimeResourceID;
	Result.NaniteHierarchyOffset				= NaniteInfo.HierarchyOffset_AndHasImposter >> 1u;
	Result.LastUpdateSceneFrameNumber			= LastUpdateSceneFrameNumber;
	Result.PerInstanceRandom					= PerInstanceRandom;
	Result.Flags								= 0;
	Result.Flags								|= bCastShadow ? INSTANCE_SCENE_DATA_FLAG_CAST_SHADOWS : 0;
	Result.Flags								|= InvNonUniformScaleAndDeterminantSign.W < 0.0f ? INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN : 0;
	Result.Flags								|= (NaniteInfo.HierarchyOffset_AndHasImposter & 1u) != 0u ? INSTANCE_SCENE_DATA_FLAG_HAS_IMPOSTER : 0;
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
	enum { InstanceDataStrideInFloat4s = 10 };

	TStaticArray<FVector4, InstanceDataStrideInFloat4s> Data;

	FInstanceSceneShaderData()
		: Data(InPlace, NoInit)
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
		: Data(InPlace, NoInit)
	{
		Setup(InstanceUniformShaderParameters);
	}

	ENGINE_API FInstanceSceneShaderData(const FPrimitiveInstance& Instance);

	ENGINE_API void Setup(const FInstanceUniformShaderParameters& InstanceUniformShaderParameters);
};
