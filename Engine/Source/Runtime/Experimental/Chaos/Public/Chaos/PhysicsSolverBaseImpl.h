// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/GCObject.h"

#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/PullPhysicsDataImp.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/JointConstraintProxy.h"

namespace Chaos
{
	extern CHAOS_API int UseAsyncResults;
// Pulls physics state for each dirty particle and allows caller to do additional work if needed
template <typename RigidLambda>
void FPhysicsSolverBase::PullPhysicsStateForEachDirtyProxy_External(const RigidLambda& RigidFunc)
{
	using namespace Chaos;

	FPullPhysicsData* PullData = nullptr;
	if(UseAsyncResults)
	{
		//todo: handle timestamp better here. For now we only expect 1 per frame, but editor may tick with 0 dt which gets no results
		PullData = MarshallingManager.PopPullData_External();
	}
	else
	{
		//if we turn async mode on and off, we may end up with a queue that has multiple frames worth of results. In that case use the latest
		FPullPhysicsData* PullDataLatest = nullptr;
		do
		{
			PullData = PullDataLatest;
			PullDataLatest = MarshallingManager.PopPullData_External();

		} while (PullDataLatest);
	}
	
	if(PullData)
	{
		const int32 SyncTimestamp = PullData->SolverTimestamp;
		for(const FDirtyRigidParticleData& DirtyData : PullData->DirtyRigids)
		{
			if(auto Proxy = DirtyData.GetProxy(SyncTimestamp))
			{
				if(Proxy->PullFromPhysicsState(DirtyData,SyncTimestamp))
				{
					RigidFunc(Proxy);
				}
			}
		}

		for(const FDirtyGeometryCollectionData& DirtyData : PullData->DirtyGeometryCollections)
		{
			if(auto Proxy = DirtyData.GetProxy(SyncTimestamp))
			{
				Proxy->PullFromPhysicsState(DirtyData,SyncTimestamp);
			}
		}

		//
		// @todo(chaos) : Add Dirty Constraints Support
		//
		// This is temporary constraint code until the DirtyParticleBuffer
		// can be updated to support constraints. In summary : The 
		// FDirtyPropertiesManager is going to be updated to support a 
		// FDirtySet that is specific to a TConstraintProperties class.
		//
		for(const FDirtyJointConstraintData& DirtyData : PullData->DirtyJointConstraints)
		{
			if(auto Proxy = DirtyData.GetProxy(SyncTimestamp))
			{
				Proxy->PullFromPhysicsState(DirtyData,SyncTimestamp);
			}
		}

		MarshallingManager.FreePullData_External(PullData);
	}
}

}