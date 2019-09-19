// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkSimulationModelCVars.h"
#include "VisualLogger/VisualLogger.h"
#include "GameFramework/Actor.h"
#include "NetworkPredictionTypes.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogNetInterpolation, Log, All);

namespace NetworkInterpolationDebugCVars
{
	NETSIM_DEVCVAR_SHIPCONST_INT(Disable, 0, "ni.Disable", "Disables Network Interpolation");
	NETSIM_DEVCVAR_SHIPCONST_INT(VLog, 1, "ni.VLog", "Enables Network Interpolation VLog ");

	NETSIM_DEVCVAR_SHIPCONST_FLOAT(WaitSlack, 0.05, "ni.WaitSlack", "How much slack to wait for when waiting");

	NETSIM_DEVCVAR_SHIPCONST_FLOAT(CatchUpThreshold, 0.200, "ni.CatchUpThreshold", "When we start catching up (seconds from head)");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(CatchUpGoal, 0.100, "ni.CatchUpGoal", "When we stop cathcing up (seconds from head)");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(CatchUpFactor, 1.25, "ni.CatchUpFactor", "Factor we use to catch up");
}


class INetworkInterpolator
{
public:

	virtual ~INetworkInterpolator() { }
	virtual void Tick(float DeltaSeconds, AActor* LogOwner) = 0;
};

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	TNetworkSimulationModelInterpolator
//	The interpolator is responsible for smoothly interpolating between known Sync States. The output of the interpolator does NOT feed back into the simulation!
//	This is still a WIP and not where we want it to be. Some notes about future improvements:
//	-We should take a data oriented approach and have an interpolation manager than can more efficiently bulk process interpolations
//	-We should have interpolation settings instead of cvars and in general improve the dynamic-ness of the interpolation algorithm.
//	-The tricky part here is that we cannot rely on consistent replication frequency and frame rates. We want the interpolator to handle any network conditions and scale accordingly.
//	-E.g, smooth motion, but minimize added application latency.
//
//	Also note: the intention is not for every actor using NetworkPrediction to necessarily be interpolated. Simulated Extrapolation is still a fine option.
//
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
template<typename TNetworkedSimulationModel, typename TDriver>
class TNetworkSimulationModelInterpolator : public INetworkInterpolator
{
public:

	using TSimTime = typename TNetworkedSimulationModel::TSimTime;
	using TRealTime = typename TNetworkedSimulationModel::TRealTime;
	using TSyncState = typename TNetworkedSimulationModel::TSyncState;

	bool bEnableVisualLog = true;

	TNetworkSimulationModelInterpolator(TNetworkedSimulationModel* Source, TDriver* InDriver)
	{
		NetworkSim = Source;
		Driver = InDriver;
	}
	virtual ~TNetworkSimulationModelInterpolator()
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


	}

	void Tick(float DeltaSeconds, AActor* LogOwner) override
	{
		const bool bDoVLog = NetworkInterpolationDebugCVars::VLog() && bEnableVisualLog;

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


		auto& SimulationTimeBuffer = NetworkSim->TickInfo.SimulationTimeBuffer;

		// Starting off: start at the tail end
		if (InterpolationTime <= 0.f)
		{
			InterpolationTime = SimulationTimeBuffer.GetElementFromTail(0)->ToRealTimeSeconds();
			InterpolationKeyframe = SimulationTimeBuffer.GetTailKeyframe();
			InterpolationState = *NetworkSim->Buffers.Sync.GetElementFromTail(0);
		}

		// Wait if we were too far ahead
		if (WaitUntilTime > 0.f)
		{
			if (WaitUntilTime <= SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds())
			{
				// done waiting, we can continue
				WaitUntilTime = 0.f;
				UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Done Waiting! Head: %s"), *LexToString(SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds()));
			}
			else
			{
				if (bDoVLog)
				{
					// Still waiting, return
					UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Still Waiting! %s < %s"), *LexToString(WaitUntilTime), *LexToString(SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds()));

					const FName LocalInterpolationTimeName("Local Interpolation Time");
					FVector2D LocalTimeVsInterpolationTime(LogOwner->GetWorld()->GetTimeSeconds(), (int32)(InterpolationTime * 1000.f));
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, "ServerSimulationTimeGraph", LocalInterpolationTimeName, LocalTimeVsInterpolationTime);
				}
				return;
			}
		}

		EVisualLoggingContext LoggingContext = EVisualLoggingContext::InterpolationLatest;

		// Calc new interpolation time
		TRealTime NewInterpolationTime = InterpolationTime;
		{
			TRealTime Step = DeltaSeconds;
			
			// Speed up if we are too far behind
			TRealTime CatchUpThreshold = SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::CatchUpThreshold();
			if (CatchUpUntilTime <= 0.f && InterpolationTime < CatchUpThreshold)
			{
				CatchUpUntilTime = SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::CatchUpGoal();
			}

			if (CatchUpUntilTime > 0.f)
			{
				if (InterpolationTime  < CatchUpUntilTime)
				{
					Step *= NetworkInterpolationDebugCVars::CatchUpFactor();
					LoggingContext = EVisualLoggingContext::InterpolationSpeedUp;

					UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Catching up! %s < %s"), *LexToString(InterpolationTime), *LexToString(CatchUpUntilTime));
					UE_LOG(LogNetInterpolation, Warning, TEXT("Catching up! %s < %s"), *LexToString(InterpolationTime), *LexToString(CatchUpUntilTime));
				}
				else
				{
					CatchUpUntilTime = 0.f;
				}
			}

			NewInterpolationTime += Step;

			// Did this put us too far ahead, and now we need to start waiting?
			if (NewInterpolationTime > SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds())
			{
				UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Too far ahead! Starting to wait! Head: %s"), *LexToString(SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds()));
				WaitUntilTime = SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds() + NetworkInterpolationDebugCVars::WaitSlack();
				NewInterpolationTime = SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds();
			}
		}

		// Find "To" keyframe
		const TSyncState* ToState = nullptr;
		TRealTime ToTime = 0.f;

		for (int32 Keyframe = SimulationTimeBuffer.GetTailKeyframe(); Keyframe <= SimulationTimeBuffer.GetHeadKeyframe(); ++Keyframe)
		{
			TSimTime ElementSimTime = *SimulationTimeBuffer.FindElementByKeyframe(Keyframe);
			if (NewInterpolationTime <= ElementSimTime.ToRealTimeSeconds())
			{
				InterpolationKeyframe = Keyframe;
				ToTime = ElementSimTime.ToRealTimeSeconds();
				ToState = NetworkSim->Buffers.Sync.FindElementByKeyframe(Keyframe);
				break;
			}
		}

		if (ensure(ToState))
		{
			const TRealTime FromRealTime = InterpolationTime;
			const TRealTime ToRealTime = ToTime;
			const TRealTime InterpolationInterval = ToRealTime - FromRealTime;
		
			if (ensure(FMath::Abs(InterpolationInterval) > 0.f))
			{
				const float InterpolationPCT = (NewInterpolationTime - FromRealTime) / InterpolationInterval;
				ensureMsgf(InterpolationPCT >= 0.f && InterpolationPCT <= 1.f, TEXT("Calculated InterpolationPCT not in expected range. NewInterpolationTime: %s. From: %s. To: %s"), *LexToString(NewInterpolationTime), *LexToString(FromRealTime), *LexToString(ToRealTime));

				TSyncState NewInterpolatedState;
				TSyncState::Interpolate(InterpolationState, *ToState, InterpolationPCT, NewInterpolatedState);

				Driver->FinalizeFrame(NewInterpolatedState);
				
				if (bDoVLog)
				{
					UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("%s - %s - %s.  InterpolationPCT: %f"), *LexToString(FromRealTime), *LexToString(NewInterpolationTime), *LexToString(ToRealTime), InterpolationPCT);

					// Graph Interpolation Time vs Buffer Head/Tail times
					const FName ServerSimulationGraphName("ServerSimulationTimeGraph");
					const FName ServerSimTimeName("Server Simulation Time");
					FVector2D LocalTimeVsServerSimTime(LogOwner->GetWorld()->GetTimeSeconds(), (int32)NetworkSim->TickInfo.SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeMS());
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, ServerSimTimeName, LocalTimeVsServerSimTime);

					const FName BufferTailSimTimeName("Buffer Tail Simulation Time");
					FVector2D LocalTimeVsBufferTailSim(LogOwner->GetWorld()->GetTimeSeconds(), (int32)NetworkSim->TickInfo.SimulationTimeBuffer.GetElementFromTail(0)->ToRealTimeMS());
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, BufferTailSimTimeName, LocalTimeVsBufferTailSim);

					const FName LocalInterpolationTimeName("Local Interpolation Time");
					FVector2D LocalTimeVsInterpolationTime(LogOwner->GetWorld()->GetTimeSeconds(), (int32)(NewInterpolationTime * 1000.f));
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, LocalInterpolationTimeName, LocalTimeVsInterpolationTime);
					
					FVector2D LocalTimeVsCatchUpThreshold(LogOwner->GetWorld()->GetTimeSeconds(), (SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::CatchUpThreshold()) * 1000.f);
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, "Catch Up Threshold", LocalTimeVsCatchUpThreshold);

					FVector2D LocalTimeVsCatchUpGoal(LogOwner->GetWorld()->GetTimeSeconds(), (SimulationTimeBuffer.GetElementFromHead(0)->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::CatchUpGoal()) * 1000.f);
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, "Catch Up Goal", LocalTimeVsCatchUpGoal);

					// VLog the actual motion states
					const TSyncState* DebugTail = NetworkSim->Buffers.Sync.GetElementFromTail(0);
					const TSyncState* DebugHead = NetworkSim->Buffers.Sync.GetElementFromHead(0);

					DebugTail->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::InterpolationBufferTail, NetworkSim->Buffers.Sync.GetTailKeyframe(), EVisualLoggingLifetime::Transient), Driver, Driver );
					DebugHead->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::InterpolationBufferHead, NetworkSim->Buffers.Sync.GetHeadKeyframe(), EVisualLoggingLifetime::Transient), Driver, Driver );

					InterpolationState.VisualLog( FVisualLoggingParameters(EVisualLoggingContext::InterpolationFrom, InterpolationKeyframe, EVisualLoggingLifetime::Transient), Driver, Driver );
					ToState->VisualLog( FVisualLoggingParameters(EVisualLoggingContext::InterpolationTo, InterpolationKeyframe, EVisualLoggingLifetime::Transient), Driver, Driver );

					NewInterpolatedState.VisualLog( FVisualLoggingParameters(LoggingContext, InterpolationKeyframe, EVisualLoggingLifetime::Transient), Driver, Driver );
				}


				InterpolationState = NewInterpolatedState;
				InterpolationTime = NewInterpolationTime;
			}
		}
	}

private:

	const TNetworkedSimulationModel* NetworkSim;
	TDriver* Driver;
	
	TRealTime InterpolationTime = 0.f; // SimTime we are currently interpolating at
	int32 InterpolationKeyframe = INDEX_NONE; // Keyframe we are currently/last interpolated at

	TSyncState InterpolationState;

	TRealTime WaitUntilTime = 0.f;	

	TRealTime CatchUpUntilTime = 0.f;


	TRealTime DynamicBufferedTime = 1/60.f; // SimTime we are currently interpolating at
	TRealTime DynamicBufferedTimeStep = 1/60.f;

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