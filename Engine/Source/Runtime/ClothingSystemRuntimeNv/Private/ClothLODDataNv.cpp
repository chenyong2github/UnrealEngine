// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ClothLODDataNv.h"

UClothLODDataNv::UClothLODDataNv(const FObjectInitializer& Init)
	: Super(Init)
{
	PhysicalMeshData = Init.CreateDefaultSubobject<UClothPhysicalMeshDataNv>(this, FName("ClothPhysicalMeshDataNv"));
}

UClothLODDataNv::~UClothLODDataNv()
{}

void FClothLODData::MigrateTo(UClothLODDataNv* LodData) const
{
	PhysicalMeshData.MigrateTo(LodData->PhysicalMeshData);
	LodData->CollisionData = CollisionData;
#if WITH_CHAOS
	// Rebuild surface points so that the legacy Apex convex collision data can also be used with Chaos
	for (auto& Convex : LodData->CollisionData.Convexes)
	{
		if (!Convex.SurfacePoints.Num())
		{
			Convex.RebuildSurfacePoints();
		}
	}
#endif // #if WITH_CHAOS
#if WITH_EDITORONLY_DATA
	LodData->ParameterMasks.SetNum(ParameterMasks.Num());
	for (int i = 0; i < ParameterMasks.Num(); i++)
	{
		ParameterMasks[i].MigrateTo(&LodData->ParameterMasks[i]);
	}
#endif // WITH_EDITORONLY_DATA
	LodData->TransitionUpSkinData = TransitionUpSkinData;
	LodData->TransitionDownSkinData = TransitionDownSkinData;
}
