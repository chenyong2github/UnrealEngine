// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceUniformShaderParameters.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveSceneInfo.h"

FInstanceSceneShaderData::FInstanceSceneShaderData(const FPrimitiveInstance& Instance)
{
	Setup(GetInstanceUniformShaderParameters(
		Instance.LocalToWorld,
		Instance.PrevLocalToWorld,
		Instance.LocalBounds.Origin,
		Instance.LocalBounds.BoxExtent,
		Instance.NonUniformScale,
		Instance.InvNonUniformScaleAndDeterminantSign,
		Instance.LightMapAndShadowMapUVBias,
		Instance.NaniteInfo,
		Instance.PrimitiveId,
		Instance.LastUpdateSceneFrameNumber,
		Instance.PerInstanceRandom,
		(Instance.Flags & INSTANCE_SCENE_DATA_FLAG_CAST_SHADOWS)
	));
}

void FInstanceSceneShaderData::Setup(const FInstanceUniformShaderParameters& InstanceUniformShaderParameters)
{
	// Note: layout must match GetInstanceData in SceneData.ush

	// TODO: Could remove LightMapAndShadowMapUVBias if r.AllowStaticLighting=false.
	// This is a read-only setting that will cause all shaders to recompile if changed.

	InstanceUniformShaderParameters.LocalToWorld.To3x4MatrixTranspose((float*)&Data[0]);
	InstanceUniformShaderParameters.PrevLocalToWorld.To3x4MatrixTranspose((float*)&Data[3]);
	
	Data[6]    = *(const FVector *)&InstanceUniformShaderParameters.LocalBoundsCenter;
	Data[6].W  = *(const    float*)&InstanceUniformShaderParameters.PrimitiveId;
	Data[7]    = *(const FVector *)&InstanceUniformShaderParameters.LocalBoundsExtent;
	Data[7].W  = *(const    float*)&InstanceUniformShaderParameters.LastUpdateSceneFrameNumber;
	Data[8].X  = *(const    float*)&InstanceUniformShaderParameters.NaniteRuntimeResourceID;
	Data[8].Y  = *(const    float*)&InstanceUniformShaderParameters.NaniteHierarchyOffset;
	Data[8].Z  = *(const    float*)&InstanceUniformShaderParameters.PerInstanceRandom;
	Data[8].W  = *(const    float*)&InstanceUniformShaderParameters.Flags;
	Data[9]    = *(const FVector4*)&InstanceUniformShaderParameters.LightMapAndShadowMapUVBias;
}
