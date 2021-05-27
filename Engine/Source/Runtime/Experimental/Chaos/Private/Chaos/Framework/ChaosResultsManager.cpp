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
				if(FResimParticleInfo* ResimInfo = ParticleToResimInfo.Find(Proxy))
				{
					ResimInfo->Next = Data;
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
				FreeToHistory_External(Results.Prev);
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
			FreeToHistory_External(Results.Prev);
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

	FRealSingle DefaultResimInterpTime = 1.f;
	FAutoConsoleVariableRef CVarResimInterpTime(TEXT("p.ResimInterpTime"), DefaultResimInterpTime, TEXT("How long to interpolate between original sim and resim results. 0 means no interpolation, the larget the value the smoother and longer interpolation takes. Restart game to see affect"));

	FRealSingle DefaultResimInterpStrength = 0.2f;
	FAutoConsoleVariableRef CVarResimInterpStrength(TEXT("p.ResimInterpStrength"), DefaultResimInterpStrength, TEXT("How strong the resim interp leash is. 1 means immediately snap to new target, 0 means do not interpolate at all"));

	FChaosResultsManager::FChaosResultsManager(FChaosMarshallingManager& InMarshallingManager)
		: MarshallingManager(InMarshallingManager)
	{
		SetResimInterpTime(DefaultResimInterpTime);
		SetResimInterpStrength(DefaultResimInterpStrength);
	}

	void FChaosResultsManager::SetResimInterpTime(const FReal InterpTime)
	{
		if(InterpTime <= 0)
		{
			InvResimInterpTime = FLT_MAX;
		}
		else
		{
			InvResimInterpTime = 1 / InterpTime;
		}
	}

	const FChaosInterpolationResults& FChaosResultsManager::UpdateInterpAlpha_External(const FReal ResultsTime, const FReal GlobalAlpha)
	{
		auto ComputeLeashAlphaHelper = [this, ResultsTime](const FReal LatestDivergeTime)
		{
			
			return FMath::Min((FReal)1, (ResultsTime - LatestDivergeTime) * InvResimInterpTime);
		};

		Results.Alpha = (float)GlobalAlpha;	 // LWC_TODO: Precision loss

		if(ParticleToResimInfo.Num() == 0)	//no resim interpolation so just exit
		{
			return Results;
		}

		//first update all interpolations that were dirtied by sim results
		for(FChaosRigidInterpolationData& RigidData : Results.RigidInterpolations)
		{
			if(FSingleParticlePhysicsProxy* Proxy = RigidData.Prev.GetProxy())	//don't care about pending deleted proxies
			{
				RigidData.LeashAlpha = 1;	//if no resim interp just use global alpha
				if (FResimParticleInfo* LeashInfo = ParticleToResimInfo.Find(RigidData.Prev.GetProxy()))
				{
					const FReal LeashAlpha = ComputeLeashAlphaHelper(LeashInfo->LeashStartTime);
					if (LeashAlpha >= 1)
					{
						ParticleToResimInfo.Remove(Proxy);	//no longer interpolating
					}
					else
					{
						//still in leash mode so let interpolator know
						if(LeashInfo->bDiverged)
						{
							//RigidData.LeashAlpha = LeashAlpha;
							RigidData.LeashAlpha = (float)ResimInterpStrength;		 // LWC_TODO: Precision loss
						}
					}
				}
			}
		}
		
		//make sure any resim interpolated bodies are still in the results array.
		//It's possible the body stopped moving after the resim and is not dirty, but we still want to interpolate to final place
		TArray<FSingleParticlePhysicsProxy*> NoLongerInterpolating;
		for (const auto& Itr : ParticleToResimInfo)
		{
			FSingleParticlePhysicsProxy* Proxy = Itr.Key;
			if (Proxy->GetPullDataInterpIdx_External() == INDEX_NONE)	//not in results array
			{
				const FReal LeashAlpha = ComputeLeashAlphaHelper(Itr.Value.LeashStartTime);

				if(Itr.Value.bDiverged)
				{
					//still need to interpolate, so add to results array
					//even if alpha > 1 we want to snap to end of interpolation (i.e. the leash target)
					const int32 DataIdx = Results.RigidInterpolations.AddDefaulted(1);
					Proxy->SetPullDataInterpIdx_External(DataIdx);
					FChaosRigidInterpolationData& RigidData = Results.RigidInterpolations[DataIdx];

					RigidData.Next = Itr.Value.Next;	//not dirty from sim, so just use whatever last next was
					RigidData.Prev = RigidData.Next;	//prev same as next since we're just using leash
					RigidData.LeashAlpha = LeashAlpha < 1 ? (float)ResimInterpStrength : 1;	//if last step with leash just snap to end		 // LWC_TODO: Precision loss
				}
				
				if (LeashAlpha >= 1)	//leash will snap to end so not needed after this
				{
					NoLongerInterpolating.Add(Proxy);
				}
			}
		}
		
		for(FSingleParticlePhysicsProxy* Proxy : NoLongerInterpolating)
		{
			ParticleToResimInfo.Remove(Proxy);
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
		//find original pull data to compare to
		FPullPhysicsData* OriginalData = nullptr;
		if(Results.Next->ExternalEndTime == Results.Prev->ExternalEndTime)
		{
			OriginalData = Results.Prev;
		}
		else
		{
			for (int32 Idx = 0; Idx < ResultsHistory.Num(); ++Idx)
			{
				if(Results.Next->ExternalEndTime == ResultsHistory[Idx]->ExternalEndTime)
				{
					OriginalData = ResultsHistory[Idx];
					break;
				}
			}
		}
		ensure(OriginalData != nullptr);	//rewind happened, but our history buffer doesn't contain the original result

		if(OriginalData)
		{
			//First record the potential target for anything that is dirty from resim
			//During the first frame of the rewind, everything that changed must be dirty
			for (const FDirtyRigidParticleData& ResimDirty : Results.Next->DirtyRigids)
			{
				if (FSingleParticlePhysicsProxy* ResimProxy = ResimDirty.GetProxy())
				{
					FResimParticleInfo& ResimInfo = ParticleToResimInfo.FindOrAdd(ResimProxy);
					ResimInfo.bDiverged = true;	//If not in original data then diverged, otherwise will be reset below
					ResimInfo.LeashStartTime = LatestTimeSeen;
					ResimInfo.EntryTime = Results.Next->ExternalEndTime;
					ResimInfo.Next = ResimDirty;
				}
			}
			
			//Then check if anything diverged from original data
			//If so, turn leash mode on (i.e. mark it as diverged)
			for (const FDirtyRigidParticleData& OriginalDirty : OriginalData->DirtyRigids)
			{
				if (FSingleParticlePhysicsProxy* OriginalProxy = OriginalDirty.GetProxy())
				{
					//During rewind we have to update game thread of original rewind
					//Because of that, anything in the original data must also be in the first frame
					//of the resim dirty data
					//Without this we don't have the target data which means we don't know what to interpolate to
					FResimParticleInfo* ResimInfo = ParticleToResimInfo.Find(OriginalProxy);
					if(ensure(ResimInfo))
					{
						ResimInfo->bDiverged = (ResimInfo->EntryTime == OriginalData->ExternalEndTime) ? StateDiverged(ResimInfo->Next, OriginalDirty) : true;
						if(ResimInfo->bDiverged)
						{
							//We still use resim's latest target (i.e. maybe it stopped moving a few frames ago)
							//But the time is updated since we want the leash to be turned on for x seconds
							//(We want to record latest moment of divergence, not moment when resim data was recorded)
							ResimInfo->EntryTime = Results.Next->ExternalEndTime;
							ResimInfo->LeashStartTime = LatestTimeSeen;
						
						}
					}
				}
			}
		}
	}

	void FChaosResultsManager::FreeToHistory_External(FPullPhysicsData* PullData)
	{
		ResultsHistory.Insert(PullData, 0);	//store in reverse order (most recent data first)
		SetHistoryLength_External(HistoryLength);
	}

	void FChaosResultsManager::SetHistoryLength_External(int32 InLength)
	{
		while(ResultsHistory.Num() > InLength)
		{
			MarshallingManager.FreePullData_External(ResultsHistory.Pop());	//oldest data is last so can just pop it
		}

		HistoryLength = InLength;
	}

	FChaosResultsManager::~FChaosResultsManager()
	{
		SetHistoryLength_External(0);
	}

	void FChaosResultsManager::RemoveProxy_External(FSingleParticlePhysicsProxy* Proxy)
	{
		ParticleToResimInfo.Remove(Proxy);
	}
}
