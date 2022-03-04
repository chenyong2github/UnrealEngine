// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ChaosCloth/ChaosClothConstraints.h"
#include "Chaos/PBDSoftsEvolutionFwd.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Transform.h"
#include "Chaos/ImplicitObject.h"
#include "PhysicsProxy/PerSolverFieldSystem.h"

class USkeletalMeshComponent;
class UClothingAssetCommon;

namespace Chaos
{
	class FTriangleMesh;

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
		void SetLocalSpaceRotation(const FQuat& InLocalSpaceRotation) { LocalSpaceRotation = InLocalSpaceRotation; }
		const FRotation3& GetLocalSpaceRotation() const { return LocalSpaceRotation; }

		// Disables all Cloths gravity override mechanism
		void EnableClothGravityOverride(bool bInIsClothGravityOverrideEnabled) { bIsClothGravityOverrideEnabled = bInIsClothGravityOverrideEnabled; }
		bool IsClothGravityOverrideEnabled() const { return bIsClothGravityOverrideEnabled; }
		void SetGravity(const TVec3<FRealSingle>& InGravity) { Gravity = InGravity; }
		const TVec3<FRealSingle>& GetGravity() const { return Gravity; }

		void SetWindVelocity(const TVec3<FRealSingle>& InWindVelocity, FRealSingle InLegacyWindAdaption = (FRealSingle)0.);
		const TVec3<FRealSingle>& GetWindVelocity() const { return WindVelocity; }

		UE_DEPRECATED(4.27, "Use SetWindProperties instead.")
		void SetWindFluidDensity(FRealSingle /*WindFluidDensity*/) {}

		void SetNumIterations(int32 InNumIterations) { NumIterations = InNumIterations; }
		int32 GetNumIterations() const { return NumIterations; }
		void SetMaxNumIterations(int32 InMaxNumIterations) { MaxNumIterations = InMaxNumIterations; }
		int32 GetMaxNumIterations() const { return MaxNumIterations; }
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
		void Update(FSolverReal InDeltaTime);

		// Return the actual of number of iterations used by the Evolution solver after the update (different from the number of iterations, depends on frame rate)
		int32 GetNumUsedIterations() const;

		FBoxSphereBounds CalculateBounds() const;
		// ---- End of the object management functions ----

		// ---- Cloth interface ----
		int32 AddParticles(int32 NumParticles, uint32 GroupId);
		void EnableParticles(int32 Offset, bool bEnable);

		void ResetStartPose(int32 Offset, int32 NumParticles);

		// Get the current solver time
		FSolverReal GetTime() const { return Time; }
		void SetParticleMassUniform(int32 Offset, FRealSingle UniformMass, FRealSingle MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate);
		void SetParticleMassFromTotalMass(int32 Offset, FRealSingle TotalMass, FRealSingle MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate);
		void SetParticleMassFromDensity(int32 Offset, FRealSingle Density, FRealSingle MinPerParticleMass, const FTriangleMesh& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate);

		// Set the amount of velocity allowed to filter from the given change in reference space transform, including local simulation space.
		void SetReferenceVelocityScale(uint32 GroupId,
			const FRigidTransform3& OldReferenceSpaceTransform,
			const FRigidTransform3& ReferenceSpaceTransform,
			const TVec3<FRealSingle>& LinearVelocityScale,
			FRealSingle AngularVelocityScale,
			FRealSingle FictitiousAngularScale);

		// Set general cloth simulation properties.
		void SetProperties(
			uint32 GroupId,
			FRealSingle DampingCoefficient,
			FRealSingle MotionDampingCoefficient,
			FRealSingle CollisionThickness,
			FRealSingle FrictionCoefficient);

		// Set whether to use continuous collision detection.
		void SetUseCCD(uint32 GroupId, bool bUseCCD);

		// Set per group gravity, used to override solver's gravity. Must be called during cloth update.
		void SetGravity(uint32 GroupId, const TVec3<FRealSingle>& Gravity);

		// Set per group wind velocity, used to override solver's wind velocity. Must be called during cloth update.
		void SetWindVelocity(uint32 GroupId, const TVec3<FRealSingle>& InWindVelocity);

		// Set the geometry affected by the wind.
		void SetWindGeometry(uint32 GroupId, const FTriangleMesh& TriangleMesh, const TConstArrayView<FRealSingle>& DragMultipliers, const TConstArrayView<FRealSingle>& LiftMultipliers);

		// Set the wind properties.
		void SetWindProperties(uint32 GroupId, const TVec2<FRealSingle>& Drag, const TVec2<FRealSingle>& Lift, FRealSingle AirDensity = 1.225e-6f);

		// Return the wind velocity field associated with a given group id.
		const Softs::FVelocityField& GetWindVelocityField(uint32 GroupId);

		// Add external forces to the particles
		void AddExternalForces(uint32 GroupId, bool bUseLegacyWind);

		const Softs::FSolverVec3* GetOldAnimationPositions(int32 Offset) const { return OldAnimationPositions.GetData() + Offset; }
		Softs::FSolverVec3* GetOldAnimationPositions(int32 Offset) { return OldAnimationPositions.GetData() + Offset; }
		const Softs::FSolverVec3* GetAnimationPositions(int32 Offset) const { return AnimationPositions.GetData() + Offset; }
		Softs::FSolverVec3* GetAnimationPositions(int32 Offset) { return AnimationPositions.GetData() + Offset; }
		const Softs::FSolverVec3* GetAnimationNormals(int32 Offset) const { return AnimationNormals.GetData() + Offset; }
		Softs::FSolverVec3* GetAnimationNormals(int32 Offset) { return AnimationNormals.GetData() + Offset; }
		const Softs::FSolverVec3* GetNormals(int32 Offset) const { return Normals.GetData() + Offset; }
		Softs::FSolverVec3* GetNormals(int32 Offset) { return Normals.GetData() + Offset; }
		const Softs::FPAndInvM* GetParticlePandInvMs(int32 Offset) const;
		Softs::FPAndInvM* GetParticlePandInvMs(int32 Offset);
		const Softs::FSolverVec3* GetParticleXs(int32 Offset) const;
		Softs::FSolverVec3* GetParticleXs(int32 Offset);
		const Softs::FSolverVec3* GetParticleVs(int32 Offset) const;
		Softs::FSolverVec3* GetParticleVs(int32 Offset);
		const Softs::FSolverReal* GetParticleInvMasses(int32 Offset) const;
		const FClothConstraints& GetClothConstraints(int32 Offset) const { return *ClothsConstraints.FindChecked(Offset); }
		FClothConstraints& GetClothConstraints(int32 Offset) { return *ClothsConstraints.FindChecked(Offset); }
		// ---- End of the Cloth interface ----

		// ---- Collider interface ----
		int32 AddCollisionParticles(int32 NumCollisionParticles, uint32 GroupId, int32 RecycledOffset = 0);
		void EnableCollisionParticles(int32 Offset, bool bEnable);

		void ResetCollisionStartPose(int32 Offset, int32 NumCollisionParticles);

		const int32* GetCollisionBoneIndices(int32 Offset) const { return CollisionBoneIndices.GetData() + Offset; }
		int32* GetCollisionBoneIndices(int32 Offset) { return CollisionBoneIndices.GetData() + Offset; }
		const Softs::FSolverRigidTransform3* GetCollisionBaseTransforms(int32 Offset) const { return CollisionBaseTransforms.GetData() + Offset; }
		Softs::FSolverRigidTransform3* GetCollisionBaseTransforms(int32 Offset) { return CollisionBaseTransforms.GetData() + Offset; }
		const Softs::FSolverRigidTransform3* GetOldCollisionTransforms(int32 Offset) const { return OldCollisionTransforms.GetData() + Offset; }
		Softs::FSolverRigidTransform3* GetOldCollisionTransforms(int32 Offset) { return OldCollisionTransforms.GetData() + Offset; }
		const Softs::FSolverRigidTransform3* GetCollisionTransforms(int32 Offset) const { return CollisionTransforms.GetData() + Offset; }
		Softs::FSolverRigidTransform3* GetCollisionTransforms(int32 Offset) { return CollisionTransforms.GetData() + Offset; }
		const Softs::FSolverVec3* GetCollisionParticleXs(int32 Offset) const;
		Softs::FSolverVec3* GetCollisionParticleXs(int32 Offset);
		const Softs::FSolverRotation3* GetCollisionParticleRs(int32 Offset) const;
		Softs::FSolverRotation3* GetCollisionParticleRs(int32 Offset);
		void SetCollisionGeometry(int32 Offset, int32 Index, TUniquePtr<FImplicitObject>&& Geometry);
		const TUniquePtr<FImplicitObject>* GetCollisionGeometries(int32 Offset) const;
		const bool* GetCollisionStatus(int32 Offset) const;
		const TArray<Softs::FSolverVec3>& GetCollisionContacts() const;
		const TArray<Softs::FSolverVec3>& GetCollisionNormals() const;
		// ---- End of the Collider interface ----

		// ---- Field interface ----
		FPerSolverFieldSystem& GetPerSolverField() { return PerSolverField; }
		const FPerSolverFieldSystem& GetPerSolverField() const { return PerSolverField; }
		// ---- End of the Field interface ----

	private:
		void ResetParticles();
		void ResetCollisionParticles(int32 InCollisionParticlesOffset = 0);
		void ApplyPreSimulationTransforms();
		Softs::FSolverReal SetParticleMassPerArea(int32 Offset, int32 Size, const FTriangleMesh& Mesh);
		void ParticleMassUpdateDensity(const FTriangleMesh& Mesh, Softs::FSolverReal Density);
		void ParticleMassClampAndEnslave(int32 Offset, int32 Size, Softs::FSolverReal MinPerParticleMass, const TFunctionRef<bool(int32)>& KinematicPredicate);

		// Update the solver field forces/velocities at the particles location
		void UpdateSolverField();

	private:
		TUniquePtr<Softs::FPBDEvolution> Evolution;

		// Object arrays
		TArray<FClothingSimulationCloth*> Cloths;

		// Simulation group attributes
		TArrayCollectionArray<Softs::FSolverRigidTransform3> PreSimulationTransforms;  // Allow a different frame of reference for each cloth groups
		TArrayCollectionArray<Softs::FSolverVec3> FictitiousAngularDisplacements;  // Relative angular displacement of the reference bone that depends on the fictitious angular scale factor

		// Particle attributes
		TArrayCollectionArray<Softs::FSolverVec3> Normals;
		TArrayCollectionArray<Softs::FSolverVec3> OldAnimationPositions;
		TArrayCollectionArray<Softs::FSolverVec3> AnimationPositions;
		TArrayCollectionArray<Softs::FSolverVec3> AnimationNormals;

		// Collision particle attributes
		TArrayCollectionArray<int32> CollisionBoneIndices;
		TArrayCollectionArray<Softs::FSolverRigidTransform3> CollisionBaseTransforms;
		TArrayCollectionArray<Softs::FSolverRigidTransform3> OldCollisionTransforms;
		TArrayCollectionArray<Softs::FSolverRigidTransform3> CollisionTransforms;

		// Cloth constraints
		TMap<int32, TUniquePtr<FClothConstraints>> ClothsConstraints;

		// Local space simulation
		FVec3 OldLocalSpaceLocation;  // This is used to translate between world space and simulation space,
		FVec3 LocalSpaceLocation;     // add this to simulation space coordinates to get world space coordinates, must keep FReal as underlying type for LWC
		FRotation3 LocalSpaceRotation;

		// Time stepping
		FSolverReal Time;
		FSolverReal DeltaTime;
		int32 NumIterations;
		int32 MaxNumIterations;
		int32 NumSubsteps;

		// Solver colliders offset
		int32 CollisionParticlesOffset;  // Collision particle offset on the first solver/non cloth collider
		int32 CollisionParticlesSize;  // Number of solver only colliders

		// Solver parameters
		TVec3<FRealSingle> Gravity;
		TVec3<FRealSingle> WindVelocity;
		FRealSingle LegacyWindAdaption;
		bool bIsClothGravityOverrideEnabled;

		// Field system unique to the cloth solver
		FPerSolverFieldSystem PerSolverField;
	};

} // namespace Chaos

// Support ISPC enable/disable in non-shipping builds
constexpr bool bChaos_CalculateBounds_ISPC_Enable = true;
#if !INTEL_ISPC
const bool bChaos_PreSimulationTransforms_ISPC_Enabled = false;
const bool bChaos_CalculateBounds_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_PreSimulationTransforms_ISPC_Enabled = true;
const bool bChaos_CalculateBounds_ISPC_Enabled = bChaos_CalculateBounds_ISPC_Enable;
#else
extern bool bChaos_PreSimulationTransforms_ISPC_Enabled;
extern bool bChaos_CalculateBounds_ISPC_Enabled;
#endif
