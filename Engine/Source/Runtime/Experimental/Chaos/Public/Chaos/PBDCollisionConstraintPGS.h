// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDCollisionTypes.h"
#include "Chaos/PBDConstraintGraph.h"

#include <memory>
#include <queue>
#include <sstream>

#if CHAOS_PARTICLEHANDLE_TODO

namespace ChaosTest
{
	template<class T> void CollisionPGS();
	template<class T> void CollisionPGS2();
}

namespace Chaos
{
template<class T, int d>
class TPBDCollisionConstraintPGS
{
  public:
	friend void ChaosTest::CollisionPGS<T>();
	friend void ChaosTest::CollisionPGS2<T>();

	typedef TRigidBodyContactConstraintPGS<T, d> FRigidBodyContactConstraint;
	typedef TPBDConstraintGraph<T, d> FConstraintGraph;

	TPBDCollisionConstraintPGS(TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, TArrayCollectionArray<bool>& Collided, TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& InPhysicsMaterials, const T Thickness = (T)0);
	virtual ~TPBDCollisionConstraintPGS() {}

	void ComputeConstraints(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const T Dt);

	void Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices);
	void ApplyPushOut(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices);
	template<class T_PARTICLES> void Solve(T_PARTICLES& InParticles, const T Dt, const TArray<int32>& InConstraintIndices);

	void RemoveConstraints(const TSet<uint32>& RemovedParticles);
	void UpdateConstraints(const TPBDRigidParticles<T, d>& InParticles, T Dt, const TSet<uint32>& AddedParticles, const TArray<uint32>& ActiveParticles);


	static bool NearestPoint(TArray<Pair<TVector<T, d>, TVector<T, d>>>& Points, TVector<T, d>& Direction);

	const TArray<FRigidBodyContactConstraint>& GetAllConstraints() const { return Constraints; }

	int32 NumConstraints() const { return Constraints.Num(); }
	TVector<int32, 2> ConstraintParticleIndices(int32 ContactIndex) const { return { Constraints[ContactIndex].ParticleIndex, Constraints[ContactIndex].LevelsetIndex }; }
	void UpdatePositionBasedState(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const T Dt) { ComputeConstraints(InParticles, InIndices, Dt); }

  private:
	void PrintParticles(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InConstraintIndices);
	void PrintConstraints(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InConstraintIndices);

	template<class T_PARTICLES>
	void UpdateConstraint(const T_PARTICLES& InParticles, const T Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateLevelsetConstraint(const T_PARTICLES& InParticles, const T Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateLevelsetConstraintGJK(const T_PARTICLES& InParticles, const T Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateBoxConstraint(const T_PARTICLES& InParticles, const T Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateBoxPlaneConstraint(const T_PARTICLES& InParticles, const T Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateSphereConstraint(const T_PARTICLES& InParticles, const T Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateSpherePlaneConstraint(const T_PARTICLES& InParticles, const T Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateSphereBoxConstraint(const T_PARTICLES& InParticles, const T Thickness, FRigidBodyContactConstraint& Constraint);

	FRigidBodyContactConstraint ComputeLevelsetConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 ParticleIndex, int32 LevelsetIndex, const T Thickness);
	FRigidBodyContactConstraint ComputeLevelsetConstraintGJK(const TPBDRigidParticles<T, d>& InParticles, int32 ParticleIndex, int32 LevelsetIndex, const T Thickness);
	FRigidBodyContactConstraint ComputeBoxConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 Box1Index, int32 Box2Index, const T Thickness);
	FRigidBodyContactConstraint ComputeBoxPlaneConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 BoxIndex, int32 PlaneIndex, const T Thickness);
	FRigidBodyContactConstraint ComputeSphereConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 Sphere1Index, int32 Sphere2Index, const T Thickness);
	FRigidBodyContactConstraint ComputeSpherePlaneConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 SphereIndex, int32 PlaneIndex, const T Thickness);
	FRigidBodyContactConstraint ComputeSphereBoxConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 SphereIndex, int32 BoxIndex, const T Thickness);
	FRigidBodyContactConstraint ComputeConstraint(const TPBDRigidParticles<T, d>& InParticles, int32 Body1Index, int32 Body2Index, const T Thickness);

	TArray<FRigidBodyContactConstraint> Constraints;
	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& MPhysicsMaterials;
	const T MThickness;
	const T Tolerance;
	const T MaxIterations;
	const bool bUseCCD;
};
}

#endif