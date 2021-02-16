// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/PBDConstraintGraph.h"

#include <memory>
#include <queue>
#include <sstream>

#if CHAOS_PARTICLEHANDLE_TODO

namespace ChaosTest
{
	void CollisionPGS();
	void CollisionPGS2();
}

namespace Chaos
{
class FPBDCollisionConstraintPGS
{
  public:
	friend void ChaosTest::CollisionPGS();
	friend void ChaosTest::CollisionPGS2();

	typedef FRigidBodyContactConstraintPGS FRigidBodyContactConstraint;
	typedef FPBDConstraintGraph FConstraintGraph;

	FPBDCollisionConstraintPGS(FPBDRigidParticles& InParticles, const TArray<int32>& InIndices, TArrayCollectionArray<bool>& Collided, TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& InPhysicsMaterials, const FReal Thickness = (FReal)0);
	virtual ~FPBDCollisionConstraintPGS() {}

	void ComputeConstraints(const FPBDRigidParticles& InParticles, const TArray<int32>& InIndices, const FReal Dt);

	void Apply(FPBDRigidParticles& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices);
	bool ApplyPushOut(FPBDRigidParticles& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices);
	template<class T_PARTICLES> void Solve(T_PARTICLES& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices);

	void RemoveConstraints(const TSet<uint32>& RemovedParticles);
	void UpdateConstraints(const FPBDRigidParticles& InParticles, FReal Dt, const TSet<uint32>& AddedParticles, const TArray<uint32>& ActiveParticles);


	static bool NearestPoint(TArray<Pair<FVec3, FVec3>>& Points, FVec3& Direction);

	const TArray<FRigidBodyContactConstraint>& GetAllConstraints() const { return Constraints; }

	int32 NumConstraints() const { return Constraints.Num(); }
	TVec2<int32> ConstraintParticleIndices(int32 ContactIndex) const { return { Constraints[ContactIndex].ParticleIndex, Constraints[ContactIndex].LevelsetIndex }; }
	void UpdatePositionBasedState(const FPBDRigidParticles& InParticles, const TArray<int32>& InIndices, const FReal Dt) { ComputeConstraints(InParticles, InIndices, Dt); }

  private:
	void PrintParticles(const FPBDRigidParticles& InParticles, const TArray<int32>& InConstraintIndices);
	void PrintConstraints(const FPBDRigidParticles& InParticles, const TArray<int32>& InConstraintIndices);

	template<class T_PARTICLES>
	void UpdateConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateLevelsetConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateLevelsetConstraintGJK(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateBoxConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateBoxPlaneConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateSphereConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateSpherePlaneConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint);
	template<class T_PARTICLES>
	void UpdateSphereBoxConstraint(const T_PARTICLES& InParticles, const FReal Thickness, FRigidBodyContactConstraint& Constraint);

	FRigidBodyContactConstraint ComputeLevelsetConstraint(const FPBDRigidParticles& InParticles, int32 ParticleIndex, int32 LevelsetIndex, const FReal Thickness);
	FRigidBodyContactConstraint ComputeLevelsetConstraintGJK(const FPBDRigidParticles& InParticles, int32 ParticleIndex, int32 LevelsetIndex, const FReal Thickness);
	FRigidBodyContactConstraint ComputeBoxConstraint(const FPBDRigidParticles& InParticles, int32 Box1Index, int32 Box2Index, const FReal Thickness);
	FRigidBodyContactConstraint ComputeBoxPlaneConstraint(const FPBDRigidParticles& InParticles, int32 BoxIndex, int32 PlaneIndex, const FReal Thickness);
	FRigidBodyContactConstraint ComputeSphereConstraint(const FPBDRigidParticles& InParticles, int32 Sphere1Index, int32 Sphere2Index, const FReal Thickness);
	FRigidBodyContactConstraint ComputeSpherePlaneConstraint(const FPBDRigidParticles& InParticles, int32 SphereIndex, int32 PlaneIndex, const FReal Thickness);
	FRigidBodyContactConstraint ComputeSphereBoxConstraint(const FPBDRigidParticles& InParticles, int32 SphereIndex, int32 BoxIndex, const FReal Thickness);
	FRigidBodyContactConstraint ComputeConstraint(const FPBDRigidParticles& InParticles, int32 Body1Index, int32 Body2Index, const FReal Thickness);

	TArray<FRigidBodyContactConstraint> Constraints;
	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& MPhysicsMaterials;
	const FReal MThickness;
	const FReal Tolerance;
	const int32 MaxIterations;
	const bool bUseCCD;
};

template<class T, int d>
using TPBDCollisionConstraintPGS UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDCollisionConstraintPGS instead") = FPBDCollisionConstraintPGS;
}

#endif