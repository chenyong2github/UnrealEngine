// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PullPhysicsDataImp.h"

namespace Chaos
{
	class FChaosMarshallingManager;
	
	struct FResimParticleInfo
	{
		FDirtyRigidParticleData Next;
		FReal LeashStartTime = -1;
		FReal EntryTime = -1;
		bool bDiverged;
	};

	struct CHAOS_API FChaosRigidInterpolationData
	{
		FDirtyRigidParticleData Prev;
		FDirtyRigidParticleData Next;
		FRealSingle LeashAlpha = 1;
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

		void SetHistoryLength_External(int32 InLength);
		void RemoveProxy_External(FSingleParticlePhysicsProxy* Proxy);
		void SetLastExternalDt_External(const FReal InExternalDt) { LastExternalDt = InExternalDt; }
		void SetResimInterpTime(const FReal InterpTime);
		void SetResimInterpStrength(const FReal InterpStrength) { ResimInterpStrength = InterpStrength; }

	private:

		const FChaosInterpolationResults& UpdateInterpAlpha_External(const FReal ResultsTime, const FReal GlobalAlpha);
		void FreeToHistory_External(FPullPhysicsData* PullData);
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
		TArray<FPullPhysicsData*> ResultsHistory;
		FReal LatestTimeSeen = 0;	//we use this to know when resim results are being pushed
		int32 HistoryLength = 0;
		FChaosMarshallingManager& MarshallingManager;
		FReal LastExternalDt = 0;
		FReal InvResimInterpTime = 0;
		FReal ResimInterpStrength = 1;
		TMap<FSingleParticlePhysicsProxy*, FResimParticleInfo> ParticleToResimInfo;
	};
}
