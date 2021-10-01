// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Framework/ChaosResultsManager.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/ChaosMarshallingManager.h"

namespace Chaos
{	
	void FChaosInterpolationResults::Reset()
	{
		for (FChaosRigidInterpolationData& Data : RigidInterpolations)
		{
			if (FSingleParticlePhysicsProxy* Proxy = Data.Prev.GetProxy())
			{
				Proxy->SetPullDataInterpIdx_External(INDEX_NONE);
			}
		}
		RigidInterpolations.Reset();

		//purposely leave Prev and Next alone as we use those for rebuild
	}

	enum class ESetPrevNextDataMode
	{
		Prev,
		Next,
	};

	template <FChaosResultsManager::ESetPrevNextDataMode Mode>
	void FChaosResultsManager::SetPrevNextDataHelper(const FPullPhysicsData& PullData)
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
					DataIdx = Results.RigidInterpolations.AddDefaulted(1);
					Proxy->SetPullDataInterpIdx_External(DataIdx);

					if(Mode == ESetPrevNextDataMode::Next)
					{
						//no prev so use GT data
						Proxy->BufferPhysicsResults_External(Results.RigidInterpolations[DataIdx].Prev);
					}
				}

				FChaosRigidInterpolationData& OutData = Results.RigidInterpolations[DataIdx];

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

				//update leash target
				if(FDirtyRigidParticleData* ResimTarget = ParticleToResimTarget.Find(Proxy))
				{
					*ResimTarget = Data;
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
	bool FChaosResultsManager::AdvanceResult()
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
			SetPrevNextDataHelper<ESetPrevNextDataMode::Prev>(*Results.Prev);

			Results.Next = PotentialNext;
			
			if(PotentialNext->ExternalEndTime <= LatestTimeSeen)
			{
				//this must be the results of a resim, so compare it to original results for divergence
				ProcessResimResult_External();
			}

			LatestTimeSeen = FMath::Max(LatestTimeSeen, PotentialNext->ExternalEndTime);
			return true;
		}
		
		return false;
	}

	/**
	 Collapse the whole pending queue inside a marshalling manager to one results object written to Results.Next

	 @param MarshallingManager Manger to advance
	 @param Results Results to update with advanced state
	*/
	void FChaosResultsManager::CollapseResultsToLatest()
	{
		if(Results.Next == nullptr)
		{
			//nothing in Next (first time), so get latest if possible
			Results.Next = MarshallingManager.PopPullData_External();
		}

		while(AdvanceResult())
		{}
	}

	const FChaosInterpolationResults& FChaosResultsManager::PullSyncPhysicsResults_External()
	{
		//sync mode doesn't use prev results, but if we were async previously we need to clean it up
		if (Results.Prev)
		{
			MarshallingManager.FreePullData_External(Results.Prev);
			Results.Prev = nullptr;
		}

		//either brand new, or we are consuming new results. Either way need to rebuild everything
		Results.Reset();

		// If we switched from async to sync we may have multiple pending results, so discard them all except latest.
		// If we dispatched substeps there will be multiple results pending but the latest is the one we want.
		CollapseResultsToLatest();

		if (Results.Next)
		{
			//whatever next ends up being, we mark the data as such
			SetPrevNextDataHelper<ESetPrevNextDataMode::Next>(*Results.Next);
			Results.Alpha = 1;
		}

		return Results;
	}

	FReal ComputeAlphaHelper(const FPullPhysicsData& Next, const FReal ResultsTime)
	{
		const FReal Denom = Next.ExternalEndTime - Next.ExternalStartTime;
		if (Denom > 0)	//if 0 dt just skip
		{
			//if we have no future results alpha will be > 1
			//in that case we just keep rendering the latest results we can
			return FMath::Min((FReal)1., (ResultsTime - Next.ExternalStartTime) / Denom);
		}

		return 1;	//if 0 dt just use 1 as alpha
	}

	FChaosResultsManager::FChaosResultsManager(FChaosMarshallingManager& InMarshallingManager)
		: MarshallingManager(InMarshallingManager)
	{
	}

	const FChaosInterpolationResults& FChaosResultsManager::UpdateInterpAlpha_External(const FReal ResultsTime, const FReal GlobalAlpha)
	{
		Results.Alpha = (float)GlobalAlpha;	 // LWC_TODO: Precision loss

		//make sure any resim interpolated bodies are still in the results array.
		//It's possible the body stopped moving after the resim and is not dirty, but we still want to interpolate to final place
		TArray<FSingleParticlePhysicsProxy*> FinishedSmoothing;
		for (const auto& Itr : ParticleToResimTarget)
		{
			FSingleParticlePhysicsProxy* Proxy = Itr.Key;

			if(Proxy->IsResimSmoothing())
			{
				if (Proxy->GetPullDataInterpIdx_External() == INDEX_NONE)	//not in results array
				{
					//still need to interpolate, so add to results array
					const int32 DataIdx = Results.RigidInterpolations.AddDefaulted(1);
					Proxy->SetPullDataInterpIdx_External(DataIdx);
					FChaosRigidInterpolationData& RigidData = Results.RigidInterpolations[DataIdx];

					RigidData.Next = Itr.Value;			//not dirty from sim, so just use whatever last next was
					RigidData.Prev = RigidData.Next;	//prev same as next since we're just using leash
				}
			}
			else
			{
				FinishedSmoothing.Add(Proxy);
			}
		}

		for(FSingleParticlePhysicsProxy* Proxy : FinishedSmoothing)
		{
			RemoveProxy_External(Proxy);
		}

		return Results;
	}

	const FChaosInterpolationResults& FChaosResultsManager::PullAsyncPhysicsResults_External(const FReal ResultsTime)
	{
		//in async mode we must interpolate between Start and End of a particular sim step, where ResultsTime is in the inclusive interval [Start, End]
		//to do this we need to keep the results of the previous sim step, which ends exactly when the next one starts
		//if no previous result exists, we use the existing GT data

		if(ResultsTime < 0)
		{
			return UpdateInterpAlpha_External(ResultsTime, 1);
		}


		//still need previous results, so rebuild them
		if (Results.Next && ResultsTime <= Results.Next->ExternalEndTime)
		{
			//already have results, just need to update alpha
			const FReal GlobalAlpha = ComputeAlphaHelper(*Results.Next, ResultsTime);
			return UpdateInterpAlpha_External(ResultsTime, GlobalAlpha);
		}

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
				if(!AdvanceResult())
				{
					break;
				}
			}
		}

		ensure(Results.Prev == nullptr || Results.Next != nullptr);	//we can never have a prev set when there isn't a next

		FReal GlobalAlpha = 1;
		if(Results.Next)
		{
			//whatever next ends up being, we mark the data as such
			SetPrevNextDataHelper<ESetPrevNextDataMode::Next>(*Results.Next);
			GlobalAlpha = ComputeAlphaHelper(*Results.Next, ResultsTime);
		}

		return UpdateInterpAlpha_External(ResultsTime, GlobalAlpha);
	}

	bool StateDiverged(const FDirtyRigidParticleData& A, const FDirtyRigidParticleData& B)
	{
		ensure(A.GetProxy() == B.GetProxy());
		return A.X != B.X || A.R != B.R || A.V != B.V || A.W != B.W || A.ObjectState != B.ObjectState;
	}

	void FChaosResultsManager::ProcessResimResult_External()
	{
		//make sure any proxy in the resim data is marked as resimming
		for(const FDirtyRigidParticleData& ResimDirty : Results.Next->DirtyRigids)
		{
			if (FSingleParticlePhysicsProxy* ResimProxy = ResimDirty.GetProxy())
			{
				ParticleToResimTarget.FindOrAdd(ResimProxy) = ResimDirty;
				ResimProxy->SetResimSmoothing(true);
			}
		}
	}

	FChaosResultsManager::~FChaosResultsManager()
	{
		if(Results.Prev)
		{
			MarshallingManager.FreePullData_External(Results.Prev);
		}

		if(Results.Next)
		{
			MarshallingManager.FreePullData_External(Results.Next);
		}
	}

	void FChaosResultsManager::RemoveProxy_External(FSingleParticlePhysicsProxy* Proxy)
	{
		ParticleToResimTarget.Remove(Proxy);
	}
}
