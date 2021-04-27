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

class CHAOS_API FPBDEvolution : public TArrayCollection
{
 public:
	using FGravityForces = FPerParticleGravity;

	// TODO(mlentine): Init particles from some type of input
	FPBDEvolution(FPBDParticles&& InParticles, FKinematicGeometryClothParticles&& InGeometryParticles, TArray<TVec3<int32>>&& CollisionTriangles, int32 NumIterations = 1, FReal CollisionThickness = 0, FReal SelfCollisionsThickness = 0, FReal CoefficientOfFriction = 0, FReal Damping = 0.04);
	~FPBDEvolution() {}

	void AdvanceOneTimeStep(const FReal dt);

	// Remove all particles, will also reset all rules
	void ResetParticles();

	// Add particles and initialize group ids. Return the index of the first added particle.
	int32 AddParticleRange(int32 NumParticles, uint32 GroupId, bool bActivate);

	// Return the number of particles of the block starting at Offset
	int32 GetParticleRangeSize(int32 Offset) const { return MParticlesActiveView.GetRangeSize(Offset); }

	// Set a block of particles active or inactive, using the index of the first added particle to identify the block.
	void ActivateParticleRange(int32 Offset, bool bActivate)  { MParticlesActiveView.ActivateRange(Offset, bActivate); }

	// Particles accessors
	const FPBDParticles& Particles() const { return MParticles; }
	FPBDParticles& Particles() { return MParticles; }
	const TPBDActiveView<FPBDParticles>& ParticlesActiveView() { return MParticlesActiveView; }

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
	const FKinematicGeometryClothParticles& CollisionParticles() const { return MCollisionParticles; }
	FKinematicGeometryClothParticles& CollisionParticles() { return MCollisionParticles; }
	const TArray<uint32>& CollisionParticleGroupIds() const { return MCollisionParticleGroupIds; }
	const TPBDActiveView<FKinematicGeometryClothParticles>& CollisionParticlesActiveView() { return MCollisionParticlesActiveView; }

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
	const TArray<TFunction<void(const FPBDParticles&, const FReal)>>& ConstraintInits() const { return MConstraintInits; }
	TArray<TFunction<void(const FPBDParticles&, const FReal)>>& ConstraintInits() { return MConstraintInits; }
	const TArray<TFunction<void(FPBDParticles&, const FReal)>>& ConstraintRules() const { return MConstraintRules; }
	TArray<TFunction<void(FPBDParticles&, const FReal)>>& ConstraintRules() { return MConstraintRules; }
	
	void SetKinematicUpdateFunction(TFunction<void(FPBDParticles&, const FReal, const FReal, const int32)> KinematicUpdate) { MKinematicUpdate = KinematicUpdate; }
	void SetCollisionKinematicUpdateFunction(TFunction<void(FKinematicGeometryClothParticles&, const FReal, const FReal, const int32)> KinematicUpdate) { MCollisionKinematicUpdate = KinematicUpdate; }

	TFunction<void(FPBDParticles&, const FReal, const int32)>& GetForceFunction(const uint32 GroupId = 0) { return MGroupForceRules[GroupId]; }
	const TFunction<void(FPBDParticles&, const FReal, const int32)>& GetForceFunction(const uint32 GroupId = 0) const { return MGroupForceRules[GroupId]; }

	FGravityForces& GetGravityForces(const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); return MGroupGravityForces[GroupId]; }
	const FGravityForces& GetGravityForces(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupGravityForces[GroupId]; }

	FVelocityField& GetVelocityField(const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); return MGroupVelocityFields[GroupId]; }
	const FVelocityField& GetVelocityField(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupVelocityFields[GroupId]; }

	void ResetSelfCollision() { MCollisionTriangles.Reset(); MDisabledCollisionElements.Reset(); };
	TArray<TVector<int32, 3>>& CollisionTriangles() { return MCollisionTriangles; }
	TSet<TVector<int32, 2>>& DisabledCollisionElements() { return MDisabledCollisionElements; }

	int32 GetIterations() const { return MNumIterations; }
	void SetIterations(const int32 Iterations) { MNumIterations = Iterations; }

	FReal GetSelfCollisionThickness(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupSelfCollisionThicknesses[GroupId]; }
	void SetSelfCollisionThickness(const FReal SelfCollisionThickness, const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); MGroupSelfCollisionThicknesses[GroupId] = SelfCollisionThickness; }

	FReal GetCollisionThickness(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupCollisionThicknesses[GroupId]; }
	void SetCollisionThickness(const FReal CollisionThickness, const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); MGroupCollisionThicknesses[GroupId] = CollisionThickness; }

	FReal GetCoefficientOfFriction(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupCoefficientOfFrictions[GroupId]; }
	void SetCoefficientOfFriction(const FReal CoefficientOfFriction, const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); MGroupCoefficientOfFrictions[GroupId] = CoefficientOfFriction; }

	FReal GetDamping(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupDampings[GroupId]; }
	void SetDamping(const FReal Damping, const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); MGroupDampings[GroupId] = Damping; }

	bool GetUseCCD(const uint32 GroupId = 0) const { check(GroupId < TArrayCollection::Size()); return MGroupUseCCDs[GroupId]; }
	void SetUseCCD(const bool bUseCCD, const uint32 GroupId = 0) { check(GroupId < TArrayCollection::Size()); MGroupUseCCDs[GroupId] = bUseCCD; }

	UE_DEPRECATED(4.27, "Use GetCollisionStatus() instead")
	const bool Collided(int32 index) { return MCollided[index]; }

	const TArray<bool>& GetCollisionStatus() { return MCollided; }
	const TArray<FVec3>& GetCollisionContacts() const { return MCollisionContacts; }
	const TArray<FVec3>& GetCollisionNormals() const { return MCollisionNormals; }

	FReal GetTime() const { return MTime; }

 private:
	// Add simulation groups and set default values
	void AddGroups(int32 NumGroups);
	// Reset simulation groups
	void ResetGroups();
	// Selected versions of the pre-iteration updates (euler step, force, velocity field. damping updates)..
	template<bool bForceRule, bool bVelocityField, bool bDampVelocityRule>
	void PreIterationUpdate(const FReal Dt, const int32 Offset, const int32 Range, const int32 MinParallelBatchSize);

private:
	FPBDParticles MParticles;
	TPBDActiveView<FPBDParticles> MParticlesActiveView;
	FKinematicGeometryClothParticles MCollisionParticles;
	TPBDActiveView<FKinematicGeometryClothParticles> MCollisionParticlesActiveView;

	TArray<TVector<int32, 3>> MCollisionTriangles;       // Used for self-collisions
	TSet<TVector<int32, 2>> MDisabledCollisionElements;  // 

	TArrayCollectionArray<FRigidTransform3> MCollisionTransforms;  // Used for CCD to store the initial state before the kinematic update
	TArrayCollectionArray<bool> MCollided;
	TArrayCollectionArray<uint32> MCollisionParticleGroupIds;  // Used for per group parameters for collision particles
	TArrayCollectionArray<uint32> MParticleGroupIds;  // Used for per group parameters for particles
	TArray<FVec3> MCollisionContacts;
	TArray<FVec3> MCollisionNormals;

	TArrayCollectionArray<FGravityForces> MGroupGravityForces;
	TArrayCollectionArray<FVelocityField> MGroupVelocityFields;
	TArrayCollectionArray<TFunction<void(FPBDParticles&, const FReal, const int32)>> MGroupForceRules;
	TArrayCollectionArray<FReal> MGroupCollisionThicknesses;
	TArrayCollectionArray<FReal> MGroupSelfCollisionThicknesses;
	TArrayCollectionArray<FReal> MGroupCoefficientOfFrictions;
	TArrayCollectionArray<FReal> MGroupDampings;
	TArrayCollectionArray<bool> MGroupUseCCDs;
	
	TArray<TFunction<void(const FPBDParticles&, const FReal)>> MConstraintInits;
	TPBDActiveView<TArray<TFunction<void(const FPBDParticles&, const FReal)>>> MConstraintInitsActiveView;
	TArray<TFunction<void(FPBDParticles&, const FReal)>> MConstraintRules;
	TPBDActiveView<TArray<TFunction<void(FPBDParticles&, const FReal)>>> MConstraintRulesActiveView;

	TFunction<void(FPBDParticles&, const FReal, const FReal, const int32)> MKinematicUpdate;
	TFunction<void(FKinematicGeometryClothParticles&, const FReal, const FReal, const int32)> MCollisionKinematicUpdate;

	int32 MNumIterations;
	FVec3 MGravity;
	FReal MCollisionThickness;
	FReal MSelfCollisionThickness;
	FReal MCoefficientOfFriction;
	FReal MDamping;
	FReal MTime;
};
}
