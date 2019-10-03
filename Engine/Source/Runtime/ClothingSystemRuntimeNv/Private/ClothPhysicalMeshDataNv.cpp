// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ClothPhysicalMeshDataNv.h"
#include "Assets/ClothingAssetNv.h" // For UClothConfigNv

UClothPhysicalMeshDataNv::UClothPhysicalMeshDataNv()
{
	Super::RegisterFloatArray((uint32)MaskTarget_PhysMesh::MaxDistance, &MaxDistances);
	Super::RegisterFloatArray((uint32)MaskTarget_PhysMesh::BackstopDistance, &BackstopDistances);
	Super::RegisterFloatArray((uint32)MaskTarget_PhysMesh::BackstopRadius, &BackstopRadiuses);
	Super::RegisterFloatArray((uint32)MaskTarget_PhysMesh::AnimDriveMultiplier, &AnimDriveMultipliers);
}

UClothPhysicalMeshDataNv::~UClothPhysicalMeshDataNv()
{}

void UClothPhysicalMeshDataNv::Reset(const int32 NumPoints)
{
	Super::Reset(NumPoints);

	MaxDistances.Reset();
	BackstopDistances.Reset();
	BackstopRadiuses.Reset();
	AnimDriveMultipliers.Reset();

	MaxDistances.AddDefaulted(NumPoints);
	BackstopDistances.AddDefaulted(NumPoints);
	BackstopRadiuses.AddDefaulted(NumPoints);
	AnimDriveMultipliers.AddDefaulted(NumPoints);
}

void UClothPhysicalMeshDataNv::ClearParticleParameters()
{
	Super::ClearParticleParameters();

	// Max distances must be present, so fill to zero on clear so we still have valid mesh data.
	const int32 NumVerts = Vertices.Num();
	MaxDistances.Reset(NumVerts);
	MaxDistances.AddZeroed(NumVerts);

	// Just clear optional properties
	BackstopDistances.Empty();
	BackstopRadiuses.Empty();
	AnimDriveMultipliers.Empty();
}

void UClothPhysicalMeshDataNv::BuildSelfCollisionData(const UClothConfigBase* ClothConfigBase)
{
	const UClothConfigNv* ClothConfig = Cast<UClothConfigNv>(ClothConfigBase);
	const float SCRadius = ClothConfig ? ClothConfig->SelfCollisionRadius * ClothConfig->SelfCollisionCullScale : 0.1f;
	const float SCRadiusSq = SCRadius * SCRadius;

	// Start with the full set
	const int32 NumVerts = Vertices.Num();
	SelfCollisionIndices.Reset();
	for(int32 Index = 0; Index < NumVerts; ++Index)
	{
		if(!IsFixed(Index))
			SelfCollisionIndices.Add(Index);
	}

	// Now start aggresively culling verts that are near others that we have accepted
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
}
