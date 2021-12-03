// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceUniformShaderParameters.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveSceneInfo.h"

FInstanceSceneShaderData::FInstanceSceneShaderData(
	const FPrimitiveInstance& Instance,
	uint32 PrimitiveId,
	const FRenderTransform& PrimitiveLocalToWorld,
	const FRenderTransform& PrimitivePrevLocalToWorld,
	const FRenderTransform& PrevLocalToPrimitive, // TODO: Temporary
	const FRenderBounds& LocalBounds, // TODO: Temporary
	const uint32 HierarchyOffset, // TODO: Temporary
	const FVector4f& LightMapShadowMapUVBias, // TODO: Temporary
	float RandomID, // TODO: Temporary
	float CustomDataFloat0, // TODO: Temporary Hack!
	uint32 LastUpdateFrame,
	uint32 InstanceFlags // TODO: Temporary
)
: Data(InPlace, NoInit)
{
	Setup
	(
		Instance,
		PrimitiveId,
		PrimitiveLocalToWorld,
		PrimitivePrevLocalToWorld,
		PrevLocalToPrimitive,
		LocalBounds,
		HierarchyOffset,
		LightMapShadowMapUVBias,
		RandomID,
		CustomDataFloat0,
		LastUpdateFrame,
		InstanceFlags
	);
}

FInstanceSceneShaderData::FInstanceSceneShaderData(
	const FPrimitiveInstance& Instance,
	uint32 PrimitiveId,
	const FMatrix& PrimitiveLocalToWorld,
	const FMatrix& PrimitivePrevLocalToWorld,
	const FRenderTransform& PrevLocalToPrimitive, // TODO: Temporary
	const FVector4& LightMapShadowMapUVBias, // TODO: Temporary
	float RandomID, // TODO: Temporary
	float CustomDataFloat0, // TODO: Temporary Hack!
	uint32 LastUpdateFrame
)
	: Data(InPlace, NoInit)
{
	const FLargeWorldRenderPosition WorldOrigin(PrimitiveLocalToWorld.GetOrigin());
	Setup(Instance,
		PrimitiveId,
		FRenderTransform(WorldOrigin.MakeToRelativeWorldMatrix(PrimitiveLocalToWorld)),
		FRenderTransform(WorldOrigin.MakeClampedToRelativeWorldMatrix(PrimitivePrevLocalToWorld)),
		PrevLocalToPrimitive,
		LightMapShadowMapUVBias,
		RandomID,
		CustomDataFloat0,
		LastUpdateFrame
	);
}

void FInstanceSceneShaderData::Setup(
	const FPrimitiveInstance& Instance,
	uint32 PrimitiveId,
	const FRenderTransform& PrimitiveToWorld,
	const FRenderTransform& PrevPrimitiveToWorld,
	const FRenderTransform& PrevLocalToPrimitive, // TODO: Temporary
	const FRenderBounds& LocalBounds, // TODO: Temporary
	const uint32 HierarchyOffset, // TODO: Temporary
	const FVector4f& LightMapShadowMapUVBias, // TODO: Temporary
	float RandomID, // TODO: Temporary
	float CustomDataFloat0, // TODO: Temporary Hack!
	uint32 LastUpdateFrame,
	uint32 RawInstanceFlags // TODO: Temporary
)
{
	// Note: layout must match GetInstanceData in SceneData.ush

	// TODO: Could remove LightMapAndShadowMapUVBias if r.AllowStaticLighting=false.
	// This is a read-only setting that will cause all shaders to recompile if changed.

	FRenderTransform LocalToWorld = Instance.LocalToPrimitive * PrimitiveToWorld;
	FRenderTransform PrevLocalToWorld;
	if (RawInstanceFlags & INSTANCE_SCENE_DATA_FLAG_HAS_DYNAMIC_DATA)
	{
		PrevLocalToWorld = PrevLocalToPrimitive * PrevPrimitiveToWorld;
	}
	else
	{
		PrevLocalToWorld = Instance.LocalToPrimitive * PrevPrimitiveToWorld;
	}

	// Remove shear
	LocalToWorld.Orthogonalize();
	PrevLocalToWorld.Orthogonalize();

	FCompressedTransform CompressedLocalToWorld( LocalToWorld );
	FCompressedTransform CompressedPrevLocalToWorld( PrevLocalToWorld );

	uint32 InstanceFlags = RawInstanceFlags;
	if (LocalToWorld.RotDeterminant() < 0.0f)
	{
		InstanceFlags |= INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN;
	}
	else
	{
		InstanceFlags &= ~INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN;
	}

	// TODO: Temporary Hack!
	float PayloadDataOffset = CustomDataFloat0;
	//uint32 PayloadDataOffset = 0xFFFFFFFFu; // TODO: Implement payload data
	const uint32 InstanceId = 123; // TODO
	const uint32 Packed0 = (InstanceFlags   << 20u) | PrimitiveId;
	const uint32 Packed1 = (/* TODO: CustomDataCount*/ 0u << 24u) | InstanceId;

	check((PrimitiveId		& 0x000FFFFF) == PrimitiveId);
	check((InstanceFlags	& 0x00000FFF) == InstanceFlags);
	check((InstanceId		& 0x00FFFFFF) == InstanceId);

	Data[0].X  = *(const float*)&Packed0;
	Data[0].Y  = *(const float*)&Packed1;
	Data[0].Z  = *(const float*)&HierarchyOffset;
	Data[0].W  = *(const float*)&LastUpdateFrame;

#if !INSTANCE_COMPRESSED_TRANSFORM
	LocalToWorld.To3x4MatrixTranspose((float*)&Data[1]);
	PrevLocalToWorld.To3x4MatrixTranspose((float*)&Data[4]);

	const FVector3f BoundsOrigin = Instance.LocalBounds.GetCenter();
	const FVector3f BoundsExtent = Instance.LocalBounds.GetExtent();
	
	Data[7]    = *(const FVector3f*)&BoundsOrigin;
	Data[7].W  = *(const     float*)&BoundsExtent.X;
	
	Data[8].X  = *(const     float*)&BoundsExtent.Y;
	Data[8].Y  = *(const     float*)&BoundsExtent.Z;
	Data[8].Z  = *(const     float*)&PayloadDataOffset;
	Data[8].W  = *(const     float*)&RandomID;

	Data[9]    = *(const  FVector4f*)&LightMapShadowMapUVBias;
#else
	Data[1]    = *(const FVector4f* )&CompressedLocalToWorld.Rotation[0];
	Data[2]    = *(const FVector3f*)&CompressedLocalToWorld.Translation;

	Data[3]    = *(const FVector4f* )&CompressedPrevLocalToWorld.Rotation[0];
	Data[4]    = *(const FVector3f*)&CompressedPrevLocalToWorld.Translation;

	const FVector3f BoundsOrigin = LocalBounds.GetCenter();
	const FVector3f BoundsExtent = LocalBounds.GetExtent();
	
	Data[5]    = *(const FVector3f*)&BoundsOrigin;
	Data[5].W  = *(const     float*)&BoundsExtent.X;
	
	Data[6].X  = *(const     float*)&BoundsExtent.Y;
	Data[6].Y  = *(const     float*)&BoundsExtent.Z;
	Data[6].Z  = *(const     float*)&PayloadDataOffset;
	Data[6].W  = *(const     float*)&RandomID;

	Data[7]    = *(const  FVector4f*)&LightMapShadowMapUVBias;
#endif
}

ENGINE_API const FInstanceSceneShaderData& GetDummyInstanceSceneShaderData()
{
	static FInstanceSceneShaderData DummyShaderData = FInstanceSceneShaderData(
		ConstructPrimitiveInstance(),
		INVALID_PRIMITIVE_ID,
		FRenderTransform::Identity, /* Primitive LocalToWorld */
		FRenderTransform::Identity,  /* Primitive PrevLocalToWorld */
		FRenderTransform::Identity,  /* PrevLocalToPrimitive */
		FRenderBounds(FVector3f::ZeroVector, FVector3f::ZeroVector), /* Instance Bounds */ // TODO: Temporary
		NANITE_INVALID_HIERARCHY_OFFSET, /* Nanite Hierarchy Offset */ // TODO: Temporary
		FVector4f(ForceInitToZero), /* Lightmap and Shadowmap UV Bias */ // TODO: Temporary
		0.0f, /* Per instance Random */ // TODO: Temporary
		0.0f, /* Custom Data Float0 */ // TODO: Temporary Hack!
		INVALID_LAST_UPDATE_FRAME,
		0 /* InstanceFlags */ // TODO: Temporary
	);
	return DummyShaderData;
}
