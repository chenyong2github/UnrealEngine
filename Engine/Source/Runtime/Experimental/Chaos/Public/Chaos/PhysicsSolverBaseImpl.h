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
// Pulls physics state for each dirty particle and allows caller to do additional work if needed
template <typename RigidLambda>
void FPhysicsSolverBase::PullPhysicsStateForEachDirtyProxy_External(const RigidLambda& RigidFunc)
{
	using namespace Chaos;
	const FReal ResultsTime = MarshallingManager.GetExternalTime_External();	//TODO: account for delay into the past

	FChaosPullPhysicsResults Results = PullResultsManager.PullPhysicsResults_External(MarshallingManager, ResultsTime, IsUsingAsyncResults());
	if(FPullPhysicsData* PullData = Results.Next)
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
	}
}

}