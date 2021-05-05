// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceUniformShaderParameters.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveSceneInfo.h"

FInstanceSceneShaderData::FInstanceSceneShaderData(const FPrimitiveInstance& Instance)
	: Data(InPlace, NoInit)
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

	Data[0].X  = *(const    float*)&InstanceUniformShaderParameters.Flags;
	Data[0].Y  = *(const    float*)&InstanceUniformShaderParameters.PrimitiveId;
	Data[0].Z  = *(const    float*)&InstanceUniformShaderParameters.NaniteRuntimeResourceID;
	Data[0].W  = *(const    float*)&InstanceUniformShaderParameters.LastUpdateSceneFrameNumber;

	InstanceUniformShaderParameters.LocalToWorld.To3x4MatrixTranspose((float*)&Data[1]);
	InstanceUniformShaderParameters.PrevLocalToWorld.To3x4MatrixTranspose((float*)&Data[4]);
	
	Data[7]    = *(const  FVector3f*)&InstanceUniformShaderParameters.LocalBoundsCenter;
	Data[7].W  = *(const    float*)&InstanceUniformShaderParameters.LocalBoundsExtent.X;
	
	Data[8].X  = *(const    float*)&InstanceUniformShaderParameters.LocalBoundsExtent.Y;
	Data[8].Y  = *(const    float*)&InstanceUniformShaderParameters.LocalBoundsExtent.Z;
	Data[8].Z  = *(const    float*)&InstanceUniformShaderParameters.NaniteHierarchyOffset;
	Data[8].W  = *(const    float*)&InstanceUniformShaderParameters.PerInstanceRandom;

	Data[9]    = *(const FVector4*)&InstanceUniformShaderParameters.LightMapAndShadowMapUVBias;
}
