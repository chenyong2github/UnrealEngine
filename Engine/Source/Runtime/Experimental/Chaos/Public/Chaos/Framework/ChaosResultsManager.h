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

		void Reset();
		
		TArray<FChaosRigidInterpolationData> RigidInterpolations;
		FPullPhysicsData* Prev;
		FPullPhysicsData* Next;
		FRealSingle Alpha;
	};

	class CHAOS_API FChaosResultsManager
	{
	public:
		FChaosResultsManager(FChaosMarshallingManager& InMarshallingManager);
		
		~FChaosResultsManager();

		const FChaosInterpolationResults& PullSyncPhysicsResults_External();
		const FChaosInterpolationResults& PullAsyncPhysicsResults_External(const FReal ResultsTime);

		void RemoveProxy_External(FSingleParticlePhysicsProxy* Proxy);
		void SetLastExternalDt_External(const FReal InExternalDt) { LastExternalDt = InExternalDt; }
	private:

		const FChaosInterpolationResults& UpdateInterpAlpha_External(const FReal ResultsTime, const FReal GlobalAlpha);
		void ProcessResimResult_External();
		bool AdvanceResult();
		void CollapseResultsToLatest();

		enum class ESetPrevNextDataMode
		{
			Prev,
			Next,
		};

		template <ESetPrevNextDataMode Mode>
		void SetPrevNextDataHelper(const FPullPhysicsData& PullData);

		FChaosInterpolationResults Results;
		FReal LatestTimeSeen = 0;	//we use this to know when resim results are being pushed
		FChaosMarshallingManager& MarshallingManager;
		FReal LastExternalDt = 0;
		TMap<FSingleParticlePhysicsProxy*, FDirtyRigidParticleData> ParticleToResimTarget;
	};
}
