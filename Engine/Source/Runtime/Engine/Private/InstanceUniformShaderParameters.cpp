// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceUniformShaderParameters.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveSceneInfo.h"

FInstanceSceneShaderData::FInstanceSceneShaderData(const FPrimitiveInstance& Instance, uint32 PrimitiveId)
	: Data(InPlace, NoInit)
{
	Setup(Instance, PrimitiveId);
}

void FInstanceSceneShaderData::Setup(const FPrimitiveInstance& Instance, uint32 PrimitiveId)
{
	// Note: layout must match GetInstanceData in SceneData.ush

	// TODO: Could remove LightMapAndShadowMapUVBias if r.AllowStaticLighting=false.
	// This is a read-only setting that will cause all shaders to recompile if changed.

	uint32 PayloadDataOffset = 0xFFFFFFFFu; // TODO: Implement payload data

	Data[0].X  = *(const     float*)&Instance.Flags;
	Data[0].Y  = *(const     float*)&PrimitiveId;
	Data[0].Z  = *(const     float*)&Instance.NaniteHierarchyOffset;
	Data[0].W  = *(const     float*)&Instance.LastUpdateSceneFrameNumber;

	Instance.LocalToWorld.To3x4MatrixTranspose((float*)&Data[1]);
	Instance.PrevLocalToWorld.To3x4MatrixTranspose((float*)&Data[4]);

	const FVector3f BoundsOrigin = Instance.LocalBounds.GetCenter();
	const FVector3f BoundsExtent = Instance.LocalBounds.GetExtent();
	
	Data[7]    = *(const FVector3f*)&BoundsOrigin;
	Data[7].W  = *(const     float*)&BoundsExtent.X;
	
	Data[8].X  = *(const     float*)&BoundsExtent.Y;
	Data[8].Y  = *(const     float*)&BoundsExtent.Z;
	Data[8].Z  = *(const     float*)&PayloadDataOffset;
	Data[8].W  = *(const     float*)&Instance.PerInstanceRandom;

	Data[9]    = *(const  FVector4*)&Instance.LightMapAndShadowMapUVBias;
}

static FPrimitiveInstance DummyInstance =
	ConstructPrimitiveInstance(
		FRenderTransform::Identity,
		FRenderTransform::Identity,
		FRenderBounds(FVector3f::ZeroVector, FVector3f::ZeroVector),
		FVector4(1.0f, 1.0f, 1.0f, 1.0f),
		FVector3f(1.0f, 1.0f, 1.0f),
		FVector4(ForceInitToZero),
		NANITE_INVALID_HIERARCHY_OFFSET,
		0u, /* Instance Flags */
		0, /* Last Update Frame */
		0.0f
	);

ENGINE_API const FPrimitiveInstance& GetDummyPrimitiveInstance()
{
	return DummyInstance;
}

ENGINE_API const FInstanceSceneShaderData& GetDummyInstanceSceneShaderData()
{
	static FInstanceSceneShaderData DummyShaderData = FInstanceSceneShaderData(GetDummyPrimitiveInstance(), 0xFFFFFFFFu /* Primitive Id */);
	return DummyShaderData;
}
