// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PullPhysicsDataImp.h"

namespace Chaos
{
	class FChaosMarshallingManager;

	struct CHAOS_API FChaosRigidInterpolationData
	{
		FDirtyRigidParticleData Prev;
		FDirtyRigidParticleData Next;
	};

	struct CHAOS_API FChaosInterpolationResults
	{
		FChaosInterpolationResults()
			: Prev(nullptr)
			, Next(nullptr)
		{
		}

		void Reset()
		{
			RigidInterpolations.Reset();

			//purposely leave Prev and Next alone as we use those for rebuild
		}

		TArray<FChaosRigidInterpolationData> RigidInterpolations;
		FPullPhysicsData* Prev;
		FPullPhysicsData* Next;
		FRealSingle Alpha;
	};

	class CHAOS_API FChaosResultsManager
	{
	public:
		FChaosResultsManager()
		{
		}

		FPullPhysicsData* PullSyncPhysicsResults_External(FChaosMarshallingManager& MarshallingManager);
		const FChaosInterpolationResults& PullAsyncPhysicsResults_External(FChaosMarshallingManager& MarshallingManager, const FReal ResultsTime);

	private:
		FChaosInterpolationResults Results;
		bool bUsingSync;
	};
}
