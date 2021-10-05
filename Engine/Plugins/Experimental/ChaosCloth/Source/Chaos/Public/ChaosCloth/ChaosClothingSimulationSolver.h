// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ChaosCloth/ChaosClothConstraints.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Transform.h"
#include "Chaos/ImplicitObject.h"
#include "PhysicsProxy/PerSolverFieldSystem.h"

class USkeletalMeshComponent;
class UClothingAssetCommon;

namespace Chaos
{
	class FTriangleMesh;
	class FPBDEvolution;
	class FVelocityField;

	class FClothingSimulationCloth;
	class FClothingSimulationCollider;
	class FClothingSimulationMesh;

	// Solver simulation node
	class FClothingSimulationSolver final
	{
	public:
		FClothingSimulationSolver();
		~FClothingSimulationSolver();

		// ---- Animatable property setters ----
		void SetLocalSpaceLocation(const FVec3& InLocalSpaceLocation, bool bReset = false);
		const FVec3& GetLocalSpaceLocation() const { return LocalSpaceLocation; }

		// Disables all Cloths gravity override mechanism
		void EnableClothGravityOverride(bool bInIsClothGravityOverrideEnabled) { bIsClothGravityOverrideEnabled = bInIsClothGravityOverrideEnabled; }
		bool IsClothGravityOverrideEnabled() const { return bIsClothGravityOverrideEnabled; }
		void SetGravity(const FVec3& InGravity) { Gravity = InGravity; }
		const FVec3& GetGravity() const { return Gravity; }

		void SetWindVelocity(const FVec3& InWindVelocity, FRealSingle InLegacyWindAdaption = 0.f);
		const FVec3& GetWindVelocity() const { return WindVelocity; }
		void SetWindFluidDensity(FRealSingle InWindFluidDensity) { WindFluidDensity = InWindFluidDensity; }

		void SetNumIterations(int32 InNumIterations) { NumIterations = InNumIterations; }
		int32 GetNumIterations() const { return NumIterations; }
		void SetNumSubsteps(int32 InNumSubsteps) { NumSubsteps = InNumSubsteps; }
		int32 GetNumSubsteps() const { return NumSubsteps; }
		// ---- End of the animatable property setters ----

		// ---- Object management functions ----
		void SetCloths(TArray<FClothingSimulationCloth*>&& InCloths); 
		void AddCloth(FClothingSimulationCloth* InCloth);
		void RemoveCloth(FClothingSimulationCloth* InCloth);
		void RemoveCloths();

		void RefreshCloth(FClothingSimulationCloth* InCloth);
		void RefreshCloths();

		TConstArrayView<const FClothingSimulationCloth*> GetCloths() const { return Cloths; }

		// Update solver properties before simulation.
		void Update(FReal InDeltaTime);

		FBoxSphereBounds CalculateBounds() const;
		// ---- End of the object management functions ----

		// ---- Cloth interface ----
		int32 AddParticles(int32 NumParticles, uint32 GroupId);
		void EnableParticles(int32 Offset, bool bEnable);

		// Get the currrent solver time
		FReal GetTime() const { return Time; }
		void SetParticleMassUniform(int32 Offset, FReal UniformMass, FReal MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate);
		void SetParticleMassFromTotalMass(int32 Offset, FReal TotalMass, FReal MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate);
		void SetParticleMassFromDensity(int32 Offset, FReal Density, FReal MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate);


		// Set the amount of velocity allowed to filter from the given change in reference space transform, including local simulation space.
		void SetReferenceVelocityScale(uint32 GroupId,
			const FRigidTransform3& OldReferenceSpaceTransform,
			const FRigidTransform3& ReferenceSpaceTransform,
			const FVec3& LinearVelocityScale,
			FReal AngularVelocityScale,
			FReal FictitiousAngularScale);

		// Set general cloth simulation properties.
		void SetProperties(uint32 GroupId, FRealSingle DampingCoefficient, FRealSingle CollisionThickness, FRealSingle FrictionCoefficient);

		// Set whether to use continuous collision detection.
		void SetUseCCD(uint32 GroupId, bool bUseCCD);

		// Set per group gravity, used to override solver's gravity. Must be called during cloth update.
		void SetGravity(uint32 GroupId, const FVec3& Gravity);

		// Set per group wind velocity, used to override solver's wind velocity. Must be called during cloth update.
		void SetWindVelocity(uint32 GroupId, const FVec3& InWindVelocity);

		// Set the geometry affected by wind, or disable if TriangleMesh is null.
		void SetWindVelocityField(uint32 GroupId, FRealSingle DragCoefficient, FRealSingle LiftCoefficient, const FTriangleMesh* TriangleMesh = nullptr);
		const FVelocityField&  GetWindVelocityField(uint32 GroupId);

		// Add external forces to the particles
		void AddExternalForces(uint32 GroupId, bool bUseLegacyWind);

		const FVec3* GetOldAnimationPositions(int32 Offset) const { return OldAnimationPositions.GetData() + Offset; }
		FVec3* GetOldAnimationPositions(int32 Offset) { return OldAnimationPositions.GetData() + Offset; }
		const FVec3* GetAnimationPositions(int32 Offset) const { return AnimationPositions.GetData() + Offset; }
		FVec3* GetAnimationPositions(int32 Offset) { return AnimationPositions.GetData() + Offset; }
		const FVec3* GetAnimationNormals(int32 Offset) const { return AnimationNormals.GetData() + Offset; }
		FVec3* GetAnimationNormals(int32 Offset) { return AnimationNormals.GetData() + Offset; }
		const FVec3* GetNormals(int32 Offset) const { return Normals.GetData() + Offset; }
		FVec3* GetNormals(int32 Offset) { return Normals.GetData() + Offset; }
		const FVec3* GetParticlePs(int32 Offset) const;
		FVec3* GetParticlePs(int32 Offset);
		const FVec3* GetParticleXs(int32 Offset) const;
		FVec3* GetParticleXs(int32 Offset);
		const FVec3* GetParticleVs(int32 Offset) const;
		FVec3* GetParticleVs(int32 Offset);
		const FReal* GetParticleInvMasses(int32 Offset) const;
		const FClothConstraints& GetClothConstraints(int32 Offset) const { return *ClothsConstraints.FindChecked(Offset); }
		FClothConstraints& GetClothConstraints(int32 Offset) { return *ClothsConstraints.FindChecked(Offset); }
		// ---- End of the Cloth interface ----

		// ---- Collider interface ----
		int32 AddCollisionParticles(int32 NumCollisionParticles, uint32 GroupId, int32 RecycledOffset = 0);
		void EnableCollisionParticles(int32 Offset, bool bEnable);

		const int32* GetCollisionBoneIndices(int32 Offset) const { return CollisionBoneIndices.GetData() + Offset; }
		int32* GetCollisionBoneIndices(int32 Offset) { return CollisionBoneIndices.GetData() + Offset; }
		const FRigidTransform3* GetCollisionBaseTransforms(int32 Offset) const { return CollisionBaseTransforms.GetData() + Offset; }
		FRigidTransform3* GetCollisionBaseTransforms(int32 Offset) { return CollisionBaseTransforms.GetData() + Offset; }
		const FRigidTransform3* GetOldCollisionTransforms(int32 Offset) const { return OldCollisionTransforms.GetData() + Offset; }
		FRigidTransform3* GetOldCollisionTransforms(int32 Offset) { return OldCollisionTransforms.GetData() + Offset; }
		const FRigidTransform3* GetCollisionTransforms(int32 Offset) const { return CollisionTransforms.GetData() + Offset; }
		FRigidTransform3* GetCollisionTransforms(int32 Offset) { return CollisionTransforms.GetData() + Offset; }
		const FVec3* GetCollisionParticleXs(int32 Offset) const;
		FVec3* GetCollisionParticleXs(int32 Offset);
		const FRotation3* GetCollisionParticleRs(int32 Offset) const;
		FRotation3* GetCollisionParticleRs(int32 Offset);
		void SetCollisionGeometry(int32 Offset, int32 Index, TUniquePtr<FImplicitObject>&& Geometry);
		const TUniquePtr<FImplicitObject>* GetCollisionGeometries(int32 Offset) const;
		const bool* GetCollisionStatus(int32 Offset) const;
		const TArray<FVec3>& GetCollisionContacts() const;
		const TArray<FVec3>& GetCollisionNormals() const;
		// ---- End of the Collider interface ----

		// ---- Field interface ----
		FPerSolverFieldSystem& GetPerSolverField() { return PerSolverField; }
		const FPerSolverFieldSystem& GetPerSolverField() const { return PerSolverField; }
		// ---- End of the Field interface ----

	private:
		void ResetParticles();
		void ResetCollisionParticles(int32 InCollisionParticlesOffset = 0);
		void ApplyPreSimulationTransforms();
		FReal SetParticleMassPerArea(int32 Offset, int32 Size, const FTriangleMesh& Mesh);
		void ParticleMassUpdateDensity(const FTriangleMesh& Mesh, FReal Density);
		void ParticleMassClampAndEnslave(int32 Offset, int32 Size, FReal MinPerParticleMass, const TFunctionRef<bool(int32)>& KinematicPredicate);

	private:
		TUniquePtr<FPBDEvolution> Evolution;

		// Object arrays
		TArray<FClothingSimulationCloth*> Cloths;

		// Simulation group attributes
		TArrayCollectionArray<FRigidTransform3> PreSimulationTransforms;  // Allow a different frame of reference for each cloth groups
		TArrayCollectionArray<FVec3> FictitiousAngularDisplacement;  // Relative angular displacement of the reference bone that depends on the fictitious angular scale factor

		// Particle attributes
		TArrayCollectionArray<FVec3> Normals;
		TArrayCollectionArray<FVec3> OldAnimationPositions;
		TArrayCollectionArray<FVec3> AnimationPositions;
		TArrayCollectionArray<FVec3> AnimationNormals;

		// Collision particle attributes
		TArrayCollectionArray<int32> CollisionBoneIndices;
		TArrayCollectionArray<FRigidTransform3> CollisionBaseTransforms;
		TArrayCollectionArray<FRigidTransform3> OldCollisionTransforms;
		TArrayCollectionArray<FRigidTransform3> CollisionTransforms;

		// Cloth constraints
		TMap<int32, TUniquePtr<FClothConstraints>> ClothsConstraints;

		// Local space simulation
		FVec3 OldLocalSpaceLocation;
		FVec3 LocalSpaceLocation;  // This is used to translate between world space and simulation space. Add this to simulation space coordinates to get world space coordinates

		// Time stepping
		FReal Time;
		FReal DeltaTime;
		int32 NumIterations;
		int32 NumSubsteps;

		// Solver colliders offset
		int32 CollisionParticlesOffset;  // Collision particle offset on the first solver/non cloth collider
		int32 CollisionParticlesSize;  // Number of solver only colliders

		// Solver parameters
		FVec3 Gravity;
		FVec3 WindVelocity;
		FRealSingle LegacyWindAdaption;
		FRealSingle WindFluidDensity;
		bool bIsClothGravityOverrideEnabled;

		// Field system unique to the cloth solver
		FPerSolverFieldSystem PerSolverField;
	};

} // namespace Chaos
