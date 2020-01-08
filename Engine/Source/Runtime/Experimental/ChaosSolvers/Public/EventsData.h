// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ExternalCollisionData.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Framework/PhysicsProxy.h"

namespace Chaos
{
	// base class for data that requires time of creation to be recorded
	struct FTimeResource
	{
		FTimeResource() : TimeCreated(-FLT_MAX) {}
		float TimeCreated;
	};

	typedef TArray<TCollisionData<float, 3>> FCollisionDataArray;
	typedef TArray<TBreakingData<float, 3>> FBreakingDataArray;
	typedef TArray<TTrailingData<float, 3>> FTrailingDataArray;
	typedef TArray<TSleepingData<float, 3>> FSleepingDataArray;

	/* Common */

	/* Maps PhysicsProxy to list of indices in events arrays 
	 * - for looking up say all collisions a particular physics object had this frame
	 */
	struct FIndicesByPhysicsProxy : public FTimeResource
	{
		FIndicesByPhysicsProxy()
			: PhysicsProxyToIndicesMap(TMap<IPhysicsProxyBase*, TArray<int32>>())
		{}

		void Reset()
		{
			PhysicsProxyToIndicesMap.Reset();
		}

		TMap<IPhysicsProxyBase*, TArray<int32>> PhysicsProxyToIndicesMap; // PhysicsProxy -> Indices in Events arrays
	};

	/* Collision */

	/*   
	 * All the collision events for one frame time stamped with the time for that frame
	 */
	struct FAllCollisionData : public FTimeResource
	{
		FAllCollisionData() : AllCollisionsArray(FCollisionDataArray()) {}

		void Reset()
		{
			AllCollisionsArray.Reset();
		}

		FCollisionDataArray AllCollisionsArray;
	};

	struct FCollisionEventData
	{
		FCollisionEventData() {}

		FAllCollisionData CollisionData;
		FIndicesByPhysicsProxy PhysicsProxyToCollisionIndices;
	};

	/* Breaking */

	/*
	 * All the breaking events for one frame time stamped with the time for that frame
	 */
	struct FAllBreakingData : public FTimeResource
	{
		FAllBreakingData() : AllBreakingsArray(FBreakingDataArray()) {}

		void Reset()
		{
			AllBreakingsArray.Reset();
		}

		FBreakingDataArray AllBreakingsArray;
	};

	struct FBreakingEventData
	{
		FBreakingEventData() {}

		FAllBreakingData BreakingData;
		FIndicesByPhysicsProxy PhysicsProxyToBreakingIndices;
	};

	/* Trailing */

	/*
	 * All the trailing events for one frame time stamped with the time for that frame  
	 */
	struct FAllTrailingData : FTimeResource
	{
		FAllTrailingData() : AllTrailingsArray(FTrailingDataArray()) {}

		void Reset()
		{
			AllTrailingsArray.Reset();
		}

		FTrailingDataArray AllTrailingsArray;
	};


	struct FTrailingEventData
	{
		FTrailingEventData() {}

		FAllTrailingData TrailingData;
		FIndicesByPhysicsProxy PhysicsProxyToTrailingIndices;
	};

	struct FSleepingEventData
	{
		FSleepingEventData() {}

		FSleepingDataArray SleepingData;
		FIndicesByPhysicsProxy PhysicsProxyToSleepingIndices;
	};

}
