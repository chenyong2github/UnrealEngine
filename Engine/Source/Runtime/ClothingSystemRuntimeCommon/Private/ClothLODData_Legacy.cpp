// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ClothLODData_Legacy.h"
#include "PointWeightMap.h"
#include "ClothLODData.h"

FClothParameterMask_Legacy::FClothParameterMask_Legacy()
	: MaskName(NAME_None)
	, CurrentTarget(EWeightMapTargetCommon::None)
	, MaxValue_DEPRECATED(0.0)
	, MinValue_DEPRECATED(100.0)
	, bEnabled(false)
{}

void FClothParameterMask_Legacy::MigrateTo(FPointWeightMap* Weights) const
{
	Weights->Values = Values;
#if WITH_EDITORONLY_DATA
	Weights->Name = MaskName;
	Weights->CurrentTarget = static_cast<uint8>(CurrentTarget);
	Weights->bEnabled = bEnabled;
#endif
}

bool FClothLODData_Legacy::Serialize(FArchive& Ar)
{
	// Serialize normal tagged data
	if (!Ar.IsCountingMemory())
	{
		UScriptStruct* Struct = FClothLODData_Legacy::StaticStruct();
		Struct->SerializeTaggedProperties(Ar, (uint8*)this, Struct, nullptr);
	}
	// Serialize the mesh to mesh data (not a USTRUCT)
	Ar	<< TransitionUpSkinData
		<< TransitionDownSkinData;
	return true;
}

void FClothLODData_Legacy::MigrateTo(UClothLODDataCommon* LodData) const
{
	PhysicalMeshData.MigrateTo(LodData->ClothPhysicalMeshData);
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
