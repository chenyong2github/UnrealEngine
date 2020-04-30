// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/KinematicGeometryParticles.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/VelocityField.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/Vector.h"

namespace Chaos
{
template<class T, int d>
class CHAOS_API TPBDEvolution  : private TArrayCollection
{
  public:
	using FGravityForces = TPerParticleGravity<T, d>;
	using FVelocityField = TVelocityField<T, d>;

	// TODO(mlentine): Init particles from some type of input
	TPBDEvolution(TPBDParticles<T, d>&& InParticles, TKinematicGeometryClothParticles<T, d>&& InGeometryParticles, TArray<TVector<int32, 3>>&& CollisionTriangles, int32 NumIterations = 1, T CollisionThickness = 0, T SelfCollisionsThickness = 0, T CoefficientOfFriction = 0, T Damping = 0.04);
	~TPBDEvolution() {}

	void AdvanceOneTimeStep(const T dt);

	void SetKinematicUpdateFunction(TFunction<void(TPBDParticles<T, d>&, const T, const T, const int32)> KinematicUpdate) { MKinematicUpdate = KinematicUpdate; }
	void SetCollisionKinematicUpdateFunction(TFunction<void(TKinematicGeometryClothParticles<T, d>&, const T, const T, const int32)> KinematicUpdate) { MCollisionKinematicUpdate = KinematicUpdate; }
	void SetParticleUpdateFunction(TFunction<void(TPBDParticles<T, d>&, const T)> ParticleUpdate) { MParticleUpdate = ParticleUpdate; }
	void AddPBDConstraintFunction(TFunction<void(TPBDParticles<T, d>&, const T)> ConstraintFunction) { MConstraintRules.Add(ConstraintFunction); }
	void AddXPBDConstraintFunctions(TFunction<void()> InitConstraintFunction, TFunction<void(TPBDParticles<T, d>&, const T)> ConstraintFunction) { MInitConstraintRules.Add(InitConstraintFunction); MConstraintRules.Add(ConstraintFunction); }
	void AddForceFunction(TFunction<void(TPBDParticles<T, d>&, const T, const int32)> ForceFunction) { MForceRules.Add(ForceFunction); }

	// Add particles and initialize group ids. Return the index of the first added particle.
	uint32 AddParticles(uint32 Num, uint32 GroupId = 0);
	// Add collision particles and initialize group ids. Return the index of the first added particle.
	uint32 AddCollisionParticles(uint32 Num, uint32 GroupId = 0);

	const TPBDParticles<T, d>& Particles() const { return MParticles; }
	TPBDParticles<T, d>& Particles() { return MParticles; }

	FGravityForces& GetGravityForces(const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); return MGroupGravityForces[GroupId]; }
	const FGravityForces& GetGravityForces(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupGravityForces[GroupId]; }

	TUniquePtr<FVelocityField>& GetVelocityField(const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); return MGroupVelocityFields[GroupId]; }
	const TUniquePtr<FVelocityField>& GetVelocityField(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupVelocityFields[GroupId]; }

	const TGeometryClothParticles<T, d>& CollisionParticles() const { return MCollisionParticles; }
	TGeometryClothParticles<T, d>& CollisionParticles() { return MCollisionParticles; }
	const bool Collided(int32 index) { return MCollided[index]; }

	TArray<TVector<int32, 3>>& CollisionTriangles() { return MCollisionTriangles; }
	TSet<TVector<int32, 2>>& DisabledCollisionElements() { return MDisabledCollisionElements; }

	int32 GetIterations() const { return MNumIterations; }
	void SetIterations(const int32 Iterations) { MNumIterations = Iterations; }

	T GetSelfCollisionThickness(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupSelfCollisionThicknesses[GroupId]; }
	void SetSelfCollisionThickness(const T SelfCollisionThickness, const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); MGroupSelfCollisionThicknesses[GroupId] = SelfCollisionThickness; }

	T GetCollisionThickness(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupCollisionThicknesses[GroupId]; }
	void SetCollisionThickness(const T CollisionThickness, const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); MGroupCollisionThicknesses[GroupId] = CollisionThickness; }

	T GetCoefficientOfFriction(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupCoefficientOfFrictions[GroupId]; }
	void SetCoefficientOfFriction(const T CoefficientOfFriction, const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); MGroupCoefficientOfFrictions[GroupId] = CoefficientOfFriction; }

	T GetDamping(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupDampings[GroupId]; }
	void SetDamping(const T Damping, const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); MGroupDampings[GroupId] = Damping; }

	T GetTime() const { return MTime; }

	void ResetConstraintRules() { MInitConstraintRules.Reset(); MConstraintRules.Reset(); };
	void ResetSelfCollision() { MCollisionTriangles.Reset(); MDisabledCollisionElements.Reset(); };

	const TArrayCollectionArray<uint32>& ParticleGroupIds() const { return MParticleGroupIds; }

  private:
	// Add simulation groups and set default values
	void AddGroups(int32 Num);

	TPBDParticles<T, d> MParticles;
	TKinematicGeometryClothParticles<T, d> MCollisionParticles;
	TArray<TVector<int32, 3>> MCollisionTriangles;       // Used for self-collisions
	TSet<TVector<int32, 2>> MDisabledCollisionElements;  // 
	TArrayCollectionArray<bool> MCollided;
	TArrayCollectionArray<uint32> MCollisionParticleGroupIds;  // Used for per group parameters for collision particles
	TArrayCollectionArray<uint32> MParticleGroupIds;  // Used for per group parameters for particles
	TArrayCollectionArray<FGravityForces> MGroupGravityForces;
	TArrayCollectionArray<T> MGroupCollisionThicknesses;
	TArrayCollectionArray<T> MGroupSelfCollisionThicknesses;
	TArrayCollectionArray<T> MGroupCoefficientOfFrictions;
	TArrayCollectionArray<T> MGroupDampings;
	TArrayCollectionArray<TUniquePtr<FVelocityField>> MGroupVelocityFields;
	int32 MNumIterations;
	TVector<T, d> MGravity;
	T MCollisionThickness;
	T MSelfCollisionThickness;
	T MCoefficientOfFriction;
	T MDamping;
	T MTime;

	TArray<TFunction<void(TPBDParticles<T, d>&, const T, const int32)>> MForceRules;
	TArray<TFunction<void()>> MInitConstraintRules;
	TArray<TFunction<void(TPBDParticles<T, d>&, const T)>> MConstraintRules;
	TFunction<void(TPBDParticles<T, d>&, const T)> MParticleUpdate;
	TFunction<void(TPBDParticles<T, d>&, const T, const T, const int32)> MKinematicUpdate;
	TFunction<void(TKinematicGeometryClothParticles<T, d>&, const T, const T, const int32)> MCollisionKinematicUpdate;
};
}
