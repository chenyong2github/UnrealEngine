// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/GCObject.h"

#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/PullPhysicsData.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/JointConstraintProxy.h"

namespace Chaos
{

// Pulls physics state for each dirty particle and allows caller to do additional work if needed
template <typename RigidLambda>
void FPhysicsSolverBase::PullPhysicsStateForEachDirtyProxy_External(const int32 SyncTimestamp, const RigidLambda& RigidFunc)
{
	using namespace Chaos;

	//todo: handle timestamp better here. For now we only expect 1 per frame, but editor may tick with 0 dt which gets no results
	if(FPullPhysicsData* PullData = MarshallingManager.PopPullData_External())
	{
		for(const FDirtyRigidParticleData& DirtyData : PullData->DirtyRigids)
		{
			if(DirtyData.Proxy->PullFromPhysicsState(DirtyData,SyncTimestamp))
			{
				RigidFunc(DirtyData.Proxy);
			}
		}

		for(const FDirtyGeometryCollectionData& DirtyData : PullData->DirtyGeometryCollections)
		{
			DirtyData.Proxy->PullFromPhysicsState(DirtyData,SyncTimestamp);
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
			DirtyData.Proxy->PullFromPhysicsState(DirtyData,SyncTimestamp);
		}

		MarshallingManager.FreePullData_External(PullData);
	}
}

}