// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ChaosCloth/ChaosClothConstraints.h"
#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Transform.h"
#include "Chaos/ImplicitObject.h"
#include "HAL/CriticalSection.h"

class USkeletalMeshComponent;
class UClothingAssetCommon;

namespace Chaos
{
	template<typename T, int d> class TPBDEvolution;
	template<typename T> class TTriangleMesh;
	template<typename T, int d> class TVelocityField;

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
		void SetLocalSpaceLocation(const TVector<float, 3>& InLocalSpaceLocation, bool bReset = false);
		const TVector<float, 3>& GetLocalSpaceLocation() const { return LocalSpaceLocation; }

		// Disables all Cloths gravity override mechanism
		void EnableClothGravityOverride(bool bInIsClothGravityOverrideEnabled) { bIsClothGravityOverrideEnabled = bInIsClothGravityOverrideEnabled; }
		bool IsClothGravityOverrideEnabled() const { return bIsClothGravityOverrideEnabled; }
		void SetGravity(const TVector<float, 3>& InGravity) { Gravity = InGravity; }
		const TVector<float, 3>& GetGravity() const { return Gravity; }

		void SetWindVelocity(const TVector<float, 3>& InWindVelocity, float InLegacyWindAdaption = 0.f);
		void SetWindFluidDensity(float InWindFluidDensity) { WindFluidDensity = InWindFluidDensity; }

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

		// Update solver properties before simulation
		void Update(float InDeltaTime);

		FBoxSphereBounds CalculateBounds() const;
		// ---- End of the object management functions ----

		// ---- Cloth interface ----
		int32 AddParticles(int32 NumParticles, uint32 GroupId);
		void EnableParticles(int32 Offset, bool bEnable);

		void SetParticleMassUniform(int32 Offset, float UniformMass, float MinPerParticleMass, const TTriangleMesh<float>& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate);
		void SetParticleMassFromTotalMass(int32 Offset, float TotalMass, float MinPerParticleMass, const TTriangleMesh<float>& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate);
		void SetParticleMassFromDensity(int32 Offset, float Density, float MinPerParticleMass, const TTriangleMesh<float>& Mesh, const TFunctionRef<bool(int32)>& KinematicPredicate);

		// Set the amount of velocity allowed to filter from the given change in reference space transform, including local simulation space
		void SetReferenceVelocityScale(uint32 GroupId,
			const TRigidTransform<float, 3>& OldReferenceSpaceTransform,
			const TRigidTransform<float, 3>& ReferenceSpaceTransform,
			const TVector<float, 3>& LinearVelocityScale,
			float AngularVelocityScale);

		// Set general cloth simulation properties
		void SetProperties(uint32 GroupId, float DampingCoefficient, float CollisionThickness, float FrictionCoefficient);

		// Set per group gravity, used to override solver's gravity. Must be called during cloth update.
		void SetGravity(uint32 GroupId, const TVector<float, 3>& Gravity);

		// Set the geometry affected by wind, or disable if TriangleMesh is null.
		void SetWindVelocityField(uint32 GroupId, float DragCoefficient, float LiftCoefficient, const TTriangleMesh<float>* TriangleMesh = nullptr);
		const TVelocityField<float, 3>&  GetWindVelocityField(uint32 GroupId);

		// Set legacy noise wind.
		void SetLegacyWind(uint32 GroupId, bool bUseLegacyWind);

		const TVector<float, 3>* GetOldAnimationPositions(int32 Offset) const { return OldAnimationPositions.GetData() + Offset; }
		TVector<float, 3>* GetOldAnimationPositions(int32 Offset) { return OldAnimationPositions.GetData() + Offset; }
		const TVector<float, 3>* GetAnimationPositions(int32 Offset) const { return AnimationPositions.GetData() + Offset; }
		TVector<float, 3>* GetAnimationPositions(int32 Offset) { return AnimationPositions.GetData() + Offset; }
		const TVector<float, 3>* GetAnimationNormals(int32 Offset) const { return AnimationNormals.GetData() + Offset; }
		TVector<float, 3>* GetAnimationNormals(int32 Offset) { return AnimationNormals.GetData() + Offset; }
		const TVector<float, 3>* GetNormals(int32 Offset) const { return Normals.GetData() + Offset; }
		TVector<float, 3>* GetNormals(int32 Offset) { return Normals.GetData() + Offset; }
		const TVector<float, 3>* GetParticlePs(int32 Offset) const;
		TVector<float, 3>* GetParticlePs(int32 Offset);
		const TVector<float, 3>* GetParticleXs(int32 Offset) const;
		TVector<float, 3>* GetParticleXs(int32 Offset);
		const TVector<float, 3>* GetParticleVs(int32 Offset) const;
		TVector<float, 3>* GetParticleVs(int32 Offset);
		const float* GetParticleInvMasses(int32 Offset) const;
		const FClothConstraints& GetClothConstraints(int32 Offset) const { return *ClothsConstraints.FindChecked(Offset); }
		FClothConstraints& GetClothConstraints(int32 Offset) { return *ClothsConstraints.FindChecked(Offset); }
		// ---- End of the Cloth interface ----

		// ---- Collider interface ----
		int32 AddCollisionParticles(int32 NumCollisionParticles, uint32 GroupId, int32 RecycledOffset = 0);
		void EnableCollisionParticles(int32 Offset, bool bEnable);

		const int32* GetCollisionBoneIndices(int32 Offset) const { return CollisionBoneIndices.GetData() + Offset; }
		int32* GetCollisionBoneIndices(int32 Offset) { return CollisionBoneIndices.GetData() + Offset; }
		const TRigidTransform<float, 3>* GetCollisionBaseTransforms(int32 Offset) const { return CollisionBaseTransforms.GetData() + Offset; }
		TRigidTransform<float, 3>* GetCollisionBaseTransforms(int32 Offset) { return CollisionBaseTransforms.GetData() + Offset; }
		const TRigidTransform<float, 3>* GetOldCollisionTransforms(int32 Offset) const { return OldCollisionTransforms.GetData() + Offset; }
		TRigidTransform<float, 3>* GetOldCollisionTransforms(int32 Offset) { return OldCollisionTransforms.GetData() + Offset; }
		const TRigidTransform<float, 3>* GetCollisionTransforms(int32 Offset) const { return CollisionTransforms.GetData() + Offset; }
		TRigidTransform<float, 3>* GetCollisionTransforms(int32 Offset) { return CollisionTransforms.GetData() + Offset; }
		const TVector<float, 3>* GetCollisionParticleXs(int32 Offset) const;
		TVector<float, 3>* GetCollisionParticleXs(int32 Offset);
		const TRotation<float, 3>* GetCollisionParticleRs(int32 Offset) const;
		TRotation<float, 3>* GetCollisionParticleRs(int32 Offset);
		void SetCollisionGeometry(int32 Offset, int32 Index, TUniquePtr<FImplicitObject>&& Geometry);
		const TUniquePtr<FImplicitObject>* GetCollisionGeometries(int32 Offset) const;
		// ---- End of the Collider interface ----

	private:
		void ResetParticles();
		void ResetCollisionParticles(int32 InCollisionParticlesOffset = 0);
		void ApplyPreSimulationTransforms();
		float SetParticleMassPerArea(int32 Offset, int32 Size, const TTriangleMesh<float>& Mesh);
		void ParticleMassUpdateDensity(const TTriangleMesh<float>& Mesh, float Density);
		void ParticleMassClampAndEnslave(int32 Offset, int32 Size, float MinPerParticleMass, const TFunctionRef<bool(int32)>& KinematicPredicate);

	private:
		TUniquePtr<TPBDEvolution<float, 3>> Evolution;

		// Object arrays
		TArray<FClothingSimulationCloth*> Cloths;

		// Simulation group attributes
		TArrayCollectionArray<TRigidTransform<float, 3>> PreSimulationTransforms;  // Allow a different frame of reference for each cloth groups

		// Particle attributes
		TArrayCollectionArray<TVector<float, 3>> Normals;
		TArrayCollectionArray<TVector<float, 3>> OldAnimationPositions;
		TArrayCollectionArray<TVector<float, 3>> AnimationPositions;
		TArrayCollectionArray<TVector<float, 3>> AnimationNormals;

		// Collision particle attributes
		TArrayCollectionArray<int32> CollisionBoneIndices;
		TArrayCollectionArray<TRigidTransform<float, 3>> CollisionBaseTransforms;
		TArrayCollectionArray<TRigidTransform<float, 3>> OldCollisionTransforms;
		TArrayCollectionArray<TRigidTransform<float, 3>> CollisionTransforms;

		// Cloth constraints
		TMap<int32, TUniquePtr<FClothConstraints>> ClothsConstraints;

		// Local space simulation
		TVector<float, 3> OldLocalSpaceLocation;
		TVector<float, 3> LocalSpaceLocation;  // This is used to translate between world space and simulation space. Add this to simulation space coordinates to get world space coordinates

		// Mutex
		FCriticalSection AddCollisionParticlesMutex;

		// Time stepping
		float Time;
		float DeltaTime;
		int32 NumIterations;
		int32 NumSubsteps;

		// Solver colliders offset
		int32 CollisionParticlesOffset;  // Collision particle offset on the first solver/non cloth collider
		int32 CollisionParticlesSize;  // Number of solver only colliders

		// Solver parameters
		TVector<float, 3> Gravity;
		TVector<float, 3> WindVelocity;
		float LegacyWindAdaption;
		float WindFluidDensity;
		bool bIsClothGravityOverrideEnabled;
	};

} // namespace Chaos
