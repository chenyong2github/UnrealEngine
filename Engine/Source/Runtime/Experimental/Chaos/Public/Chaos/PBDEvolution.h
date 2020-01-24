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
class CHAOS_API TPBDEvolution
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

	const TPBDParticles<T, d>& Particles() const { return MParticles; }
	TPBDParticles<T, d>& Particles() { return MParticles; }

	FGravityForces& GetGravityForces() { return GravityForces; }
	const FGravityForces& GetGravityForces() const { return GravityForces; }

	TArray<FVelocityField>& GetVelocityFields() { return VelocityFields; }
	const TArray<FVelocityField>& GetVelocityFields() const { return VelocityFields; }

	const TGeometryClothParticles<T, d>& CollisionParticles() const { return MCollisionParticles; }
	TGeometryClothParticles<T, d>& CollisionParticles() { return MCollisionParticles; }
	const bool Collided(int32 index) { return MCollided[index]; }

	TArray<TVector<int32, 3>>& CollisionTriangles() { return MCollisionTriangles; }
	TSet<TVector<int32, 2>>& DisabledCollisionElements() { return MDisabledCollisionElements; }

	int32 GetIterations() const { return MNumIterations; }
	void SetIterations(const int32 Iterations) { MNumIterations = Iterations; }

	T GetSelfCollisionThickness() const { return MSelfCollisionThickness; }
	void SetSelfCollisionThickness(const T SelfCollisionThickness) { MSelfCollisionThickness = SelfCollisionThickness; }

	T GetCollisionThickness() const { return MCollisionThickness; }
	void SetCollisionThickness(const T CollisionThickness) { MCollisionThickness = CollisionThickness; }

	T GetCoefficientOfFriction() const { return MCoefficientOfFriction; }
	void SetCoefficientOfFriction(const T CoefficientOfFriction) { MCoefficientOfFriction = CoefficientOfFriction; }

	T GetDamping() const { return MDamping; }
	void SetDamping(const T Damping) { MDamping = Damping; }

	T GetTime() const { return MTime; }

	void ResetConstraintRules() { MInitConstraintRules.Reset(); MConstraintRules.Reset(); };
	void ResetSelfCollision() { MCollisionTriangles.Reset(); MDisabledCollisionElements.Reset(); };
	void ResetVelocityFields() { VelocityFields.Reset(); };

  private:
	TPBDParticles<T, d> MParticles;
	TKinematicGeometryClothParticles<T, d> MCollisionParticles;
	TArray<TVector<int32, 3>> MCollisionTriangles;       // Used for self-collisions
	TSet<TVector<int32, 2>> MDisabledCollisionElements;  // 
	TArrayCollectionArray<bool> MCollided;
	int32 MNumIterations;
	T MCollisionThickness;
	T MSelfCollisionThickness;
	T MCoefficientOfFriction;
	T MDamping;
	T MTime;

	FGravityForces GravityForces;
	TArray<FVelocityField> VelocityFields;

	TArray<TFunction<void(TPBDParticles<T, d>&, const T, const int32)>> MForceRules;
	TArray<TFunction<void()>> MInitConstraintRules;
	TArray<TFunction<void(TPBDParticles<T, d>&, const T)>> MConstraintRules;
	TFunction<void(TPBDParticles<T, d>&, const T)> MParticleUpdate;
	TFunction<void(TPBDParticles<T, d>&, const T, const T, const int32)> MKinematicUpdate;
	TFunction<void(TKinematicGeometryClothParticles<T, d>&, const T, const T, const int32)> MCollisionKinematicUpdate;
};
}
