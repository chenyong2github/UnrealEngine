// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkedSimulationModelCVars.h"
#include "VisualLogger/VisualLogger.h"
#include "GameFramework/Actor.h"
#include "NetworkPredictionTypes.h"
#include "Engine/World.h"
#include "NetworkedSimulationModelTypes.h"
#include "NetworkedSimulationModelTick.h"
#include "NetworkedSimulationModelTraits.h"

DEFINE_LOG_CATEGORY_STATIC(LogNetInterpolation, Log, All);

namespace NetworkInterpolationDebugCVars
{
	NETSIM_DEVCVAR_SHIPCONST_INT(Disable, 0, "ni.Disable", "Disables Network Interpolation");
	NETSIM_DEVCVAR_SHIPCONST_INT(VLog, 0, "ni.VLog", "Enables Network Interpolation VLog ");

	NETSIM_DEVCVAR_SHIPCONST_FLOAT(WaitSlack, 0.05, "ni.WaitSlack", "How much slack to wait for when waiting");

	// Catchup means "interpolate faster than realtime"
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(CatchUpThreshold, 0.300, "ni.CatchUpThreshold", "When we start catching up (seconds from head)");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(CatchUpGoal, 0.010, "ni.CatchUpGoal", "When we stop catching up (seconds from head)");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(CatchUpFactor, 1.50, "ni.CatchUpFactor", "Factor we use to catch up");

	// Snap means "we are too far behind, just jump to a specific point"
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(SnapThreshold, 0.500, "ni.SnapThreshold", "If we are < this, we will snap");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(SnapGoal, 0.010, "ni.SnapGoal", "Where we snap to");
}

template<typename TSync, typename TAux>
struct TInterpolatorParameters
{
	template<typename S, typename A>
	struct TStatePair
	{
		S& Sync;
		A& Aux;
	};

	using TInputPair = TStatePair<const TSync, const TAux>;
	using TOutputPair = TStatePair<TSync, TAux>;

	TInputPair From;
	TInputPair To;
	float InterpolationPCT;
	TOutputPair Out;

	template<typename TS, typename TA>
	TInterpolatorParameters<TS, TA> Cast() const { return { {From.Sync, From.Aux}, {To.Sync, To.Aux}, InterpolationPCT, {Out.Sync, Out.Aux} }; }
};

template<typename Model>
struct TNetSimInterpolator
{
	using TBufferTypes = typename TNetSimModelTraits<Model>::InternalBufferTypes;
	using TTickSettings = typename Model::TickSettings;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	using FSimTime = FNetworkSimTime;
	using FRealTime = FNetworkSimTime::FRealTime;

	struct FStatePair
	{
		TSyncState Sync;
		TAuxState Aux;
	};

	bool bEnableVisualLog = true;

	template<typename TSystemDriver>
	FRealTime PostSimTick(TSystemDriver* Driver, const TNetworkSimBufferContainer<Model>& Buffers, const FSimulationTickState& TickInfo, const FNetSimTickParameters& TickParameters)
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
			return TickInfo.GetTotalProcessedSimulationTime().ToRealTimeSeconds();
		}
		
		if (TickInfo.SimulationTimeBuffer.Num() <= 1)
		{
			// Cant interpolate yet	
			return 0.f;
		}

		auto& SimulationTimeBuffer = TickInfo.SimulationTimeBuffer;

		// Starting off: start at the tail end
		if (InterpolationTime <= 0.f)
		{
			InterpolationTime = SimulationTimeBuffer.TailElement()->ToRealTimeSeconds();
			InterpolationFrame = SimulationTimeBuffer.TailFrame();

			auto& FromState = GetFromInterpolationState();
			FromState.Sync = *Buffers.Sync[InterpolationFrame];
			FromState.Aux = *Buffers.Aux[InterpolationFrame];
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
				return InterpolationTime;
			}
		}

		EVisualLoggingContext LoggingContext = EVisualLoggingContext::InterpolationLatest;

		// Calc new interpolation time
		FRealTime NewInterpolationTime = InterpolationTime;
		{
			FRealTime Step = DeltaSeconds;

			// Snap if way far behind
			FRealTime SnapThreshold = SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::SnapThreshold();
			if (NewInterpolationTime < SnapThreshold)
			{
				NewInterpolationTime = SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::SnapGoal();
				UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Snapping to %s"), *LexToString(NewInterpolationTime));
			}
			
			// Speed up if we are too far behind
			FRealTime CatchUpThreshold = SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::CatchUpThreshold();
			if (CatchUpUntilTime <= 0.f && NewInterpolationTime < CatchUpThreshold)
			{
				CatchUpUntilTime = SimulationTimeBuffer.HeadElement()->ToRealTimeSeconds() - NetworkInterpolationDebugCVars::CatchUpGoal();
			}

			if (CatchUpUntilTime > 0.f)
			{
				if (NewInterpolationTime  < CatchUpUntilTime)
				{
					Step *= NetworkInterpolationDebugCVars::CatchUpFactor();
					LoggingContext = EVisualLoggingContext::InterpolationSpeedUp;

					UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Catching up! %s < %s"), *LexToString(NewInterpolationTime), *LexToString(CatchUpUntilTime));
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
		const TAuxState* ToAuxState = nullptr;
		FRealTime ToTime = 0.f;

		for (auto It = SimulationTimeBuffer.CreateConstIterator(); It; ++It)
		{
			FSimTime ElementSimTime = *It.Element();
			if (NewInterpolationTime <= ElementSimTime.ToRealTimeSeconds())
			{
				InterpolationFrame = It.Frame();
				ToTime = ElementSimTime.ToRealTimeSeconds();
				ToState = Buffers.Sync[It.Frame()];
				ToAuxState = Buffers.Aux[It.Frame()];
				break;
			}
		}

		if (ensure(ToState && ToAuxState))
		{
			const FRealTime FromRealTime = InterpolationTime;
			const FRealTime ToRealTime = ToTime;
			const FRealTime InterpolationInterval = ToRealTime - FromRealTime;
		
			if (ensure(FMath::Abs(InterpolationInterval) > 0.f))
			{
				const float InterpolationPCT = (NewInterpolationTime - FromRealTime) / InterpolationInterval;
				ensureMsgf(InterpolationPCT >= 0.f && InterpolationPCT <= 1.f, TEXT("Calculated InterpolationPCT not in expected range. NewInterpolationTime: %s. From: %s. To: %s"), *LexToString(NewInterpolationTime), *LexToString(FromRealTime), *LexToString(ToRealTime));

				auto& FromState = GetFromInterpolationState();
				auto& OutputState = GetNextInterpolationState();

				Model::Interpolate({ { FromState.Sync, FromState.Aux }, { *ToState, *ToAuxState }, InterpolationPCT, { OutputState.Sync, OutputState.Aux } });
				Driver->FinalizeFrame(OutputState.Sync, OutputState.Aux);
				
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

					auto VLogHelper = [&](int32 Frame, EVisualLoggingContext Context, const FString& DebugStr)
					{
						FVisualLoggingParameters VLogParams(Context, Frame, EVisualLoggingLifetime::Transient, DebugStr);
						Driver->InvokeVisualLog(Buffers.Input[Frame], Buffers.Sync[Frame], Buffers.Aux[Frame], VLogParams);
					};

					VLogHelper(Buffers.Sync.TailFrame(), EVisualLoggingContext::InterpolationBufferTail, LexToString(TickInfo.SimulationTimeBuffer.HeadElement()->ToRealTimeMS()));
					VLogHelper(Buffers.Sync.HeadFrame(), EVisualLoggingContext::InterpolationBufferHead, LexToString(TickInfo.SimulationTimeBuffer.TailElement()->ToRealTimeMS()));

					{
						FVisualLoggingParameters VLogParams(EVisualLoggingContext::InterpolationFrom, InterpolationFrame-1, EVisualLoggingLifetime::Transient, LexToString(FromRealTime));
						Driver->InvokeVisualLog(Buffers.Input[InterpolationFrame-1], &FromState.Sync, &FromState.Aux, VLogParams);
					}

					{
						FVisualLoggingParameters VLogParams(EVisualLoggingContext::InterpolationTo, InterpolationFrame, EVisualLoggingLifetime::Transient, LexToString(ToRealTime));
						Driver->InvokeVisualLog(Buffers.Input[InterpolationFrame], ToState, ToAuxState, VLogParams);
					}

					{
						FVisualLoggingParameters VLogParams(LoggingContext, InterpolationFrame, EVisualLoggingLifetime::Transient, LexToString(NewInterpolationTime));
						Driver->InvokeVisualLog(Buffers.Input[InterpolationFrame], &OutputState.Sync, &OutputState.Aux, VLogParams);
					}
				}
				
				InterpolationTime = NewInterpolationTime;
				InternalIdx ^= 1;
			}
		}

		return InterpolationTime;
	}

private:

	FStatePair& GetFromInterpolationState() { return InterpolationState[InternalIdx]; }
	FStatePair& GetNextInterpolationState() { return InterpolationState[InternalIdx ^ 1]; }

	FRealTime InterpolationTime = 0.f; // SimTime we are currently interpolating at
	int32 InterpolationFrame = INDEX_NONE; // Frame we are currently/last interpolated at
	FStatePair InterpolationState[2]; // Interpolating "from" state and "out" state
	int32 InternalIdx = 0; // index into InterpolationState for double buffering pattern

	FRealTime WaitUntilTime = 0.f;	
	FRealTime CatchUpUntilTime = 0.f;

	FRealTime DynamicBufferedTime = 1/60.f; // SimTime we are currently interpolating at
	FRealTime DynamicBufferedTimeStep = 1/60.f;

	FRealTime MinBufferedTime = 1/120.f;
	FRealTime MaxBufferedTime = 1.f;
};