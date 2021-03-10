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

class FJointConstraintPhysicsProxy;

namespace Chaos
{

template <typename TProxy>
struct TBasePullData
{
public:
	void SetProxy(TProxy& InProxy)
	{
		ensure(Timestamp.Get() == nullptr);
		Timestamp = InProxy.GetSyncTimestamp();
		Proxy = &InProxy;
	}

	TProxy* GetProxy() const
	{
		return !Timestamp->bDeleted ? Proxy : nullptr;
	}

	const FProxyTimestamp* GetTimestamp() const { return Timestamp.Get(); }

protected:
	TBasePullData() : Proxy(nullptr){}
	~TBasePullData() = default;
	
private:
	TProxy* Proxy;
	TSharedPtr<FProxyTimestamp,ESPMode::ThreadSafe> Timestamp;	//question: is destructor expensive now? might need a better way
};

//Simple struct for when the simulation dirties a particle. Copies all properties regardless of which changed since they tend to change together
struct FDirtyRigidParticleData : public TBasePullData<FSingleParticlePhysicsProxy>
{
	FVec3 X;
	FQuat R;
	FVec3 V;
	FVec3 W;
	EObjectStateType ObjectState;
};

struct FDirtyGeometryCollectionData : public TBasePullData<FGeometryCollectionPhysicsProxy>
{
	FGeometryCollectionResults Results;
};

struct FJointConstraintOutputData {
	bool bIsBroken = false;
	FVector Force = FVector(0);
	FVector Torque = FVector(0);
};

class FJointConstraint;

struct FDirtyJointConstraintData : public TBasePullData<FJointConstraintPhysicsProxy>
{
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
	FReal ExternalStartTime;	//The start time associated with this result. The time is synced using the external time
	FReal ExternalEndTime;		//The end time associated with this result. The time is synced using the external time

	void Reset();
};

}; // namespace Chaos
