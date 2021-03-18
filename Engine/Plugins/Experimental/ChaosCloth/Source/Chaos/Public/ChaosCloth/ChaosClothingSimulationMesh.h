// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Containers/ContainersFwd.h"
#include "Chaos/Transform.h"

class USkeletalMeshComponent;
class UClothingAssetCommon;

namespace Chaos
{
	class FClothingSimulationSolver;

	// Mesh simulation node
	class FClothingSimulationMesh final
	{
	public:
		FClothingSimulationMesh(const UClothingAssetCommon* InAsset, const USkeletalMeshComponent* InSkeletalMeshComponent);
		~FClothingSimulationMesh();

		// ---- Node property getters/setters
		const UClothingAssetCommon* GetAsset() const { return Asset; }
		const USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }
		// ---- End of node property getters/setters

		// ---- Cloth interface ----
		void Update(
			FClothingSimulationSolver* Solver,
			int32 PrevLODIndex,
			int32 LODIndex,
			int32 PrevOffset,
			int32 Offset);

		// Return the LOD Index specified by the input SkeletalMeshComponent. Note that this is not the same as the current LOD index, and that Mesh LOD changes are always driven by the Cloth.
		int32 GetLODIndex() const;
		int32 GetNumLODs() const;
		int32 GetNumPoints(int32 LODIndex) const;
		TConstArrayView<const uint32> GetIndices(int32 LODIndex) const;
		TArray<TConstArrayView<const FRealSingle>> GetWeightMaps(int32 LODIndex) const;
		int32 GetReferenceBoneIndex() const;
		FRigidTransform3 GetReferenceBoneTransform() const;

		bool WrapDeformLOD(
			int32 PrevLODIndex,
			int32 LODIndex,
			const FVec3* Normals,
			const FVec3* Positions,
			FVec3* OutPositions) const;

		bool WrapDeformLOD(
			int32 PrevLODIndex,
			int32 LODIndex,
			const FVec3* Normals,
			const FVec3* Positions,
			const FVec3* Velocities,
			FVec3* OutPositions0,
			FVec3* OutPositions1,
			FVec3* OutVelocities) const;
		// ---- End of the Cloth interface ----

	private:
		void SkinPhysicsMesh(
			int32 LODIndex,
			const FVec3& LocalSpaceLocation,
			FVec3* OutPositions,
			FVec3* OutNormals) const;

	private:
		const UClothingAssetCommon* Asset;
		const USkeletalMeshComponent* SkeletalMeshComponent;
	};
} // namespace Chaos
