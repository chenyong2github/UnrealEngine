// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Transform.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/AABB.h"
#include "Containers/ContainersFwd.h"
#include "ChaosCloth/ChaosClothConstraints.h"

namespace Chaos
{
	class FClothingSimulationSolver;
	class FClothingSimulationMesh;
	class FClothingSimulationCollider;

	// Cloth simulation node
	class FClothingSimulationCloth final
	{
	public:
		enum EMassMode
		{
			UniformMass,
			TotalMass,
			Density
		};

		typedef FClothConstraints::ETetherMode ETetherMode;

		FClothingSimulationCloth(
			FClothingSimulationMesh* InMesh,
			TArray<FClothingSimulationCollider*>&& InColliders,
			uint32 InGroupId,
			EMassMode InMassMode,
			float InMassValue,
			float InMinPerParticleMass,
			float InEdgeStiffness,
			float InBendingStiffness,
			bool bInUseBendingElements,
			float InAreaStiffness,
			float InVolumeStiffness,
			bool bInUseThinShellVolumeConstraints,
			float InStrainLimitingStiffness,
			float InLimitScale,
			ETetherMode InTetherMode,
			float InMaxDistancesMultiplier,
			float InAnimDriveSpringStiffness,
			float InShapeTargetStiffness,
			bool bInUseXPBDConstraints,
			float InGravityScale,
			bool bIsGravityOverridden,
			const TVector<float, 3>& InGravityOverride,
			const TVector<float, 3>& InLinearVelocityScale,
			float InAngularVelocityScale,
			float InDragCoefficient,
			float InLiftCoefficient,
			bool bInUseLegacyWind,
			float InDampingCoefficient,
			float InCollisionThickness,
			float InFrictionCoefficient,
			bool bInUseSelfCollisions,
			float InSelfCollisionThickness,
			bool bInUseLegacyBackstop,
			bool bInUseLODIndexOverride,
			int32 InLODIndexOverride);
		~FClothingSimulationCloth();

		uint32 GetGroupId() const { return GroupId; }
		uint32 GetLODIndex(const FClothingSimulationSolver* Solver) const { return LODIndices.FindChecked(Solver); }

		int32 GetNumActiveKinematicParticles() const { return NumActiveKinematicParticles; }
		int32 GetNumActiveDynamicParticles() const { return NumActiveDynamicParticles; }

		// ---- Animatable property setters ----
		void SetMaxDistancesMultiplier(float InMaxDistancesMultiplier) { MaxDistancesMultiplier = InMaxDistancesMultiplier; }
		void SetAnimDriveSpringStiffness(float InAnimDriveSpringStiffness) { AnimDriveSpringStiffness = InAnimDriveSpringStiffness; }

		void Reset() { bNeedsReset = true; }
		void Teleport() { bNeedsTeleport = true; }
		// ---- End of the animatable property setters ----

		// ---- Node property getters/setters
		FClothingSimulationMesh* GetMesh() const { return Mesh; }
		void SetMesh(FClothingSimulationMesh* InMesh);

		const TArray<FClothingSimulationCollider*>& GetColliders() const { return Colliders; }
		void SetColliders(TArray<FClothingSimulationCollider*>&& InColliders);
		void AddCollider(FClothingSimulationCollider* InCollider);
		void RemoveCollider(FClothingSimulationCollider* InCollider);
		void RemoveColliders();
		// ---- End of the Node property getters/setters

		// ---- Debugging/visualization functions
		// Return the solver's input positions for this cloth source current LOD, not thread safe, call must be done right after the solver update.
		TConstArrayView<TVector<float, 3>> GetAnimationPositions(const FClothingSimulationSolver* Solver) const;
		// Return the solver's input normals for this cloth source current LOD, not thread safe, call must be done right after the solver update.
		TConstArrayView<TVector<float, 3>> GetAnimationNormals(const FClothingSimulationSolver* Solver) const;
		// Return the solver's positions for this cloth current LOD, not thread safe, call must be done right after the solver update.
		TConstArrayView<TVector<float, 3>> GetParticlePositions(const FClothingSimulationSolver* Solver) const;
		// Return the solver's normals for this cloth current LOD, not thread safe, call must be done right after the solver update.
		TConstArrayView<TVector<float, 3>> GetParticleNormals(const FClothingSimulationSolver* Solver) const;
		// Return the solver's normals for this cloth current LOD, not thread safe, call must be done right after the solver update.
		TConstArrayView<float> GetParticleInvMasses(const FClothingSimulationSolver* Solver) const;
		// Return the current gravity as applied by the solver using the various overrides, not thread safe, call must be done right after the solver update.
		TVector<float, 3> GetGravity(const FClothingSimulationSolver* Solver) const;
		// Return the current bounding box based on a given solver, not thread safe, call must be done right after the solver update.
		TAABB<float, 3> CalculateBoundingBox(const FClothingSimulationSolver* Solver) const;
		// Return the current LOD Offset in the solver's particle array, or INDEX_NONE if no LOD is currently selected
		int32 GetOffset(const FClothingSimulationSolver* Solver) const;
		// Return the current LOD Mesh
		const TTriangleMesh<float>& GetTriangleMesh(const FClothingSimulationSolver* Solver) const;
		// Return the current LOD Weightmaps
		const TArray<TConstArrayView<float>>& GetWeightMaps(const FClothingSimulationSolver* Solver) const;
		// Return the reference bone index for this cloth
		int32 GetReferenceBoneIndex() const;
		// Return the local reference space transform for this cloth
		const TRigidTransform<float, 3>& GetReferenceSpaceTransform() const { return ReferenceSpaceTransform;  }
		// ---- End of the debugging/visualization functions

		// ---- Solver interface ----
		void Add(FClothingSimulationSolver* Solver);
		void Remove(FClothingSimulationSolver* Solver);

		void Update(FClothingSimulationSolver* Solver);
		void PostUpdate(FClothingSimulationSolver* Solver);
		// ---- End of the Solver interface ----

	private:
		int32 GetNumParticles(int32 InLODIndex) const;
		int32 GetOffset(const FClothingSimulationSolver* Solver, int32 InLODIndex) const;

	private:
		struct FLODData
		{
			// Input mesh
			const int32 NumParticles;
			const TConstArrayView<uint32> Indices;
			const TArray<TConstArrayView<float>> WeightMaps;

			// Per Solver data
			struct FSolverData
			{
				int32 Offset;
				TTriangleMesh<float> TriangleMesh;  // TODO: Triangle Mesh shouldn't really be solver dependent (ie not use an offset)
			};
			TMap<FClothingSimulationSolver*, FSolverData> SolverData;

			// Stats
			int32 NumKinenamicParticles;
			int32 NumDynammicParticles;

			FLODData(int32 InNumParticles, const TConstArrayView<uint32>& InIndices, const TArray<TConstArrayView<float>>& InWeightMaps);

			void Add(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth, int32 LODIndex);
			void Remove(FClothingSimulationSolver* Solver);

			void Update(FClothingSimulationSolver* Solver, FClothingSimulationCloth* Cloth);

			void Enable(FClothingSimulationSolver* Solver, bool bEnable) const;

			void ResetStartPose(FClothingSimulationSolver* Solver) const;

			void UpdateNormals(FClothingSimulationSolver* Solver) const;
		};

		// Cloth parameters
		FClothingSimulationMesh* Mesh;
		TArray<FClothingSimulationCollider*> Colliders;
		uint32 GroupId;
		EMassMode MassMode;
		float MassValue;
		float MinPerParticleMass;
		float EdgeStiffness;
		float BendingStiffness;
		bool bUseBendingElements;
		float AreaStiffness;
		float VolumeStiffness;
		bool bUseThinShellVolumeConstraints;
		float StrainLimitingStiffness;
		float LimitScale;
		ETetherMode TetherMode;
		float MaxDistancesMultiplier;  // Animatable
		float AnimDriveSpringStiffness;  // Animatable
		float ShapeTargetStiffness;
		bool bUseXPBDConstraints;
		float GravityScale;
		bool bIsGravityOverridden;
		TVector<float, 3> GravityOverride;
		TVector<float, 3> LinearVelocityScale;  // Linear ratio applied to the reference bone transforms
		float AngularVelocityScale;  // Angular ratio factor applied to the reference bone transforms
		float DragCoefficient;
		float LiftCoefficient;
		bool bUseLegacyWind;
		float DampingCoefficient;
		float CollisionThickness;
		float FrictionCoefficient;
		bool bUseSelfCollisions;
		float SelfCollisionThickness;
		bool bUseLegacyBackstop;
		bool bUseLODIndexOverride;
		int32 LODIndexOverride;
		bool bNeedsReset;
		bool bNeedsTeleport;

		// Reference space transform
		TRigidTransform<float, 3> ReferenceSpaceTransform;  // TODO: Add override in the style of LODIndexOverride

		// LOD data
		TArray<FLODData> LODData;
		TMap<FClothingSimulationSolver*, int32> LODIndices;

		// Stats
		int32 NumActiveKinematicParticles;
		int32 NumActiveDynamicParticles;
	};
} // namespace Chaos