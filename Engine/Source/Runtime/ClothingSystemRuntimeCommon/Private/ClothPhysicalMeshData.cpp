// Copyright Epic Games, Inc. All Rights Reserved.
#include "ClothPhysicalMeshData.h"
#include "ClothConfigBase.h"
#include "ClothPhysicalMeshDataBase_Legacy.h"

FClothPhysicalMeshData::FClothPhysicalMeshData()
	: MaxBoneWeights(0)
	, NumFixedVerts(0)
{
	ClearWeightMaps();
}

void FClothPhysicalMeshData::MigrateTo(FClothPhysicalMeshData& ClothPhysicalMeshData) const
{
	// Migrate properties
	ClothPhysicalMeshData.Vertices = Vertices;
	ClothPhysicalMeshData.Normals = Normals;
#if WITH_EDITORONLY_DATA
	ClothPhysicalMeshData.VertexColors = VertexColors;
#endif
	ClothPhysicalMeshData.Indices = Indices;
	ClothPhysicalMeshData.InverseMasses = InverseMasses;
	ClothPhysicalMeshData.BoneData = BoneData;
	ClothPhysicalMeshData.NumFixedVerts = NumFixedVerts;
	ClothPhysicalMeshData.MaxBoneWeights = MaxBoneWeights;
	ClothPhysicalMeshData.SelfCollisionIndices = SelfCollisionIndices;

	// Migrate maps
	if (MaxDistances_DEPRECATED.Num())
	{
		ClothPhysicalMeshData.GetWeightMap(EWeightMapTargetCommon::MaxDistance).Values = MaxDistances_DEPRECATED;
	}
	if (BackstopDistances_DEPRECATED.Num())
	{
		ClothPhysicalMeshData.GetWeightMap(EWeightMapTargetCommon::BackstopDistance).Values = BackstopDistances_DEPRECATED;
	}
	if (BackstopRadiuses_DEPRECATED.Num())
	{
		ClothPhysicalMeshData.GetWeightMap(EWeightMapTargetCommon::BackstopRadius).Values = BackstopRadiuses_DEPRECATED;
	}
	if (AnimDriveMultipliers_DEPRECATED.Num())
	{
		ClothPhysicalMeshData.GetWeightMap(EWeightMapTargetCommon::AnimDriveMultiplier).Values = AnimDriveMultipliers_DEPRECATED;
	}
}

void FClothPhysicalMeshData::MigrateFrom(const UClothPhysicalMeshDataBase_Legacy* ClothPhysicalMeshDataBase)
{
	Vertices = ClothPhysicalMeshDataBase->Vertices;
	Normals = ClothPhysicalMeshDataBase->Normals;
#if WITH_EDITORONLY_DATA
	VertexColors = ClothPhysicalMeshDataBase->VertexColors;
#endif
	Indices = ClothPhysicalMeshDataBase->Indices;
	InverseMasses = ClothPhysicalMeshDataBase->InverseMasses;
	BoneData = ClothPhysicalMeshDataBase->BoneData;
	NumFixedVerts = ClothPhysicalMeshDataBase->NumFixedVerts;
	MaxBoneWeights = ClothPhysicalMeshDataBase->MaxBoneWeights;
	SelfCollisionIndices = ClothPhysicalMeshDataBase->SelfCollisionIndices;

	const TArray<uint32> FloatArrayIds = ClothPhysicalMeshDataBase->GetFloatArrayIds();
	for (uint32 FloatArrayId : FloatArrayIds)
	{
		if (const TArray<float>* const FloatArray = ClothPhysicalMeshDataBase->GetFloatArray(FloatArrayId))
		{
			FindOrAddWeightMap(FloatArrayId).Values = *FloatArray;
		}
	}
}

void FClothPhysicalMeshData::Reset(const int32 InNumVerts, const int32 InNumIndices)
{
	Vertices.Init(FVector::ZeroVector, InNumVerts);
	Normals.Init(FVector::ZeroVector, InNumVerts);
#if WITH_EDITORONLY_DATA
	VertexColors.Init(FColor::Black, InNumVerts);
#endif //#if WITH_EDITORONLY_DATA
	InverseMasses.Init(0.f, InNumVerts);
	BoneData.Reset(InNumVerts);
	BoneData.AddDefaulted(InNumVerts);
	Indices.Init(0, InNumIndices);

	NumFixedVerts = 0;
	MaxBoneWeights = 0;

	ClearWeightMaps();
}

void FClothPhysicalMeshData::ClearWeightMaps()
{
	// Clear all weight maps (and reserve 4 slots)
	WeightMaps.Empty(4);

	// Max distances must be present, so fill to zero on clear so we still have valid mesh data.
	AddWeightMap(EWeightMapTargetCommon::MaxDistance).Initialize(Vertices.Num());

	// Add default (empty) optional maps, as these are always expected to be found
	AddWeightMap(EWeightMapTargetCommon::BackstopDistance);
	AddWeightMap(EWeightMapTargetCommon::BackstopRadius);
	AddWeightMap(EWeightMapTargetCommon::AnimDriveMultiplier);
}

void FClothPhysicalMeshData::BuildSelfCollisionData(const TMap<FName, UClothConfigBase*>& ClothConfigs)
{
#if WITH_APEX_CLOTHING  // Only apex clothing needs to build the SelfCollisionIndices
	float SCRadius = 0.f;
	for (const TPair<FName, UClothConfigBase*>& ClothConfig : ClothConfigs)
	{
		SCRadius = ClothConfig.Value->NeedsSelfCollisionIndices();
		if (SCRadius > 0.f)
		{
			break; 
		}
	}

	if (SCRadius == 0.f) { return; }

	const float SCRadiusSq = SCRadius * SCRadius;

	// Start with the full set
	const int32 NumVerts = Vertices.Num();
	SelfCollisionIndices.Reset();
	const FPointWeightMap& MaxDistances = GetWeightMap(EWeightMapTargetCommon::MaxDistance);
	for (int32 Index = 0; Index < NumVerts; ++Index)
	{
		if (!MaxDistances.IsBelowThreshold(Index))
		{
			SelfCollisionIndices.Add(Index);
		}
	}

	// Now start aggressively culling verts that are near others that we have accepted
	for (int32 Vert0Itr = 0; Vert0Itr < SelfCollisionIndices.Num(); ++Vert0Itr)
	{
		const uint32 V0Index = SelfCollisionIndices[Vert0Itr];
		if (V0Index == INDEX_NONE)
		{
			// We'll remove these indices later.  Just skip it for now.
			continue;
		}

		const FVector& V0Pos = Vertices[V0Index];

		// Start one after our current V0, we've done the other checks
		for (int32 Vert1Itr = Vert0Itr + 1; Vert1Itr < SelfCollisionIndices.Num(); ++Vert1Itr)
		{
			const uint32 V1Index = SelfCollisionIndices[Vert1Itr];
			if (V1Index == INDEX_NONE)
			{
				// We'll remove these indices later.  Just skip it for now.
				continue;
			}

			const FVector& V1Pos = Vertices[V1Index];
			const float V0ToV1DistSq = (V1Pos - V0Pos).SizeSquared();
			if (V0ToV1DistSq < SCRadiusSq)
			{
				// Points are in contact in the rest state.  Remove it.
				//
				// It's worth noting that this biases towards removing indices 
				// of later in the list, and keeping ones earlier.  That's not
				// a great criteria for choosing which one is more important.
				SelfCollisionIndices[Vert1Itr] = INDEX_NONE;
				continue;
			}
		}
	}

	// Cull flagged indices.
	for (int32 It = SelfCollisionIndices.Num(); It--;)
	{
		if (SelfCollisionIndices[It] == INDEX_NONE)
		{
			SelfCollisionIndices.RemoveAt(It);
		}
	}
#endif  // WITH_APEX_CLOTHING
}
