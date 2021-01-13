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
		Instance.PerInstanceRandom
	));
}

void FInstanceSceneShaderData::Setup(const FInstanceUniformShaderParameters& InstanceUniformShaderParameters)
{
	// Note: layout must match GetInstanceData in usf

	// TODO: Could shrink instance size further if r.AllowStaticLighting=false. This is a read-only cvar
	// that will cause all shaders to recompile if changed.

	InstanceUniformShaderParameters.LocalToWorld.To3x4MatrixTranspose((float*)&Data[0]);
	InstanceUniformShaderParameters.PrevLocalToWorld.To3x4MatrixTranspose((float*)&Data[3]);
	Data[6]    = *(const FVector4*)&InstanceUniformShaderParameters.NonUniformScale;
	Data[7]    = *(const FVector4*)&InstanceUniformShaderParameters.InvNonUniformScaleAndDeterminantSign;
	Data[8]    = *(const FVector *)&InstanceUniformShaderParameters.LocalBoundsCenter;
	Data[8].W  = *(const    float*)&InstanceUniformShaderParameters.PrimitiveId;
	Data[9]    = *(const FVector *)&InstanceUniformShaderParameters.LocalBoundsExtent;
	Data[9].W  = *(const    float*)&InstanceUniformShaderParameters.LastUpdateSceneFrameNumber;
	Data[10].X = *(const    float*)&InstanceUniformShaderParameters.NaniteRuntimeResourceID;
	Data[10].Y = *(const    float*)&InstanceUniformShaderParameters.NaniteHierarchyOffset_AndHasImposter;
	Data[10].Z = *(const    float*)&InstanceUniformShaderParameters.PerInstanceRandom;
	Data[10].W = 0.0f; // Unused
	Data[11]   = *(const FVector4*)&InstanceUniformShaderParameters.LightMapAndShadowMapUVBias;
}
