// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/KinematicGeometryParticles.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/VelocityField.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDActiveView.h"
#include "Chaos/Vector.h"

namespace Chaos
{
template<class T, int d>
class CHAOS_API TPBDEvolution : public TArrayCollection
{
 public:
	using FGravityForces = TPerParticleGravity<T, d>;
	using FVelocityField = TVelocityField<T, d>;

	// TODO(mlentine): Init particles from some type of input
	TPBDEvolution(TPBDParticles<T, d>&& InParticles, TKinematicGeometryClothParticles<T, d>&& InGeometryParticles, TArray<TVector<int32, 3>>&& CollisionTriangles, int32 NumIterations = 1, T CollisionThickness = 0, T SelfCollisionsThickness = 0, T CoefficientOfFriction = 0, T Damping = 0.04);
	~TPBDEvolution() {}

	void AdvanceOneTimeStep(const T dt);

	// Remove all particles, will also reset all rules
	void ResetParticles();

	// Add particles and initialize group ids. Return the index of the first added particle.
	int32 AddParticleRange(int32 NumParticles, uint32 GroupId, bool bActivate);

	// Return the number of particles of the block starting at Offset
	int32 GetParticleRangeSize(int32 Offset) const { return MParticlesActiveView.GetRangeSize(Offset); }

	// Set a block of particles active or inactive, using the index of the first added particle to identify the block.
	void ActivateParticleRange(int32 Offset, bool bActivate)  { MParticlesActiveView.ActivateRange(Offset, bActivate); }

	// Particles accessors
	const TPBDParticles<T, d>& Particles() const { return MParticles; }
	TPBDParticles<T, d>& Particles() { return MParticles; }
	const TPBDActiveView<TPBDParticles<T, d>>& ParticlesActiveView() { return MParticlesActiveView; }

	const TArray<uint32>& ParticleGroupIds() const { return MParticleGroupIds; }

	// Remove all collision particles
	void ResetCollisionParticles(int32 NumParticles = 0);

	// Add collision particles and initialize group ids. Return the index of the first added particle.
	// Use INDEX_NONE as GroupId for collision particles that affect all particle groups.
	int32 AddCollisionParticleRange(int32 NumParticles, uint32 GroupId, bool bActivate);

	// Set a block of collision particles active or inactive, using the index of the first added particle to identify the block.
	void ActivateCollisionParticleRange(int32 Offset, bool bActivate) { MCollisionParticlesActiveView.ActivateRange(Offset, bActivate); }

	// Return the number of particles of the block starting at Offset
	int32 GetCollisionParticleRangeSize(int32 Offset) const { return MCollisionParticlesActiveView.GetRangeSize(Offset); }

	// Collision particles accessors
	const TKinematicGeometryClothParticles<T, d>& CollisionParticles() const { return MCollisionParticles; }
	TKinematicGeometryClothParticles<T, d>& CollisionParticles() { return MCollisionParticles; }
	const TArray<uint32>& CollisionParticleGroupIds() const { return MCollisionParticleGroupIds; }
	const TPBDActiveView<TKinematicGeometryClothParticles<T, d>>& CollisionParticlesActiveView() { return MCollisionParticlesActiveView; }

	// Reset all constraint init and rule functions.
	void ResetConstraintRules() { MConstraintInits.Reset(); MConstraintRules.Reset(); MConstraintInitsActiveView.Reset(); MConstraintRulesActiveView.Reset();  };

	// Add constraints. Return the index of the first added constraint.
	int32 AddConstraintInitRange(int32 NumConstraints, bool bActivate);
	int32 AddConstraintRuleRange(int32 NumConstraints, bool bActivate);
	
	// Return the number of particles of the block starting at Offset
	int32 GetConstraintInitRangeSize(int32 Offset) const { return MConstraintInitsActiveView.GetRangeSize(Offset); }
	int32 GetConstraintRuleRangeSize(int32 Offset) const { return MConstraintRulesActiveView.GetRangeSize(Offset); }

	// Set a block of constraints active or inactive, using the index of the first added particle to identify the block.
	void ActivateConstraintInitRange(int32 Offset, bool bActivate) { MConstraintInitsActiveView.ActivateRange(Offset, bActivate); }
	void ActivateConstraintRuleRange(int32 Offset, bool bActivate) { MConstraintRulesActiveView.ActivateRange(Offset, bActivate); }

	// Constraint accessors
	const TArray<TFunction<void()>>& ConstraintInits() const { return MConstraintInits; }
	TArray<TFunction<void()>>& ConstraintInits() { return MConstraintInits; }
	const TArray<TFunction<void(TPBDParticles<T, d>&, const T)>>& ConstraintRules() const { return MConstraintRules; }
	TArray<TFunction<void(TPBDParticles<T, d>&, const T)>>& ConstraintRules() { return MConstraintRules; }
	
	void SetKinematicUpdateFunction(TFunction<void(TPBDParticles<T, d>&, const T, const T, const int32)> KinematicUpdate) { MKinematicUpdate = KinematicUpdate; }
	void SetCollisionKinematicUpdateFunction(TFunction<void(TKinematicGeometryClothParticles<T, d>&, const T, const T, const int32)> KinematicUpdate) { MCollisionKinematicUpdate = KinematicUpdate; }

	TFunction<void(TPBDParticles<T, d>&, const T, const int32)>& GetForceFunction(const uint32 GroupId = 0) { return MGroupForceRules[GroupId]; }
	const TFunction<void(TPBDParticles<T, d>&, const T, const int32)>& GetForceFunction(const uint32 GroupId = 0) const { return MGroupForceRules[GroupId]; }

	FGravityForces& GetGravityForces(const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); return MGroupGravityForces[GroupId]; }
	const FGravityForces& GetGravityForces(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupGravityForces[GroupId]; }

	FVelocityField& GetVelocityField(const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); return MGroupVelocityFields[GroupId]; }
	const FVelocityField& GetVelocityField(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupVelocityFields[GroupId]; }

	const bool Collided(int32 index) { return MCollided[index]; }

	void ResetSelfCollision() { MCollisionTriangles.Reset(); MDisabledCollisionElements.Reset(); };
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

 private:
	// Add simulation groups and set default values
	void AddGroups(int32 NumGroups);
	// Reset simulation groups
	void ResetGroups();
 
private:
	TPBDParticles<T, d> MParticles;
	TPBDActiveView<TPBDParticles<T, d>> MParticlesActiveView;
	TKinematicGeometryClothParticles<T, d> MCollisionParticles;
	TPBDActiveView<TKinematicGeometryClothParticles<T, d>> MCollisionParticlesActiveView;

	TArray<TVector<int32, 3>> MCollisionTriangles;       // Used for self-collisions
	TSet<TVector<int32, 2>> MDisabledCollisionElements;  // 

	TArrayCollectionArray<bool> MCollided;
	TArrayCollectionArray<uint32> MCollisionParticleGroupIds;  // Used for per group parameters for collision particles
	TArrayCollectionArray<uint32> MParticleGroupIds;  // Used for per group parameters for particles

	TArrayCollectionArray<FGravityForces> MGroupGravityForces;
	TArrayCollectionArray<FVelocityField> MGroupVelocityFields;
	TArrayCollectionArray<TFunction<void(TPBDParticles<T, d>&, const T, const int32)>> MGroupForceRules;
	TArrayCollectionArray<T> MGroupCollisionThicknesses;
	TArrayCollectionArray<T> MGroupSelfCollisionThicknesses;
	TArrayCollectionArray<T> MGroupCoefficientOfFrictions;
	TArrayCollectionArray<T> MGroupDampings;
	TArrayCollectionArray<TVector<T, d>> MGroupCenterOfMass;
	TArrayCollectionArray<TVector<T, d>> MGroupVelocity;
	TArrayCollectionArray<TVector<T, d>> MGroupAngularVelocity;
	
	TArray<TFunction<void()>> MConstraintInits;
	TPBDActiveView<TArray<TFunction<void()>>> MConstraintInitsActiveView;
	TArray<TFunction<void(TPBDParticles<T, d>&, const T)>> MConstraintRules;
	TPBDActiveView<TArray<TFunction<void(TPBDParticles<T, d>&, const T)>>> MConstraintRulesActiveView;

	TFunction<void(TPBDActiveView<TPBDParticles<T, d>>&, const T)> MParticleUpdate;
	TFunction<void(TPBDParticles<T, d>&, const T, const T, const int32)> MKinematicUpdate;
	TFunction<void(TKinematicGeometryClothParticles<T, d>&, const T, const T, const int32)> MCollisionKinematicUpdate;

	int32 MNumIterations;
	TVector<T, d> MGravity;
	T MCollisionThickness;
	T MSelfCollisionThickness;
	T MCoefficientOfFriction;
	T MDamping;
	T MTime;
};
}
