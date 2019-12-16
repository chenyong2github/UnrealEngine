// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkedSimulationModelCVars.h"
#include "VisualLogger/VisualLogger.h"
#include "GameFramework/Actor.h"
#include "NetworkPredictionTypes.h"
#include "Engine/World.h"
#include "NetworkedSimulationModelTypes.h"
#include "NetworkedSimulationModelTick.h"

DEFINE_LOG_CATEGORY_STATIC(LogNetInterpolation, Log, All);

namespace NetworkInterpolationDebugCVars
{
	NETSIM_DEVCVAR_SHIPCONST_INT(Disable, 0, "ni.Disable", "Disables Network Interpolation");
	NETSIM_DEVCVAR_SHIPCONST_INT(VLog, 0, "ni.VLog", "Enables Network Interpolation VLog ");

	NETSIM_DEVCVAR_SHIPCONST_FLOAT(WaitSlack, 0.05, "ni.WaitSlack", "How much slack to wait for when waiting");

	NETSIM_DEVCVAR_SHIPCONST_FLOAT(CatchUpThreshold, 0.300, "ni.CatchUpThreshold", "When we start catching up (seconds from head)");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(CatchUpGoal, 0.010, "ni.CatchUpGoal", "When we stop cathcing up (seconds from head)");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(CatchUpFactor, 1.50, "ni.CatchUpFactor", "Factor we use to catch up");
}

template<typename TBufferTypes, typename TTickSettings>
struct TInterpolator
{
	using FSimTime = FNetworkSimTime;
	using FRealTime = FNetworkSimTime::FRealTime;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	bool bEnableVisualLog = true;

	template<typename TSystemDriver>
	void PostSimTick(TSystemDriver* Driver, const TNetworkSimBufferContainer<TBufferTypes>& Buffers, const FSimulationTickState& TickInfo, const FNetSimTickParameters& TickParameters)
	{
		const bool bDoVLog = NetworkInterpolationDebugCVars::VLog() && bEnableVisualLog;
		const float DeltaSeconds = TickParameters.LocalDeltaTimeSeconds;

		const AActor* LogOwner = Driver->GetVLogOwner();

		// Interpolation disabled
		if (NetworkInterpolationDebugCVars::Disable() > 0)
		{			
			if (const TSyncState* HeadState = Buffers.Sync.HeadElement())
			{
				const TAuxState* AuxState = Buffers.Aux.HeadElement();
				check(AuxState);

				Driver->FinalizeFrame(*HeadState, *AuxState);
			}
			return;
		}
		
		if (TickInfo.SimulationTimeBuffer.Num() <= 1)
		{
			// Cant interpolate yet	
			return;
		}

		auto& SimulationTimeBuffer = TickInfo.SimulationTimeBuffer;

		// Starting off: start at the tail end
		if (InterpolationTime <= 0.f)
		{
			InterpolationTime = SimulationTimeBuffer.TailElement()->ToRealTimeSeconds();
			InterpolationFrame = SimulationTimeBuffer.TailFrame();
			InterpolationState = *Buffers.Sync.TailElement();
		}

		// Wait if we were too far ahead
		if (WaitUntilTime > 0.f)
		{
			if (WaitUntilTime <= SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds())
			{
				// done waiting, we can continue
				WaitUntilTime = 0.f;
				UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Done Waiting! Head: %s"), *LexToString(SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds()));
			}
			else
			{
				if (bDoVLog)
				{
					// Still waiting, return
					UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Still Waiting! %s < %s"), *LexToString(WaitUntilTime), *LexToString(SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds()));

					const FName LocalInterpolationTimeName("Local Interpolation Time");
					FVector2D LocalTimeVsInterpolationTime(LogOwner->GetWorld()->GetTimeSeconds(), (int32)(InterpolationTime * 1000.f));
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, "ServerSimulationTimeGraph", LocalInterpolationTimeName, LocalTimeVsInterpolationTime);
				}
				return;
			}
		}

		EVisualLoggingContext LoggingContext = EVisualLoggingContext::InterpolationLatest;

		// Calc new interpolation time
		FRealTime NewInterpolationTime = InterpolationTime;
		{
			FRealTime Step = DeltaSeconds;
			
			// Speed up if we are too far behind
			FRealTime CatchUpThreshold = SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::CatchUpThreshold();
			if (CatchUpUntilTime <= 0.f && InterpolationTime < CatchUpThreshold)
			{
				CatchUpUntilTime = SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::CatchUpGoal();
			}

			if (CatchUpUntilTime > 0.f)
			{
				if (InterpolationTime  < CatchUpUntilTime)
				{
					Step *= NetworkInterpolationDebugCVars::CatchUpFactor();
					LoggingContext = EVisualLoggingContext::InterpolationSpeedUp;

					UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Catching up! %s < %s"), *LexToString(InterpolationTime), *LexToString(CatchUpUntilTime));
					//!UE_LOG(LogNetInterpolation, Warning, TEXT("Catching up! %s < %s"), *LexToString(InterpolationTime), *LexToString(CatchUpUntilTime));
				}
				else
				{
					CatchUpUntilTime = 0.f;
				}
			}

			NewInterpolationTime += Step;

			// Did this put us too far ahead, and now we need to start waiting?
			if (NewInterpolationTime > SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds())
			{
				UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Too far ahead! Starting to wait! Head: %s"), *LexToString(SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds()));
				WaitUntilTime = SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds() + NetworkInterpolationDebugCVars::WaitSlack();
				NewInterpolationTime = SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds();
			}
		}

		// Find "To" frame
		const TSyncState* ToState = nullptr;
		const TAuxState* AuxState = nullptr;
		FRealTime ToTime = 0.f;

		for (auto It = SimulationTimeBuffer.CreateConstIterator(); It; ++It)
		{
			FSimTime ElementSimTime = *It.Element();
			if (NewInterpolationTime <= ElementSimTime.ToRealTimeSeconds())
			{
				InterpolationFrame = It.Frame();
				ToTime = ElementSimTime.ToRealTimeSeconds();
				ToState = Buffers.Sync[It.Frame()];
				AuxState = Buffers.Aux[It.Frame()];
				break;
			}
		}

		if (ensure(ToState && AuxState))
		{
			const FRealTime FromRealTime = InterpolationTime;
			const FRealTime ToRealTime = ToTime;
			const FRealTime InterpolationInterval = ToRealTime - FromRealTime;
		
			if (ensure(FMath::Abs(InterpolationInterval) > 0.f))
			{
				const float InterpolationPCT = (NewInterpolationTime - FromRealTime) / InterpolationInterval;
				ensureMsgf(InterpolationPCT >= 0.f && InterpolationPCT <= 1.f, TEXT("Calculated InterpolationPCT not in expected range. NewInterpolationTime: %s. From: %s. To: %s"), *LexToString(NewInterpolationTime), *LexToString(FromRealTime), *LexToString(ToRealTime));

				TSyncState NewInterpolatedState;
				TSyncState::Interpolate(InterpolationState, *ToState, InterpolationPCT, NewInterpolatedState);

				Driver->FinalizeFrame(NewInterpolatedState, *AuxState);
				
				if (bDoVLog)
				{
					UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("%s - %s - %s.  InterpolationPCT: %f"), *LexToString(FromRealTime), *LexToString(NewInterpolationTime), *LexToString(ToRealTime), InterpolationPCT);

					// Graph Interpolation Time vs Buffer Head/Tail times
					const FName ServerSimulationGraphName("ServerSimulationTimeGraph");
					const FName ServerSimTimeName("Server Simulation Time");
					FVector2D LocalTimeVsServerSimTime(LogOwner->GetWorld()->GetTimeSeconds(), (int32)TickInfo.SimulationTimeBuffer.HeadElement()->ToRealTimeMS());
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, ServerSimTimeName, LocalTimeVsServerSimTime);

					const FName BufferTailSimTimeName("Buffer Tail Simulation Time");
					FVector2D LocalTimeVsBufferTailSim(LogOwner->GetWorld()->GetTimeSeconds(), (int32)TickInfo.SimulationTimeBuffer.TailElement()->ToRealTimeMS());
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, BufferTailSimTimeName, LocalTimeVsBufferTailSim);

					const FName LocalInterpolationTimeName("Local Interpolation Time");
					FVector2D LocalTimeVsInterpolationTime(LogOwner->GetWorld()->GetTimeSeconds(), (int32)(NewInterpolationTime * 1000.f));
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, LocalInterpolationTimeName, LocalTimeVsInterpolationTime);
					
					FVector2D LocalTimeVsCatchUpThreshold(LogOwner->GetWorld()->GetTimeSeconds(), (SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::CatchUpThreshold()) * 1000.f);
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, "Catch Up Threshold", LocalTimeVsCatchUpThreshold);

					FVector2D LocalTimeVsCatchUpGoal(LogOwner->GetWorld()->GetTimeSeconds(), (SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::CatchUpGoal()) * 1000.f);
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, "Catch Up Goal", LocalTimeVsCatchUpGoal);

					// VLog the actual motion states
					const TSyncState* DebugTail = Buffers.Sync.TailElement();
					const TSyncState* DebugHead = Buffers.Sync.HeadElement();

					auto VLogHelper = [&](int32 Frame, EVisualLoggingContext Context)
					{
						FVisualLoggingParameters VLogParams(Context, Frame, EVisualLoggingLifetime::Transient);
						Driver->VisualLog(Buffers.Input[Frame], Buffers.Sync[Frame], Buffers.Aux[Frame], VLogParams);
					};

					VLogHelper(Buffers.Sync.TailFrame(), EVisualLoggingContext::InterpolationBufferTail);
					VLogHelper(Buffers.Sync.HeadFrame(), EVisualLoggingContext::InterpolationBufferHead);

					{
						FVisualLoggingParameters VLogParams(EVisualLoggingContext::InterpolationFrom, InterpolationFrame-1, EVisualLoggingLifetime::Transient);
						Driver->VisualLog(Buffers.Input[InterpolationFrame-1], &InterpolationState, Buffers.Aux[InterpolationFrame-1], VLogParams);
					}

					{
						FVisualLoggingParameters VLogParams(EVisualLoggingContext::InterpolationTo, InterpolationFrame, EVisualLoggingLifetime::Transient);
						Driver->VisualLog(Buffers.Input[InterpolationFrame], &InterpolationState, Buffers.Aux[InterpolationFrame], VLogParams);
					}

					{
						FVisualLoggingParameters VLogParams(LoggingContext, InterpolationFrame, EVisualLoggingLifetime::Transient);
						Driver->VisualLog(Buffers.Input[InterpolationFrame], &NewInterpolatedState, Buffers.Aux[InterpolationFrame], VLogParams);
					}
				}

				InterpolationState = NewInterpolatedState;
				InterpolationTime = NewInterpolationTime;
			}
		}
	}

private:

	FRealTime InterpolationTime = 0.f; // SimTime we are currently interpolating at
	int32 InterpolationFrame = INDEX_NONE; // Frame we are currently/last interpolated at

	TSyncState InterpolationState;

	FRealTime WaitUntilTime = 0.f;	

	FRealTime CatchUpUntilTime = 0.f;


	FRealTime DynamicBufferedTime = 1/60.f; // SimTime we are currently interpolating at
	FRealTime DynamicBufferedTimeStep = 1/60.f;

	FRealTime	MinBufferedTime = 1/120.f;
	FRealTime	MaxBufferedTime = 1.f;
};