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
			FReal InMassValue,
			FReal InMinPerParticleMass,
			FRealSingle InEdgeStiffness,
			FRealSingle InBendingStiffness,
			bool bInUseBendingElements,
			FRealSingle InAreaStiffness,
			FRealSingle InVolumeStiffness,
			bool bInUseThinShellVolumeConstraints,
			const FVec2& InTetherStiffness,
			FRealSingle InLimitScale,
			ETetherMode InTetherMode,
			FRealSingle InMaxDistancesMultiplier,
			const FVec2& InAnimDriveStiffness,
			const FVec2& InAnimDriveDamping,
			FRealSingle InShapeTargetStiffness,
			bool bInUseXPBDConstraints,
			FRealSingle InGravityScale,
			bool bIsGravityOverridden,
			const FVec3& InGravityOverride,
			const FVec3& InLinearVelocityScale,
			FRealSingle InAngularVelocityScale,
			FRealSingle InFictitiousAngularScale,
			FRealSingle InDragCoefficient,
			FRealSingle InLiftCoefficient,
			bool bInUseLegacyWind,
			FRealSingle InDampingCoefficient,
			FRealSingle InCollisionThickness,
			FRealSingle InFrictionCoefficient,
			bool bInUseCCD,
			bool bInUseSelfCollisions,
			FRealSingle InSelfCollisionThickness,
			bool bInUseLegacyBackstop,
			bool bInUseLODIndexOverride,
			int32 InLODIndexOverride);
		~FClothingSimulationCloth();

		uint32 GetGroupId() const { return GroupId; }
		uint32 GetLODIndex(const FClothingSimulationSolver* Solver) const { return LODIndices.FindChecked(Solver); }

		int32 GetNumActiveKinematicParticles() const { return NumActiveKinematicParticles; }
		int32 GetNumActiveDynamicParticles() const { return NumActiveDynamicParticles; }

		// ---- Animatable property setters ----
		void SetMaxDistancesMultiplier(FRealSingle InMaxDistancesMultiplier) { MaxDistancesMultiplier = InMaxDistancesMultiplier; }

		void SetMaterialProperties(FRealSingle InEdgeStiffness, FRealSingle InBendingStiffness, FRealSingle InAreaStiffness) { EdgeStiffness = InEdgeStiffness; BendingStiffness = InBendingStiffness; AreaStiffness = InAreaStiffness; }
		void SetLongRangeAttachmentProperties(const FVec2& InTetherStiffness) { TetherStiffness = InTetherStiffness; }
		void SetCollisionProperties(FRealSingle InCollisionThickness, FRealSingle InFrictionCoefficient, bool bInUseCCD, FRealSingle InSelfCollisionThickness) { CollisionThickness = InCollisionThickness; FrictionCoefficient = InFrictionCoefficient; bUseCCD = bInUseCCD; SelfCollisionThickness = InSelfCollisionThickness; }
		void SetDampingProperties(FRealSingle InDampingCoefficient) { DampingCoefficient = InDampingCoefficient; }
		void SetAerodynamicsProperties(FRealSingle InDragCoefficient, FRealSingle InLiftCoefficient, const FVec3& InWindVelocity) { DragCoefficient = InDragCoefficient; LiftCoefficient = InLiftCoefficient; WindVelocity = InWindVelocity; }
		void SetGravityProperties(FRealSingle InGravityScale, bool bInIsGravityOverridden, const FVec3& InGravityOverride) { GravityScale = InGravityScale; bIsGravityOverridden = bInIsGravityOverridden; GravityOverride = InGravityOverride; }
		void SetAnimDriveProperties(const FVec2& InAnimDriveStiffness, const FVec2& InAnimDriveDamping) { AnimDriveStiffness = InAnimDriveStiffness; AnimDriveDamping = InAnimDriveDamping; }
		void SetVelocityScaleProperties(const FVec3& InLinearVelocityScale, FRealSingle InAngularVelocityScale, FRealSingle InFictitiousAngularScale) { LinearVelocityScale = InLinearVelocityScale; AngularVelocityScale = InAngularVelocityScale; FictitiousAngularScale = InFictitiousAngularScale;  }

		void GetAnimDriveProperties(FVec2& OutAnimDriveStiffness, FVec2& OutAnimDriveDamping) { OutAnimDriveStiffness = AnimDriveStiffness; OutAnimDriveDamping = AnimDriveDamping; }

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
		TConstArrayView<FVec3> GetAnimationPositions(const FClothingSimulationSolver* Solver) const;
		// Return the solver's input normals for this cloth source current LOD, not thread safe, call must be done right after the solver update.
		TConstArrayView<FVec3> GetAnimationNormals(const FClothingSimulationSolver* Solver) const;
		// Return the solver's positions for this cloth current LOD, not thread safe, call must be done right after the solver update.
		TConstArrayView<FVec3> GetParticlePositions(const FClothingSimulationSolver* Solver) const;
		// Return the solver's previous frame positions for this cloth current LOD, not thread safe, call must be done right after the solver update.
		TConstArrayView<FVec3> GetParticleOldPositions(const FClothingSimulationSolver* Solver) const;
		// Return the solver's normals for this cloth current LOD, not thread safe, call must be done right after the solver update.
		TConstArrayView<FVec3> GetParticleNormals(const FClothingSimulationSolver* Solver) const;
		// Return the solver's normals for this cloth current LOD, not thread safe, call must be done right after the solver update.
		TConstArrayView<FReal> GetParticleInvMasses(const FClothingSimulationSolver* Solver) const;
		// Return the current gravity as applied by the solver using the various overrides, not thread safe, call must be done right after the solver update.
		FVec3 GetGravity(const FClothingSimulationSolver* Solver) const;
		// Return the current bounding box based on a given solver, not thread safe, call must be done right after the solver update.
		FAABB3 CalculateBoundingBox(const FClothingSimulationSolver* Solver) const;
		// Return the current LOD Offset in the solver's particle array, or INDEX_NONE if no LOD is currently selected
		int32 GetOffset(const FClothingSimulationSolver* Solver) const;
		// Return the current LOD Mesh
		const FTriangleMesh& GetTriangleMesh(const FClothingSimulationSolver* Solver) const;
		// Return the current LOD Weightmaps
		const TArray<TConstArrayView<FRealSingle>>& GetWeightMaps(const FClothingSimulationSolver* Solver) const;
		// Return the reference bone index for this cloth
		int32 GetReferenceBoneIndex() const;
		// Return the local reference space transform for this cloth
		const FRigidTransform3& GetReferenceSpaceTransform() const { return ReferenceSpaceTransform;  }
		// ---- End of the debugging/visualization functions

		// ---- Solver interface ----
		void Add(FClothingSimulationSolver* Solver);
		void Remove(FClothingSimulationSolver* Solver);

		void PreUpdate(FClothingSimulationSolver* Solver);
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
			const TArray<TConstArrayView<FRealSingle>> WeightMaps;

			// Per Solver data
			struct FSolverData
			{
				int32 Offset;
				FTriangleMesh TriangleMesh;  // TODO: Triangle Mesh shouldn't really be solver dependent (ie not use an offset)
			};
			TMap<FClothingSimulationSolver*, FSolverData> SolverData;

			// Stats
			int32 NumKinenamicParticles;
			int32 NumDynammicParticles;

			FLODData(int32 InNumParticles, const TConstArrayView<uint32>& InIndices, const TArray<TConstArrayView<FRealSingle>>& InWeightMaps);

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
		FReal MassValue;
		FReal MinPerParticleMass;
		FRealSingle EdgeStiffness;
		FRealSingle BendingStiffness;
		bool bUseBendingElements;
		FRealSingle AreaStiffness;
		FRealSingle VolumeStiffness;
		bool bUseThinShellVolumeConstraints;
		FVec2 TetherStiffness;
		FRealSingle LimitScale;
		ETetherMode TetherMode;
		FRealSingle MaxDistancesMultiplier;
		FVec2 AnimDriveStiffness;
		FVec2 AnimDriveDamping;
		FRealSingle ShapeTargetStiffness;
		bool bUseXPBDConstraints;
		FRealSingle GravityScale;
		bool bIsGravityOverridden;
		FVec3 GravityOverride;
		FVec3 LinearVelocityScale;  // Linear ratio applied to the reference bone transforms
		FRealSingle AngularVelocityScale;  // Angular ratio factor applied to the reference bone transforms
		FRealSingle FictitiousAngularScale;
		FRealSingle DragCoefficient;
		FRealSingle LiftCoefficient;
		FVec3 WindVelocity;
		bool bUseLegacyWind;
		FRealSingle DampingCoefficient;
		FRealSingle CollisionThickness;
		FRealSingle FrictionCoefficient;
		bool bUseCCD;
		bool bUseSelfCollisions;
		FRealSingle SelfCollisionThickness;
		bool bUseLegacyBackstop;
		bool bUseLODIndexOverride;
		int32 LODIndexOverride;
		bool bNeedsReset;
		bool bNeedsTeleport;

		// Reference space transform
		FRigidTransform3 ReferenceSpaceTransform;  // TODO: Add override in the style of LODIndexOverride

		// LOD data
		TArray<FLODData> LODData;
		TMap<FClothingSimulationSolver*, int32> LODIndices;

		// Stats
		int32 NumActiveKinematicParticles;
		int32 NumActiveDynamicParticles;
	};
} // namespace Chaos
