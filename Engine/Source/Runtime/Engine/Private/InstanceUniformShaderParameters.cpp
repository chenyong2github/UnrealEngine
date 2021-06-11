// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceUniformShaderParameters.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveSceneInfo.h"

FInstanceSceneShaderData::FInstanceSceneShaderData(
	const FPrimitiveInstance& Instance,
	uint32 PrimitiveId,
	const FRenderTransform& PrimitiveLocalToWorld,
	const FRenderTransform& PrimitivePrevLocalToWorld,
	uint32 LastUpdateFrame,
	bool bHasPreviousTransform
)
: Data(InPlace, NoInit)
{
	Setup(Instance, PrimitiveId, PrimitiveLocalToWorld, PrimitivePrevLocalToWorld, LastUpdateFrame, bHasPreviousTransform);
}

void FInstanceSceneShaderData::Setup(
	const FPrimitiveInstance& Instance,
	uint32 PrimitiveId,
	const FRenderTransform& PrimitiveToWorld,
	const FRenderTransform& PrevPrimitiveToWorld,
	uint32 LastUpdateFrame,
	bool bHasPreviousTransform
)
{
	// Note: layout must match GetInstanceData in SceneData.ush

	// TODO: Could remove LightMapAndShadowMapUVBias if r.AllowStaticLighting=false.
	// This is a read-only setting that will cause all shaders to recompile if changed.

	FRenderTransform LocalToWorld		= Instance.LocalToPrimitive * PrimitiveToWorld;

	// TODO: Remove
	const FRenderTransform& PrevLocalToPrimitive = bHasPreviousTransform ? Instance.PrevLocalToPrimitive : Instance.LocalToPrimitive;
	FRenderTransform PrevLocalToWorld = PrevLocalToPrimitive * PrevPrimitiveToWorld;
	//FRenderTransform PrevLocalToWorld	= Instance.PrevLocalToPrimitive * PrevPrimitiveToWorld;

	// Remove shear
	LocalToWorld.Orthonormalize();
	PrevLocalToWorld.Orthonormalize();

	uint32 InstanceFlags = Instance.Flags;
	if (LocalToWorld.RotDeterminant() < 0.0f)
	{
		InstanceFlags |= INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN;
	}
	else
	{
		InstanceFlags &= ~INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN;
	}

	uint32 PayloadDataOffset = 0xFFFFFFFFu; // TODO: Implement payload data

	Data[0].X  = *(const     float*)&InstanceFlags;
	Data[0].Y  = *(const     float*)&PrimitiveId;
	Data[0].Z  = *(const     float*)&Instance.NaniteHierarchyOffset;
	Data[0].W  = *(const     float*)&LastUpdateFrame;

	LocalToWorld.To3x4MatrixTranspose((float*)&Data[1]);
	PrevLocalToWorld.To3x4MatrixTranspose((float*)&Data[4]);

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
		FRenderBounds(FVector3f::ZeroVector, FVector3f::ZeroVector),
		FVector4(ForceInitToZero),
		NANITE_INVALID_HIERARCHY_OFFSET,
		0u, /* Instance Flags */
		0.0f
	);

ENGINE_API const FPrimitiveInstance& GetDummyPrimitiveInstance()
{
	return DummyInstance;
}

ENGINE_API const FInstanceSceneShaderData& GetDummyInstanceSceneShaderData()
{
	static FInstanceSceneShaderData DummyShaderData = FInstanceSceneShaderData(
		GetDummyPrimitiveInstance(),
		0xFFFFFFFFu, /* Primitive Id */
		FRenderTransform::Identity, /* Primitive LocalToWorld */
		FRenderTransform::Identity,  /* Primitive PrevLocalToWorld */
		INVALID_LAST_UPDATE_FRAME,
		false /* Has Previous Transform */
	);
	return DummyShaderData;
}
