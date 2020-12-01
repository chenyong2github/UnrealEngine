// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceUniformShaderParameters.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveSceneInfo.h"

FInstanceSceneShaderData::FInstanceSceneShaderData(const FPrimitiveInstance& Instance)
{
	Setup(GetInstanceUniformShaderParameters(
		Instance.LocalToWorld,
		Instance.PrevLocalToWorld,
		Instance.WorldToLocal,
		Instance.LocalBounds.Origin,
		Instance.LocalBounds.BoxExtent,
		Instance.NonUniformScale,
		Instance.InvNonUniformScaleAndDeterminantSign,
		Instance.NaniteInfo,
		Instance.PrimitiveId,
		Instance.LastUpdateSceneFrameNumber
	));
}

void FInstanceSceneShaderData::Setup(const FInstanceUniformShaderParameters& InstanceUniformShaderParameters)
{
	// Note: layout must match GetInstanceData in usf

	InstanceUniformShaderParameters.LocalToWorld.To3x4MatrixTranspose((float*)&Data[0]);
	InstanceUniformShaderParameters.PrevLocalToWorld.To3x4MatrixTranspose((float*)&Data[3]);
	InstanceUniformShaderParameters.WorldToLocal.To3x4MatrixTranspose((float*)&Data[6]);
	Data[9]    = *(const FVector4*)&InstanceUniformShaderParameters.NonUniformScale;
	Data[10]   = *(const FVector4*)&InstanceUniformShaderParameters.InvNonUniformScaleAndDeterminantSign;
	Data[11]   = *(const FVector *)&InstanceUniformShaderParameters.LocalBoundsCenter;
	Data[11].W = *(const    float*)&InstanceUniformShaderParameters.PrimitiveId;
	Data[12]   = *(const FVector *)&InstanceUniformShaderParameters.LocalBoundsExtent;
	Data[12].W = *(const    float*)&InstanceUniformShaderParameters.LastUpdateSceneFrameNumber;
	Data[13].X = *(const    float*)&InstanceUniformShaderParameters.NaniteRuntimeResourceID;
	Data[13].Y = *(const    float*)&InstanceUniformShaderParameters.NaniteHierarchyOffset_AndHasImposter;
	Data[13].Z = 0.0f;
	Data[13].W = 0.0f;
}
