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
	int32 HierarchyOffset;

	FNaniteInfo()
	: RuntimeResourceID(0xFFFFFFFFu)
	, HierarchyOffset(-1)
	{
	}

	FNaniteInfo(uint32 InRuntimeResourceID, int32 InHierarchyOffset)
	: RuntimeResourceID(InRuntimeResourceID)
	, HierarchyOffset(InHierarchyOffset)
	{
	}
};

struct FPrimitiveInstance
{
	FMatrix  InstanceToLocal;
	FMatrix  LocalToInstance;
	FMatrix  LocalToWorld;
	FMatrix  PrevLocalToWorld;
	FMatrix  WorldToLocal;
	FVector4 NonUniformScale;
	FVector4 InvNonUniformScaleAndDeterminantSign;
	FBoxSphereBounds RenderBounds;
	FBoxSphereBounds LocalBounds;
	uint32 PrimitiveId;
	FNaniteInfo NaniteInfo;
};

/** 
 * The uniform shader parameters associated with a primitive instance.
 * Note: Must match FInstanceSceneData in shaders.
 * Note 2: Try to keep this 16 byte aligned. i.e |Matrix4x4|Vector3,float|Vector3,float|Vector4|  _NOT_  |Vector3,(waste padding)|Vector3,(waste padding)|Vector3. Or at least mark out padding if it can't be avoided.
 */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FInstanceUniformShaderParameters,ENGINE_API)
	SHADER_PARAMETER(FMatrix,  LocalToWorld)
	SHADER_PARAMETER(FMatrix,  PrevLocalToWorld)
	SHADER_PARAMETER(FMatrix,  WorldToLocal)
	SHADER_PARAMETER(FVector4, NonUniformScale)
	SHADER_PARAMETER(FVector4, InvNonUniformScaleAndDeterminantSign)
	SHADER_PARAMETER(FVector,  LocalBoundsMin)
	SHADER_PARAMETER(uint32,   PrimitiveId)
	SHADER_PARAMETER(FVector,  LocalBoundsMax)
	SHADER_PARAMETER(uint32,   Unused1)
	SHADER_PARAMETER(uint32,   NaniteRuntimeResourceID)
	SHADER_PARAMETER(int32,	   NaniteHierarchyOffset)
	SHADER_PARAMETER(uint32,   Unused2)
	SHADER_PARAMETER(uint32,   Unused3)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

/** Initializes the instance uniform shader parameters. */
inline FInstanceUniformShaderParameters GetInstanceUniformShaderParameters(
	const FMatrix&  LocalToWorld,
	const FMatrix&  PrevLocalToWorld,
	const FMatrix&  WorldToLocal,
	const FVector&  LocalBoundsMin,
	const FVector&  LocalBoundsMax,
	const FVector4& NonUniformScale,
	const FVector4& InvNonUniformScaleAndDeterminantSign,
	const FNaniteInfo& NaniteInfo,
	uint32 PrimitiveId
)
{
	FInstanceUniformShaderParameters Result;
	Result.LocalToWorld							= LocalToWorld;
	Result.PrevLocalToWorld						= PrevLocalToWorld;
	Result.WorldToLocal							= WorldToLocal;
	Result.NonUniformScale						= NonUniformScale;
	Result.InvNonUniformScaleAndDeterminantSign	= InvNonUniformScaleAndDeterminantSign;
	Result.PrimitiveId							= PrimitiveId;
	Result.LocalBoundsMin						= LocalBoundsMin;
	Result.LocalBoundsMax						= LocalBoundsMax;
	Result.NaniteRuntimeResourceID				= NaniteInfo.RuntimeResourceID;
	Result.NaniteHierarchyOffset				= NaniteInfo.HierarchyOffset;
	return Result;
}

inline TUniformBufferRef<FInstanceUniformShaderParameters> CreateInstanceUniformBufferImmediate(
	const FMatrix&  LocalToWorld,
	const FMatrix&  PrevLocalToWorld,
	const FMatrix&  WorldToLocal,
	const FVector&  LocalBoundsMin,
	const FVector&  LocalBoundsMax,
	const FVector4& NonUniformScale,
	const FVector4& InvNonUniformScaleAndDeterminantSign,
	const FNaniteInfo& NaniteInfo,
	uint32 PrimitiveId
)
{
	check(IsInRenderingThread());
	return TUniformBufferRef<FInstanceUniformShaderParameters>::CreateUniformBufferImmediate(
		GetInstanceUniformShaderParameters(
			LocalToWorld,
			PrevLocalToWorld,
			WorldToLocal,
			LocalBoundsMin,
			LocalBoundsMax,
			NonUniformScale,
			InvNonUniformScaleAndDeterminantSign,
			NaniteInfo,
			PrimitiveId
		),
		UniformBuffer_MultiFrame
	);
}

struct FInstanceSceneShaderData
{
	// Must match usf
	enum { InstanceDataStrideInFloat4s = 14 };

	FVector4 Data[InstanceDataStrideInFloat4s];

	FInstanceSceneShaderData()
	{
		Setup(GetInstanceUniformShaderParameters(
			FMatrix::Identity,
			FMatrix::Identity,
			FMatrix::Identity,
			FVector::ZeroVector,
			FVector::ZeroVector,
			FVector4(1.0f, 1.0f, 1.0f, 1.0f),
			FVector4(1.0f, 1.0f, 1.0f, 1.0f),
			FNaniteInfo(),
			0
		));
	}

	explicit FInstanceSceneShaderData(const FInstanceUniformShaderParameters& InstanceUniformShaderParameters)
	{
		Setup(InstanceUniformShaderParameters);
	}

	ENGINE_API FInstanceSceneShaderData(const FPrimitiveInstance& Instance);

	ENGINE_API void Setup(const FInstanceUniformShaderParameters& InstanceUniformShaderParameters);
};
