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
#include "Chaos/Framework/ChaosResultsManager.h"

namespace Chaos
{
	// Pulls physics state for each dirty particle and allows caller to do additional work if needed
	template <typename RigidLambda>
	void FPhysicsSolverBase::PullPhysicsStateForEachDirtyProxy_External(const RigidLambda& RigidFunc)
	{
		using namespace Chaos;

		FPullPhysicsData* LatestData = nullptr;
		if (IsUsingAsyncResults() && UseAsyncInterpolation)
		{
			const FReal ResultsTime = GetPhysicsResultsTime_External();
			//we want to interpolate between prev and next. There are a few cases to consider:
			//case 1: dirty data exists in both prev and next. In this case continuous data is interpolated, state data is a step function from prev to next
			//case 2: prev has dirty data and next doesn't. in this case take prev as it means nothing to interpolate, just a constant value
			//case 3: prev has dirty data and next has overwritten data. In this case we do nothing as the overwritten data wins (came from GT), and also particle may be deleted
			//case 4: prev has no dirty data and next does. In this case interpolate from gt data to next
			//case 5: prev has no dirty data and next was overwritten. In this case do nothing as the overwritten data wins, and also particle may be deleted

			const FChaosInterpolationResults& Results = PullResultsManager->PullAsyncPhysicsResults_External(MarshallingManager, ResultsTime);
			LatestData = Results.Next;
			//todo: go wide
			const int32 SolverTimestamp = Results.Next ? Results.Next->SolverTimestamp : INDEX_NONE;
			for(const FChaosRigidInterpolationData& RigidInterp : Results.RigidInterpolations)
			{
				if(FSingleParticlePhysicsProxy* Proxy = RigidInterp.Prev.GetProxy())
				{
					if (Proxy->PullFromPhysicsState(RigidInterp.Prev, SolverTimestamp, &RigidInterp.Next, &Results.Alpha))
					{
						RigidFunc(Proxy);
					}

					//Only used for building results. These results are either reused, or re-built
					//if they are rebuilt we get a new index
					Proxy->SetPullDataInterpIdx_External(INDEX_NONE);
				}
				
			}
		}
		else
		{
			//no interpolation so just use latest
			if (FPullPhysicsData* PullData = PullResultsManager->PullSyncPhysicsResults_External(MarshallingManager))
			{
				LatestData = PullData;
				const int32 SyncTimestamp = PullData->SolverTimestamp;
				for (const FDirtyRigidParticleData& DirtyData : PullData->DirtyRigids)
				{
					if (auto Proxy = DirtyData.GetProxy())
					{
						if (Proxy->PullFromPhysicsState(DirtyData, SyncTimestamp))
						{
							RigidFunc(Proxy);
						}
					}
				}
			}
		}

		//no interpolation for GC or joints at the moment
		if(LatestData)
		{
			const int32 SyncTimestamp = LatestData->SolverTimestamp;
			for (const FDirtyGeometryCollectionData& DirtyData : LatestData->DirtyGeometryCollections)
			{
				if (auto Proxy = DirtyData.GetProxy())
				{
					Proxy->PullFromPhysicsState(DirtyData, SyncTimestamp);
				}
			}

			//latest data may be used multiple times during interpolation, so for non interpolated GC we clear it
			LatestData->DirtyGeometryCollections.Reset();

			//
			// @todo(chaos) : Add Dirty Constraints Support
			//
			// This is temporary constraint code until the DirtyParticleBuffer
			// can be updated to support constraints. In summary : The 
			// FDirtyPropertiesManager is going to be updated to support a 
			// FDirtySet that is specific to a TConstraintProperties class.
			//
			for (const FDirtyJointConstraintData& DirtyData : LatestData->DirtyJointConstraints)
			{
				if (auto Proxy = DirtyData.GetProxy())
				{
					Proxy->PullFromPhysicsState(DirtyData, SyncTimestamp);
				}
			}

			//latest data may be used multiple times during interpolation, so for non interpolated joints we clear it
			LatestData->DirtyJointConstraints.Reset();
		}
	}

}