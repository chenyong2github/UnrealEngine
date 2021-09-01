// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ExternalCollisionData.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Framework/PhysicsProxy.h"

class UPrimitiveComponent;

namespace Chaos
{
	// base class for data that requires time of creation to be recorded
	struct FTimeResource
	{
		FTimeResource() : TimeCreated(-TNumericLimits<FReal>::Max()) {}
		FReal TimeCreated;
	};

	typedef TArray<FCollidingData> FCollisionDataArray;
	typedef TArray<FBreakingData> FBreakingDataArray;
	typedef TArray<FTrailingData> FTrailingDataArray;
	typedef TArray<FRemovalData> FRemovalDataArray;
	typedef TArray<FSleepingData> FSleepingDataArray;

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

	/* Removal */

	/*
	 * All the removal events for one frame time stamped with the time for that frame
	 */
	struct FAllRemovalData : FTimeResource
	{
		FAllRemovalData() : AllRemovalArray(FRemovalDataArray()) {}

		void Reset()
		{
			AllRemovalArray.Reset();
		}

		FRemovalDataArray AllRemovalArray;
	};


	struct FRemovalEventData
	{
		FRemovalEventData() {}

		FAllRemovalData RemovalData;
		FIndicesByPhysicsProxy PhysicsProxyToRemovalIndices;
	};

	struct FSleepingEventData
	{
		FSleepingEventData() {}

		FSleepingDataArray SleepingData;
	};

}
