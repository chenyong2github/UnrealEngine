// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Queue.h"
#include "Chaos/Defines.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/ParallelFor.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "GeometryCollectionProxyData.h"
#include "PBDRigidsEvolutionFwd.h"

template <typename T>
class TJointConstraintProxy;

namespace Chaos
{

//Simple struct for when the simulation dirties a particle. Copies all properties regardless of which changed since they tend to change together
struct FDirtyRigidParticleData
{
	FRigidParticlePhysicsProxy* Proxy;
	FVec3 X;
	FQuat R;
	FVec3 V;
	FVec3 W;
	EObjectStateType ObjectState;
};

struct FDirtyGeometryCollectionData
{
	FGeometryCollectionPhysicsProxy* Proxy;
	FGeometryCollectionResults Results;
};

struct FJointConstraintOutputData {
	bool bIsBroken = false;
	FVector Force = FVector(0);
	FVector Torque = FVector(0);
};

class FJointConstraint;

struct FDirtyJointConstraintData
{
	TJointConstraintProxy<FJointConstraint>* Proxy;
	FJointConstraintOutputData OutputData;
};

//A simulation frame's result of dirty particles. These are all the particles that were dirtied in this particular sim step
class FPullPhysicsData
{
public:
	TArray<FDirtyRigidParticleData> DirtyRigids;
	TArray<FDirtyGeometryCollectionData> DirtyGeometryCollections;
	TArray<FDirtyJointConstraintData> DirtyJointConstraints;

	int32 SolverTimestamp;

	void Reset();
};

}; // namespace Chaos
