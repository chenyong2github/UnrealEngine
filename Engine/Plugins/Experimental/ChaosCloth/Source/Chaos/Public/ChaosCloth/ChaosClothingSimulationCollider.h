// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothCollisionData.h"
#include "Containers/ContainersFwd.h"

class USkeletalMeshComponent;
class UClothingAssetCommon;
class FClothingSimulationContextCommon;

namespace Chaos
{
	class FClothingSimulationSolver;
	class FClothingSimulationCloth;

	// Collider simulation node
	class FClothingSimulationCollider final
	{
	public:
		enum class ECollisionDataType : int32
		{
			Global = 0,  // Global collision slot (physics collisions)
			Dynamic,  // Dynamic collision slot (aka external collisions)
			LODs,  // LODIndex based start slot (LODs collisions)
		};

		FClothingSimulationCollider(
			const UClothingAssetCommon* InAsset,  // Cloth asset for collision data, can be nullptr
			const USkeletalMeshComponent* InSkeletalMeshComponent,  // For asset LODs management, can be nullptr
			bool bInUseLODIndexOverride,
			int32 InLODIndexOverride);
		~FClothingSimulationCollider();

		int32 GetNumGeometries() const { int32 NumGeometries = 0; for (const FLODData& LODDatum : LODData) { NumGeometries += LODDatum.NumGeometries; } return NumGeometries; }

		// Return source (untransformed) collision data for global, dynamic and active LODs.
		FClothCollisionData GetCollisionData() const;

		// ---- Animatable property setters ----
		// Set dynamic collision data, will only get updated when used as a Solver Collider TODO: Subclass collider?
		void SetCollisionData(const FClothCollisionData* InCollisionData) { CollisionData = InCollisionData; }
		// ---- End of the animatable property setters ----

		// ---- Cloth/Solver interface ----
		void Add(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth = nullptr);
		void Remove(FClothingSimulationSolver* /*Solver*/, FClothingSimulationCloth* /*Cloth*/ = nullptr) {}

		void Update(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth = nullptr);
		// ---- End of the Cloth/Solver interface ----

		// ---- Debugging and visualization functions ----
		// Return currently LOD active collision particles translations, not thread safe, to use after solver update.
		TConstArrayView<TVector<float, 3>> GetCollisionTranslations(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth, ECollisionDataType CollisionDataType) const;

		// Return currently LOD active collision particles rotations, not thread safe, to use after solver update.
		TConstArrayView<TRotation<float, 3>> GetCollisionRotations(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth, ECollisionDataType CollisionDataType) const;

		// Return currently LOD active collision geometries, not thread safe, to use after solver update.
		TConstArrayView<TUniquePtr<FImplicitObject>> GetCollisionGeometries(const FClothingSimulationSolver* Solver, const FClothingSimulationCloth* Cloth, ECollisionDataType CollisionDataType) const;
		// ---- End of the debugging and visualization functions ----

	private:
		void ExtractPhysicsAssetCollision(FClothCollisionData& ClothCollisionData, TArray<int32>& UsedBoneIndices);

	private:
		struct FLODData
		{
			FClothCollisionData ClothCollisionData;
			int32 NumGeometries;  // Number of collision bodies
			TMap<TPair<const FClothingSimulationSolver*, const FClothingSimulationCloth*>, int32> Offsets;  // Solver particle offset

			FLODData() : NumGeometries(0) {}

			void Add(
				FClothingSimulationSolver* Solver,
				FClothingSimulationCloth* Cloth,
				const FClothCollisionData& InClothCollisionData,
				const TArray<int32>& UsedBoneIndices = TArray<int32>());

			void Update(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, const FClothingSimulationContextCommon* Context);

			void Enable(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, bool bEnable);

			void ResetStartPose(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth);

			FORCEINLINE static int32 GetMappedBoneIndex(const TArray<int32>& UsedBoneIndices, int32 BoneIndex)
			{
				return UsedBoneIndices.IsValidIndex(BoneIndex) ? UsedBoneIndices[BoneIndex] : INDEX_NONE;
			}
		};

		const UClothingAssetCommon* Asset;
		const USkeletalMeshComponent* SkeletalMeshComponent;
		const FClothCollisionData* CollisionData;
		bool bUseLODIndexOverride;
		int32 LODIndexOverride;

		// Collision primitives
		TArray<FLODData> LODData;  // Actual LODs start at LODStart
		int32 LODIndex;  // TODO: Have map of LODIndex per solver
	};
} // namespace Chaos