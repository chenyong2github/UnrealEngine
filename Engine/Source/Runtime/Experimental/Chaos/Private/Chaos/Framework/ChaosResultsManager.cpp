// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Framework/ChaosResultsManager.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/ChaosMarshallingManager.h"

namespace Chaos
{	
	enum class ESetPrevNextDataMode
	{
		Prev,
		Next,
	};

	template <ESetPrevNextDataMode Mode>
	void SetPrevNextDataHelper(const FPullPhysicsData& PullData, TArray<FChaosRigidInterpolationData>& OutDataArray)
	{
		//clear results
		const int32 Timestamp = PullData.SolverTimestamp;
		for (const FDirtyRigidParticleData& Data : PullData.DirtyRigids)
		{
			if (FSingleParticlePhysicsProxy* Proxy = Data.GetProxy())
			{
				int32 DataIdx = Proxy->GetPullDataInterpIdx_External();
				if(DataIdx == INDEX_NONE)
				{
					DataIdx = OutDataArray.AddDefaulted(1);
					Proxy->SetPullDataInterpIdx_External(DataIdx);

					if(Mode == ESetPrevNextDataMode::Next)
					{
						//no prev so use GT data
						Proxy->BufferPhysicsResults_External(OutDataArray[DataIdx].Prev);
					}
				}

				FChaosRigidInterpolationData& OutData = OutDataArray[DataIdx];

				if(Mode == ESetPrevNextDataMode::Prev)
				{
					//if particle doesn't change we won't get it in next step, so just interpolate as constant
					OutData.Prev = Data;
					OutData.Next = Data;
				}
				else if(Mode == ESetPrevNextDataMode::Next)
				{
					OutData.Next = Data;
				}
			}
		}
	}

	/** 
	 Advance the results in the marshaller queue by one if it is available 

	 @param MarshallingManager Manger to advance
	 @param Results Results to update with advanced state
	 @return whether an advance occurred
	*/
	bool AdvanceResult(FChaosMarshallingManager& MarshallingManager, FChaosInterpolationResults& Results)
	{
		if(FPullPhysicsData* PotentialNext = MarshallingManager.PopPullData_External())
		{
			//newer result exists so prev is overwritten
			if(Results.Prev)
			{
				MarshallingManager.FreePullData_External(Results.Prev);
			}

			Results.Prev = Results.Next;
			//mark prev with next's data.
			//any particles that were dirty in the previous results and are now constant will still have the old values set
			SetPrevNextDataHelper<ESetPrevNextDataMode::Prev>(*Results.Prev, Results.RigidInterpolations);

			Results.Next = PotentialNext;
			return true;
		}
		
		return false;
	}

	/**
	 Collapse the whole pending queue inside a marshalling manager to one results object written to Results.Next

	 @param MarshallingManager Manger to advance
	 @param Results Results to update with advanced state
	*/
	void CollapseResultsToLatest(FChaosMarshallingManager& MarshallingManager, FChaosInterpolationResults& Results)
	{
		if(Results.Next == nullptr)
		{
			//nothing in Next (first time), so get latest if possible
			Results.Next = MarshallingManager.PopPullData_External();
		}

		while(AdvanceResult(MarshallingManager, Results))
		{}
	}

	FPullPhysicsData* FChaosResultsManager::PullSyncPhysicsResults_External(FChaosMarshallingManager& MarshallingManager, bool bWasSubstepping)
	{
		//sync mode doesn't use prev results, but if we were async previously we need to clean it up
		if (Results.Prev)
		{
			MarshallingManager.FreePullData_External(Results.Prev);
			Results.Prev = nullptr;
		}

		// If we switched from async to sync we may have multiple pending results, so discard them all except latest.
		// If we dispatched substeps there will be multiple results pending but the latest is the one we want.
		if(bUsingSync == false || bWasSubstepping)
		{
			CollapseResultsToLatest(MarshallingManager, Results);
		}
		else
		{
			//already in sync mode so don't take latest, just take next result
			if (Results.Next)
			{
				MarshallingManager.FreePullData_External(Results.Next);
			}

			Results.Next = MarshallingManager.PopPullData_External();
		}

		bUsingSync = true;	//indicate that we used sync mode so don't rely on any cached results

		return Results.Next;
	}

	FReal ComputeAlphaHelper(const FPullPhysicsData& Next, const FReal ResultsTime)
	{
		const FReal Denom = Next.ExternalEndTime - Next.ExternalStartTime;
		if (Denom > 0)	//if 0 dt just skip
		{
			//if we have no future results alpha will be > 1
			//in that case we just keep rendering the latest results we can
			return FMath::Min(1.f, (ResultsTime - Next.ExternalStartTime) / Denom);
		}

		return 1;	//if 0 dt just use 1 as alpha
	}

	const FChaosInterpolationResults& FChaosResultsManager::PullAsyncPhysicsResults_External(FChaosMarshallingManager& MarshallingManager, const FReal ResultsTime)
	{
		//in async mode we must interpolate between Start and End of a particular sim step, where ResultsTime is in the inclusive interval [Start, End]
		//to do this we need to keep the results of the previous sim step, which ends exactly when the next one starts
		//if no previous result exists, we use the existing GT data

		if(ResultsTime < 0)
		{
			Results.Alpha = 1;
			return Results;
		}

		if(Results.Next && ResultsTime <= Results.Next->ExternalEndTime && !bUsingSync)
		{
			//already have results, just need to update alpha
			Results.Alpha = ComputeAlphaHelper(*Results.Next, ResultsTime);
			return Results;
		}

		bUsingSync = false;

		//either brand new, or we are consuming new results. Either way need to rebuild everything
		Results.Reset();

		if (Results.Next == nullptr)
		{
			//nothing in Next (first time), so get latest if possible
			Results.Next = MarshallingManager.PopPullData_External();
		}

		if(Results.Next)
		{
			//go through every result and record the dirty proxies
			while (Results.Next->ExternalEndTime < ResultsTime)
			{
				if(!AdvanceResult(MarshallingManager, Results))
				{
					break;
				}
			}
		}

		ensure(Results.Prev == nullptr || Results.Next != nullptr);	//we can never have a prev set when there isn't a next

		if(Results.Next)
		{
			//whatever next ends up being, we mark the data as such
			SetPrevNextDataHelper<ESetPrevNextDataMode::Next>(*Results.Next, Results.RigidInterpolations);
			Results.Alpha = ComputeAlphaHelper(*Results.Next, ResultsTime);
		}

		return Results;
	}
}
