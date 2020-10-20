// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothingSimulationMesh.h"
#include "ChaosCloth/ChaosClothingSimulationSolver.h"
#include "ChaosCloth/ChaosWeightMapTarget.h"
#include "ChaosCloth/ChaosClothPrivate.h"
#include "ClothingSimulation.h"
#include "ClothingAsset.h"
#include "Containers/ArrayView.h"
#include "Components/SkeletalMeshComponent.h"
#include "Async/ParallelFor.h"

DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Skin Physics Mesh"), STAT_ChaosClothSkinPhysicsMesh, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Wrap Deform Mesh"), STAT_ChaosClothWrapDeformMesh, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Wrap Deform Cloth LOD"), STAT_ChaosClothWrapDeformClothLOD, STATGROUP_ChaosCloth);

using namespace Chaos;

FClothingSimulationMesh::FClothingSimulationMesh(const UClothingAssetCommon* InAsset, const USkeletalMeshComponent* InSkeletalMeshComponent)
	: Asset(InAsset)
	, SkeletalMeshComponent(InSkeletalMeshComponent)
{
}

FClothingSimulationMesh::~FClothingSimulationMesh()
{
}

int32 FClothingSimulationMesh::GetNumLODs() const
{
	return Asset ? Asset->LodData.Num() : 0;
}

int32 FClothingSimulationMesh::GetLODIndex() const
{
	int32 LODIndex = INDEX_NONE;

	if (Asset && SkeletalMeshComponent)
	{
		if (const FClothingSimulationContextCommon* const Context = 
			static_cast<const FClothingSimulationContextCommon*>(SkeletalMeshComponent->GetClothingSimulationContext()))
		{
			const int32 PredictedLODIndex = Context->PredictedLod;

			// If PredictedLODIndex doesn't map to a valid LOD, we try higher LOD levels for a valid LOD.
			// Asset might only have lod on LOD 1 and not 0, however if mesh doesn't force LOD to 1, 
			// asset will not be assigned valid LOD index and will not generate sim data, breaking things.
			for (int32 Index = PredictedLODIndex; Index < Asset->LodMap.Num(); ++Index)
			{
				const int32 MappedLODIndex = Asset->LodMap[Index];
				if (Asset->LodData.IsValidIndex(MappedLODIndex))
				{
					LODIndex = MappedLODIndex;
					break;
				}
			}
		}
	}
	return LODIndex;
}

int32 FClothingSimulationMesh::GetNumPoints(int32 LODIndex) const
{
	return (Asset && Asset->LodData.IsValidIndex(LODIndex)) ?
		Asset->LodData[LODIndex].PhysicalMeshData.Vertices.Num() :
		0;
}

TConstArrayView<uint32> FClothingSimulationMesh::GetIndices(int32 LODIndex) const
{
	return (Asset && Asset->LodData.IsValidIndex(LODIndex)) ?
		TConstArrayView<uint32>(Asset->LodData[LODIndex].PhysicalMeshData.Indices) :
		TConstArrayView<uint32>();
}

TArray<TConstArrayView<float>> FClothingSimulationMesh::GetWeightMaps(int32 LODIndex) const
{
	TArray<TConstArrayView<float>> WeightMaps;
	if (Asset && Asset->LodData.IsValidIndex(LODIndex))
	{
		const FClothLODDataCommon& ClothLODData = Asset->LodData[LODIndex];
		const FClothPhysicalMeshData& ClothPhysicalMeshData = ClothLODData.PhysicalMeshData;

		const UEnum* const ChaosWeightMapTargetEnum = StaticEnum<EChaosWeightMapTarget>();
		const int32 NumWeightMaps = (int32)ChaosWeightMapTargetEnum->GetMaxEnumValue() + 1;

		WeightMaps.SetNum(NumWeightMaps);

		for (int32 EnumIndex = 0; EnumIndex < ChaosWeightMapTargetEnum->NumEnums(); ++EnumIndex)
		{
			const int32 TargetIndex = (int32)ChaosWeightMapTargetEnum->GetValueByIndex(EnumIndex);
			if (const FPointWeightMap* const WeightMap = ClothPhysicalMeshData.FindWeightMap(TargetIndex))
			{
				WeightMaps[TargetIndex] = WeightMap->Values;
			}
		}
	}
	return WeightMaps;
}

int32 FClothingSimulationMesh::GetReferenceBoneIndex() const
{
	return Asset ? Asset->ReferenceBoneIndex : INDEX_NONE;
}

TRigidTransform<float, 3> Chaos::FClothingSimulationMesh::GetReferenceBoneTransform() const
{
	if (SkeletalMeshComponent)
	{
		if (const FClothingSimulationContextCommon* const Context =
			static_cast<const FClothingSimulationContextCommon*>(SkeletalMeshComponent->GetClothingSimulationContext()))
		{
			const int32 ReferenceBoneIndex = GetReferenceBoneIndex();
			const TArray<FTransform>& BoneTransforms = Context->BoneTransforms;

			return BoneTransforms.IsValidIndex(ReferenceBoneIndex) ?
				BoneTransforms[ReferenceBoneIndex] * Context->ComponentToWorld :
				Context->ComponentToWorld;
		}
	}
	return TRigidTransform<float, 3>::Identity;
}

bool FClothingSimulationMesh::WrapDeformLOD(
	int32 PrevLODIndex,
	int32 LODIndex,
	const TVector<float, 3>* Normals,
	const TVector<float, 3>* Positions,
	TVector<float, 3>* OutPositions) const
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothWrapDeformMesh);

	const int32 NumLODsPassed = FMath::Abs(LODIndex - PrevLODIndex);
	if (NumLODsPassed != 1 || !Asset || !Asset->LodData.IsValidIndex(PrevLODIndex) || !Asset->LodData.IsValidIndex(LODIndex))
	{
		return false;
	}

	const FClothLODDataCommon& LODData = Asset->LodData[LODIndex];
	const int32 NumPoints = LODData.PhysicalMeshData.Vertices.Num();
	const TArray<FMeshToMeshVertData>& SkinData = (PrevLODIndex < LODIndex) ?
		LODData.TransitionUpSkinData :
		LODData.TransitionDownSkinData;

	for (int32 Index = 0; Index < NumPoints; ++Index)  // TODO: Profile for parallel for
	{
		const FMeshToMeshVertData& VertData = SkinData[Index];

		const int32 VertIndex0 = (int32)VertData.SourceMeshVertIndices[0];  // Note: The source is uint16. Watch out for large mesh sections!
		const int32 VertIndex1 = (int32)VertData.SourceMeshVertIndices[1];
		const int32 VertIndex2 = (int32)VertData.SourceMeshVertIndices[2];

		OutPositions[Index] = 
			Positions[VertIndex0] * VertData.PositionBaryCoordsAndDist.X + Normals[VertIndex0] * VertData.PositionBaryCoordsAndDist.W +
			Positions[VertIndex1] * VertData.PositionBaryCoordsAndDist.Y + Normals[VertIndex1] * VertData.PositionBaryCoordsAndDist.W +
			Positions[VertIndex2] * VertData.PositionBaryCoordsAndDist.Z + Normals[VertIndex2] * VertData.PositionBaryCoordsAndDist.W;
	}

	return true;
}

bool FClothingSimulationMesh::WrapDeformLOD(
	int32 PrevLODIndex,
	int32 LODIndex,
	const TVector<float, 3>* Normals,
	const TVector<float, 3>* Positions,
	const TVector<float, 3>* Velocities,
	TVector<float, 3>* OutPositions0,
	TVector<float, 3>* OutPositions1,
	TVector<float, 3>* OutVelocities) const
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothWrapDeformClothLOD);

	const int32 NumLODsPassed = FMath::Abs(LODIndex - PrevLODIndex);
	if (NumLODsPassed != 1 || !Asset || !Asset->LodData.IsValidIndex(PrevLODIndex) || !Asset->LodData.IsValidIndex(LODIndex))
	{
		return false;
	}

	const FClothLODDataCommon& LODData = Asset->LodData[LODIndex];
	const int32 NumPoints = LODData.PhysicalMeshData.Vertices.Num();
	const TArray<FMeshToMeshVertData>& SkinData = (PrevLODIndex < LODIndex) ?
		LODData.TransitionUpSkinData :
		LODData.TransitionDownSkinData;

	for (int32 Index = 0; Index < NumPoints; ++Index)  // TODO: Profile for parallel for
	{
		const FMeshToMeshVertData& VertData = SkinData[Index];

		const int32 VertIndex0 = (int32)VertData.SourceMeshVertIndices[0];  // Note: The source is uint16. Watch out for large mesh sections!
		const int32 VertIndex1 = (int32)VertData.SourceMeshVertIndices[1];
		const int32 VertIndex2 = (int32)VertData.SourceMeshVertIndices[2];

		OutPositions0[Index] = OutPositions1[Index] =
			Positions[VertIndex0] * VertData.PositionBaryCoordsAndDist.X + Normals[VertIndex0] * VertData.PositionBaryCoordsAndDist.W +
			Positions[VertIndex1] * VertData.PositionBaryCoordsAndDist.Y + Normals[VertIndex1] * VertData.PositionBaryCoordsAndDist.W +
			Positions[VertIndex2] * VertData.PositionBaryCoordsAndDist.Z + Normals[VertIndex2] * VertData.PositionBaryCoordsAndDist.W;

		OutVelocities[Index] = 
			Velocities[VertIndex0] * VertData.PositionBaryCoordsAndDist.X +
			Velocities[VertIndex1] * VertData.PositionBaryCoordsAndDist.Y +
			Velocities[VertIndex2] * VertData.PositionBaryCoordsAndDist.Z;
	}

	return true;
}

// Inline function used to force the unrolling of the skinning loop
FORCEINLINE static void AddInfluence(FVector& OutPosition, FVector& OutNormal, const FVector& RefParticle, const FVector& RefNormal, const FMatrix& BoneMatrix, const float Weight)
{
	OutPosition += BoneMatrix.TransformPosition(RefParticle) * Weight;
	OutNormal += BoneMatrix.TransformVector(RefNormal) * Weight;
}

void FClothingSimulationMesh::SkinPhysicsMesh(int32 LODIndex, const TVector<float, 3>& LocalSpaceLocation, TVector<float, 3>* OutPositions, TVector<float, 3>* OutNormals) const
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothSkinPhysicsMesh);
	SCOPE_CYCLE_COUNTER(STAT_ClothSkinPhysMesh);

	check(Asset && Asset->LodData.IsValidIndex(LODIndex));
	const FClothPhysicalMeshData& PhysicalMeshData = Asset->LodData[LODIndex].PhysicalMeshData;
	UE_CLOG(PhysicalMeshData.MaxBoneWeights > 12, LogChaosCloth, Warning, TEXT("The cloth physics mesh skinning code can't cope with more than 12 bone influences."));

	const uint32 NumPoints = PhysicalMeshData.Vertices.Num();

	check(SkeletalMeshComponent && SkeletalMeshComponent->GetClothingSimulationContext());
	const FClothingSimulationContextCommon* const Context = static_cast<const FClothingSimulationContextCommon*>(SkeletalMeshComponent->GetClothingSimulationContext());
	FTransform ComponentToLocalSpace = Context->ComponentToWorld;
	ComponentToLocalSpace.AddToTranslation(-LocalSpaceLocation);

	// Zero out positions & normals
	FMemory::Memzero((uint8*)OutPositions, NumPoints * sizeof(TVector<float, 3>));  // PS4 performance note: It is faster to zero the memory first
	FMemory::Memzero((uint8*)OutNormals, NumPoints * sizeof(TVector<float, 3>));    // instead of changing this function to work with uninitialized memory

	const int32* const RESTRICT BoneMap = Asset->UsedBoneIndices.GetData();
	const FMatrix* const RESTRICT BoneMatrices = Context->RefToLocals.GetData();
		
	static const uint32 MinParallelVertices = 500;  // 500 seems to be the lowest threshold still giving gains even on profiled assets that are only using a small number of influences

	ParallelFor(NumPoints, [&PhysicalMeshData, &ComponentToLocalSpace, BoneMap, BoneMatrices, &OutPositions, &OutNormals](uint32 VertIndex)
	{
		const uint16* const RESTRICT BoneIndices = PhysicalMeshData.BoneData[VertIndex].BoneIndices;
		const float* const RESTRICT BoneWeights = PhysicalMeshData.BoneData[VertIndex].BoneWeights;

		// WARNING - HORRIBLE UNROLLED LOOP + JUMP TABLE BELOW
		// done this way because this is a pretty tight and performance critical loop. essentially
		// rather than checking each influence we can just jump into this switch and fall through
		// everything to compose the final skinned data
		const TVector<float, 3>& RefParticle = PhysicalMeshData.Vertices[VertIndex];
		const TVector<float, 3>& RefNormal = PhysicalMeshData.Normals[VertIndex];
		TVector<float, 3>& OutPosition = OutPositions[VertIndex];
		TVector<float, 3>& OutNormal = OutNormals[VertIndex];
		switch (PhysicalMeshData.BoneData[VertIndex].NumInfluences)
		{
		case 12: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[11]]], BoneWeights[11]);  // Intentional fall through
		case 11: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[10]]], BoneWeights[10]);  // Intentional fall through
		case 10: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 9]]], BoneWeights[ 9]);  // Intentional fall through
		case  9: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 8]]], BoneWeights[ 8]);  // Intentional fall through
		case  8: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 7]]], BoneWeights[ 7]);  // Intentional fall through
		case  7: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 6]]], BoneWeights[ 6]);  // Intentional fall through
		case  6: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 5]]], BoneWeights[ 5]);  // Intentional fall through
		case  5: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 4]]], BoneWeights[ 4]);  // Intentional fall through
		case  4: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 3]]], BoneWeights[ 3]);  // Intentional fall through
		case  3: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 2]]], BoneWeights[ 2]);  // Intentional fall through
		case  2: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 1]]], BoneWeights[ 1]);  // Intentional fall through
		case  1: AddInfluence(OutPosition, OutNormal, RefParticle, RefNormal, BoneMatrices[BoneMap[BoneIndices[ 0]]], BoneWeights[ 0]);  // Intentional fall through
		default: break;
		}

		OutPosition = ComponentToLocalSpace.TransformPosition(OutPosition);
		OutNormal = ComponentToLocalSpace.TransformVector(OutNormal);
		OutNormal.Normalize();
	}, NumPoints > MinParallelVertices ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
}

void FClothingSimulationMesh::Update(
	FClothingSimulationSolver* Solver,
	int32 PrevLODIndex,
	int32 LODIndex,
	int32 PrevOffset,
	int32 Offset)
{
	check(Solver);

	// Exit if any inputs are missing or not ready, and if the LOD is invalid
	if (!Asset || !Asset->LodData.IsValidIndex(LODIndex) || !SkeletalMeshComponent || !SkeletalMeshComponent->GetClothingSimulationContext())
	{
		return;
	}

	// Skin current LOD positions
	const TVector<float, 3>& LocalSpaceLocation = Solver->GetLocalSpaceLocation();
	TVector<float, 3>* const OutPositions = Solver->GetAnimationPositions(Offset);
	TVector<float, 3>* const OutNormals = Solver->GetAnimationNormals(Offset);
	
	SkinPhysicsMesh(LODIndex, LocalSpaceLocation, OutPositions, OutNormals);

	// Update old positions after LOD Switching
	if (LODIndex != PrevLODIndex)
	{
		// TODO: Using the more accurate skinning method here would require double buffering the context at the skeletal mesh level
		const TVector<float, 3>* const SrcWrapNormals = Solver->GetAnimationNormals(PrevOffset);  // No need to keep an old normals array around, since the LOD has just changed
		const TVector<float, 3>* const SrcWrapPositions = Solver->GetOldAnimationPositions(PrevOffset);
		TVector<float, 3>* const OutOldPositions = Solver->GetOldAnimationPositions(Offset);

		const bool bValidWrap = WrapDeformLOD(PrevLODIndex, LODIndex, SrcWrapNormals, SrcWrapPositions, OutOldPositions);
	
		if (!bValidWrap)
		{
			// The previous LOD is invalid, reset old positions with the new LOD
			const FClothPhysicalMeshData& PhysicalMeshData = Asset->LodData[LODIndex].PhysicalMeshData;
			const int32 NumPoints = PhysicalMeshData.Vertices.Num();

			for (int32 Index = 0; Index < NumPoints; ++Index)
			{
				OutOldPositions[Index] = OutPositions[Index];
			}
		}
	}
}
