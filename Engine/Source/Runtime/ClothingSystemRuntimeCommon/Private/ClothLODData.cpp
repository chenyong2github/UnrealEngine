// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothLODData.h"
#include "ClothPhysicalMeshData.h"
#include "ClothingAssetCustomVersion.h"

#if WITH_EDITORONLY_DATA
void FClothLODDataCommon::GetParameterMasksForTarget(
	const uint8 InTarget, 
	TArray<FPointWeightMap*>& OutMasks)
{
	for(FPointWeightMap& Mask : PointWeightMaps)
	{
		if(Mask.CurrentTarget == InTarget)
		{
			OutMasks.Add(&Mask);
		}
	}
}
#endif // WITH_EDITORONLY_DATA

bool FClothLODDataCommon::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FClothingAssetCustomVersion::GUID);

	// Serialize normal tagged property data
	if (!Ar.IsCountingMemory())
	{
		UScriptStruct* const Struct = FClothLODDataCommon::StaticStruct();
		Struct->SerializeTaggedProperties(Ar, (uint8*)this, Struct, nullptr);
	}

	// Serialize the mesh to mesh data (not a USTRUCT)
	Ar << TransitionUpSkinData
	   << TransitionDownSkinData;

	const int32 ClothingCustomVersion = Ar.CustomVer(FClothingAssetCustomVersion::GUID);
	if (ClothingCustomVersion < FClothingAssetCustomVersion::MovePropertiesToCommonBaseClasses)
	{
		// Migrate maps
		PhysicalMeshData.GetWeightMap(EWeightMapTargetCommon::MaxDistance).Values = MoveTemp(PhysicalMeshData.MaxDistances_DEPRECATED);
		PhysicalMeshData.GetWeightMap(EWeightMapTargetCommon::BackstopDistance).Values = MoveTemp(PhysicalMeshData.BackstopDistances_DEPRECATED);
		PhysicalMeshData.GetWeightMap(EWeightMapTargetCommon::BackstopRadius).Values = MoveTemp(PhysicalMeshData.BackstopRadiuses_DEPRECATED);
		PhysicalMeshData.GetWeightMap(EWeightMapTargetCommon::AnimDriveMultiplier).Values = MoveTemp(PhysicalMeshData.AnimDriveMultipliers_DEPRECATED);

#if WITH_EDITORONLY_DATA
		// Migrate editor maps
		PointWeightMaps.SetNum(ParameterMasks_DEPRECATED.Num());
		for (int32 i = 0; i < PointWeightMaps.Num(); ++i)
		{
			ParameterMasks_DEPRECATED[i].MigrateTo(PointWeightMaps[i]);
		}
		ParameterMasks_DEPRECATED.Empty();
#endif // WITH_EDITORONLY_DATA

#if WITH_CHAOS
		// Rebuild surface points so that the legacy Apex convex collision data can also be used with Chaos
		for (auto& Convex : CollisionData.Convexes)
		{
			if (!Convex.SurfacePoints.Num())
			{
				Convex.RebuildSurfacePoints();
			}
		}
#endif // #if WITH_CHAOS
	}
	return true;
}

#if WITH_EDITOR
void FClothLODDataCommon::PushWeightsToMesh()
{
	PhysicalMeshData.ClearWeightMaps();
	for (const FPointWeightMap& Weights : PointWeightMaps)
	{
		if (Weights.bEnabled)
		{
			FPointWeightMap& TargetWeightMap = PhysicalMeshData.FindOrAddWeightMap(Weights.CurrentTarget);
			TargetWeightMap.Values = Weights.Values;
		}
	}
}
#endif
