// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkSimulationModelCVars.h"

namespace NetworkInterpolationDebugCVars
{
	NETSIM_DEVCVAR_SHIPCONST_INT(Disable, 0, "ni.Disable", "Disables Network Interpolation");
}

class INetworkInterpolator
{
public:

	virtual ~INetworkInterpolator() { }
	virtual void Tick(float DeltaSeconds) = 0;
};

template<typename TNetworkedSimulationModel, typename TDriver>
class TNetworkSimulationModelInterpolator : public INetworkInterpolator
{
public:

	using TSimTime = typename TNetworkedSimulationModel::TSimTime;
	using TRealTime = typename TNetworkedSimulationModel::TRealTime;
	using TSyncState = typename TNetworkedSimulationModel::TSyncState;

	TNetworkSimulationModelInterpolator(TNetworkedSimulationModel* Source, TDriver* InDriver)
	{
		NetworkSim = Source;
		Driver = InDriver;
	}
	virtual ~TNetworkSimulationModelInterpolator()
	{

	}

	void Tick(float DeltaSeconds) override
	{
		if (NetworkInterpolationDebugCVars::Disable() > 0)
		{			
			if (const TSyncState* HeadState = NetworkSim->Buffers.Sync.GetElementFromHead(0))
			{
				Driver->FinalizeFrame(*HeadState);
			}
			return;
		}

		if (NetworkSim->TickInfo.SimulationTimeBuffer.GetNumValidElements() <= 1)
		{
			// Cant interpolate yet	
			return;
		}

		InterpolationTime += DeltaSeconds;

		// SimulatinoTimeBuffer holds the simulation time stamps of each SyncState in the SyncBuffer
		// (Note that things are not going to be evenly spaced time wise)
		auto& SimulationTimeBuffer = NetworkSim->TickInfo.SimulationTimeBuffer;		
		const int32 HeadKeyframe = SimulationTimeBuffer.GetHeadKeyframe();

		// Find which keyframes we should be interpolating between
		int32 InterpolationKeyframe = INDEX_NONE;
		TSimTime FromInterpolationTime;
		TSimTime ToInterpolationTime;

		for (int32 Keyframe = SimulationTimeBuffer.GetTailKeyframe(); Keyframe < HeadKeyframe; ++Keyframe)
		{
			TSimTime ElementSimTime = *SimulationTimeBuffer.FindElementByKeyframe(Keyframe);
			if (InterpolationTime > ElementSimTime.ToRealTimeSeconds())
			{
				InterpolationKeyframe = Keyframe;
				FromInterpolationTime = ElementSimTime;
			}
			else
			{
				break;
			}
		}

		auto ResetInterpolationTimeToMidpoint = [&](int32 Keyframe)
		{
			check(SimulationTimeBuffer.IsValidKeyframe(Keyframe));
			check(SimulationTimeBuffer.IsValidKeyframe(Keyframe+1));

			InterpolationKeyframe = Keyframe;
			
			FromInterpolationTime = *SimulationTimeBuffer.FindElementByKeyframe(Keyframe);
			ToInterpolationTime = *SimulationTimeBuffer.FindElementByKeyframe(Keyframe+1);
			InterpolationTime = (FromInterpolationTime.ToRealTimeSeconds() + ToInterpolationTime.ToRealTimeSeconds()) / 2.f;
		};

		if (InterpolationKeyframe == INDEX_NONE)
		{
			//UE_LOG(LogTemp, Warning, TEXT("FELL BEHIND. %s < %s"), *LexToString(InterpolationTime), *LexToString(SimulationTimeBuffer.GetElementFromTail(0)->ToRealTimeSeconds()));
			//ResetInterpolationTimeToMidpoint(SimulationTimeBuffer.GetTailKeyframe());
			ResetInterpolationTimeToMidpoint((SimulationTimeBuffer.GetTailKeyframe() + SimulationTimeBuffer.GetHeadKeyframe()) / 2);
		}
		else
		{
			ToInterpolationTime = *SimulationTimeBuffer.FindElementByKeyframe(InterpolationKeyframe + 1);
			if (InterpolationTime > ToInterpolationTime.ToRealTimeSeconds())
			{
				//UE_LOG(LogTemp, Warning, TEXT("GOT AHEAD. %s > %s"), *LexToString(InterpolationTime), *LexToString(ToInterpolationTime.ToRealTimeSeconds()));
				//ResetInterpolationTimeToMidpoint(SimulationTimeBuffer.GetHeadKeyframe()-1);
				ResetInterpolationTimeToMidpoint((SimulationTimeBuffer.GetTailKeyframe() + SimulationTimeBuffer.GetHeadKeyframe()) / 2);
			}
		}


		const TSyncState* FromState = NetworkSim->Buffers.Sync.FindElementByKeyframe(InterpolationKeyframe);
		const TSyncState* ToState = NetworkSim->Buffers.Sync.FindElementByKeyframe(InterpolationKeyframe+1);

		if (FromState && ToState)
		{
			const TRealTime FromRealTime = FromInterpolationTime.ToRealTimeSeconds();
			const TRealTime ToRealTime = ToInterpolationTime.ToRealTimeSeconds();
			const TRealTime InterpolationInterval = ToRealTime - FromRealTime;
		
			if (ensure(FMath::Abs(InterpolationInterval) > 0.f))
			{
				const float InterpolationPCT = (InterpolationTime - FromRealTime) / InterpolationInterval;
				ensureMsgf(InterpolationPCT >= 0.f && InterpolationPCT <= 1.f, TEXT("Calculated InterpolationPCT not in expected range. InterpolationTime: %s. From: %s. To: %s"), *LexToString(InterpolationTime), *LexToString(FromRealTime), *LexToString(ToRealTime));

				TSyncState InterpolatedState;
				TSyncState::Interpolate(*FromState, *ToState, InterpolationPCT, InterpolatedState);

				//UE_LOG(LogTemp, Warning, TEXT("Interp. [%d] %s"), ToInterpolationData->Keyframe,  *LexToString(InterpolationTime));

				Driver->FinalizeFrame(InterpolatedState);
			}
		}
		else
		{
			ensure(false);
		}

		//UE_LOG(LogTemp, Warning, TEXT("NumElements: [%d, %d]"), NetworkSim->Buffers.Sync.GetNumValidElements(), SimulationTimeBuffer.GetNumValidElements());

		/*
		InterpolationTime += DeltaSeconds;

		static TRealTime LastInterpolationTime = 0.f;		
		static TRealTime LastSimTime = 0.f;
		static TSimTime LastSimTimeMSec;

		TRealTime DeltaSim = NetworkSim->TickInfo.TotalProcessedSimulationTime.ToRealTimeSeconds() - LastSimTime;
		TRealTime DeltaInt = InterpolationTime - LastInterpolationTime;
		TSimTime DeltaSimMSec = NetworkSim->TickInfo.TotalProcessedSimulationTime - LastSimTimeMSec;
		

		TRealTime Delta = NetworkSim->TickInfo.TotalProcessedSimulationTime.ToRealTimeSeconds() - InterpolationTime;
		UE_LOG(LogTemp, Warning, TEXT("%s (%s)... %s (%s) - %s (%s). Delta: %s"), *NetworkSim->TickInfo.TotalProcessedSimulationTime.ToString(), *DeltaSimMSec.ToString(),
			*LexToString(NetworkSim->TickInfo.TotalProcessedSimulationTime.ToRealTimeSeconds()), *LexToString(DeltaSim), *LexToString(InterpolationTime), *LexToString(DeltaInt), *LexToString(Delta));

		if (InterpolationTime > NetworkSim->TickInfo.TotalProcessedSimulationTime.ToRealTimeSeconds())
		{
			InterpolationTime = NetworkSim->TickInfo.TotalProcessedSimulationTime.ToRealTimeSeconds();
			UE_LOG(LogTemp, Warning, TEXT("CLAMP! %s"), *LexToString(InterpolationTime));
		}

		LastInterpolationTime = InterpolationTime;
		LastSimTime = NetworkSim->TickInfo.TotalProcessedSimulationTime.ToRealTimeSeconds();
		LastSimTimeMSec = NetworkSim->TickInfo.TotalProcessedSimulationTime;

		if (true)
			return;

		*/
	}

private:

	const TNetworkedSimulationModel* NetworkSim;
	TDriver* Driver;
	
	TRealTime InterpolationTime = 0.f; // SimTime we are currently interpolating at

	TRealTime	MinBufferedTime = 1/120.f;
	TRealTime	MaxBufferedTime = 1.f;

};


class UNetworkInterpolationManager
{
public:

	virtual ~UNetworkInterpolationManager() { }


	virtual void Tick(float DeltaSeconds);
	
	void Register(INetworkInterpolator* Interpolater)
	{
		Interpolaters.Add(Interpolater);
	}

	
	void UnregisterNetworkSim(INetworkInterpolator* Interpolater)
	{
		Interpolaters.Remove(Interpolater);
	}

private:

	TArray<INetworkInterpolator*> Interpolaters;
};