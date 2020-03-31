// Copyright Epic Games, Inc. All Rights Reserved.

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
	using TBufferTypes = typename Model::BufferTypes;
	using TTickSettings = typename Model::TickSettings;
	using TSyncState = typename TBufferTypes::TSyncState;
	using TAuxState = typename TBufferTypes::TAuxState;

	using FSimTime = FNetworkSimTime;
	using FRealTime = FNetworkSimTime::FRealTime;

	using TFrameState = TSimulationFrameState<Model>;

	struct FStatePair
	{
		TSyncState Sync;
		TAuxState Aux;
	};

	bool bEnableVisualLog = true;

	template<typename TSystemDriver>
	FRealTime PostSimTick(TSystemDriver* Driver, const TNetworkedSimulationState<Model>& State, const FNetSimTickParameters& TickParameters)
	{
		const bool bDoVLog = NetworkInterpolationDebugCVars::VLog() && bEnableVisualLog;
		const float DeltaSeconds = TickParameters.LocalDeltaTimeSeconds;

		const AActor* LogOwner = Driver->GetVLogOwner();

		// Interpolation disabled or we don't have 2 elements yet
		if (NetworkInterpolationDebugCVars::Disable() > 0 || State.GetPendingTickFrame() <= 1)
		{
			// We are still responsible for calling FinalizeFrame though
			const int32 HeadFrame = State.GetPendingTickFrame();
			if (HeadFrame >= 0)
			{
				const TFrameState* FrameState = State.ReadFrame(HeadFrame);
				const TAuxState* AuxState = State.ReadAux(HeadFrame);
				check(FrameState && AuxState);
				Driver->FinalizeFrame(FrameState->SyncState, *AuxState);
			}

			return State.GetTotalProcessedSimulationTime().ToRealTimeSeconds();
		}

		const TFrameState* HeadFrameState = State.ReadFrame(State.GetPendingTickFrame());
		const TFrameState* TailFrameState = State.ReadFrame(State.GetConfirmedFrame());

		FRealTime HeadRealTimeSeconds = HeadFrameState->TotalSimTime.ToRealTimeSeconds();
		FRealTime TailRealTimeSeconds = TailFrameState->TotalSimTime.ToRealTimeSeconds();

		// Starting off: start at the tail end
		if (InterpolationTime <= 0.f)
		{
			InterpolationTime = TailFrameState->TotalSimTime.ToRealTimeSeconds();
			InterpolationFrame = State.GetConfirmedFrame();

			auto& FromState = GetFromInterpolationState();
			FromState.Sync = State.ReadFrame(InterpolationFrame)->SyncState;
			FromState.Aux = *State.ReadAux(InterpolationFrame);
		}

		EVisualLoggingContext LoggingContext = EVisualLoggingContext::InterpolationLatest;
		FRealTime NewInterpolationTime = InterpolationTime;

		// Wait if we were too far ahead
		if (WaitUntilTime > 0.f)
		{
			if (WaitUntilTime <= HeadRealTimeSeconds)
			{
				// done waiting, we can continue
				WaitUntilTime = 0.f;
				UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Done Waiting! Head: %s"), *LexToString(HeadRealTimeSeconds));
			}
			else
			{
				if (bDoVLog)
				{
					// Still waiting, return
					UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Still Waiting! %s < %s"), *LexToString(WaitUntilTime), *LexToString(HeadRealTimeSeconds));

					const FName LocalInterpolationTimeName("Local Interpolation Time");
					FVector2D LocalTimeVsInterpolationTime(LogOwner->GetWorld()->GetTimeSeconds(), (int32)(InterpolationTime * 1000.f));
					UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, "ServerSimulationTimeGraph", LocalInterpolationTimeName, LocalTimeVsInterpolationTime);
				}
			}
		}
		else
		{
			// Calc new interpolation time
			FRealTime Step = DeltaSeconds;

			// Snap if way far behind
			FRealTime SnapThreshold = HeadRealTimeSeconds - NetworkInterpolationDebugCVars::SnapThreshold();
			if (NewInterpolationTime < SnapThreshold)
			{
				NewInterpolationTime = HeadRealTimeSeconds - NetworkInterpolationDebugCVars::SnapGoal();
				UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Snapping to %s"), *LexToString(NewInterpolationTime));
			}
			
			// Speed up if we are too far behind
			FRealTime CatchUpThreshold = HeadRealTimeSeconds - NetworkInterpolationDebugCVars::CatchUpThreshold();
			if (CatchUpUntilTime <= 0.f && NewInterpolationTime < CatchUpThreshold)
			{
				CatchUpUntilTime = HeadRealTimeSeconds - NetworkInterpolationDebugCVars::CatchUpGoal();
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
			if (NewInterpolationTime > HeadRealTimeSeconds)
			{
				UE_VLOG(LogOwner, LogNetInterpolation, Log, TEXT("Too far ahead! Starting to wait! Head: %s"), *LexToString(HeadRealTimeSeconds));
				WaitUntilTime = HeadRealTimeSeconds + NetworkInterpolationDebugCVars::WaitSlack();
				NewInterpolationTime = HeadRealTimeSeconds;
			}
		}

		// Find "To" frame
		const TSyncState* ToState = nullptr;
		const TAuxState* ToAuxState = nullptr;
		FRealTime ToTime = 0.f;

		for (int32 Frame = State.GetConfirmedFrame(); Frame <= State.GetPendingTickFrame(); ++Frame)
		{
			FRealTime FrameRealTime = State.ReadFrame(Frame)->TotalSimTime.ToRealTimeSeconds();
			if (NewInterpolationTime <= FrameRealTime)
			{
				InterpolationFrame = Frame;
				ToTime = FrameRealTime;
				ToState = &State.ReadFrame(Frame)->SyncState;
				ToAuxState = State.ReadAux(Frame);
				break;
			}
		}

		if (ensure(ToState && ToAuxState))
		{
			const FRealTime FromRealTime = InterpolationTime;
			const FRealTime ToRealTime = ToTime;
			const FRealTime InterpolationInterval = ToRealTime - FromRealTime;
			
			const bool bValidInterval = FMath::Abs(InterpolationInterval) > 0.f;

			const float InterpolationPCT = bValidInterval ? ((NewInterpolationTime - FromRealTime) / InterpolationInterval) : 1.f;
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
				FVector2D LocalTimeVsServerSimTime(LogOwner->GetWorld()->GetTimeSeconds(), (int32)HeadFrameState->TotalSimTime.ToRealTimeMS());
				UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, ServerSimTimeName, LocalTimeVsServerSimTime);

				const FName BufferTailSimTimeName("Buffer Tail Simulation Time");
				FVector2D LocalTimeVsBufferTailSim(LogOwner->GetWorld()->GetTimeSeconds(), (int32)TailFrameState->TotalSimTime.ToRealTimeMS());
				UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, BufferTailSimTimeName, LocalTimeVsBufferTailSim);

				const FName LocalInterpolationTimeName("Local Interpolation Time");
				FVector2D LocalTimeVsInterpolationTime(LogOwner->GetWorld()->GetTimeSeconds(), (int32)(NewInterpolationTime * 1000.f));
				UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, LocalInterpolationTimeName, LocalTimeVsInterpolationTime);
					
				FVector2D LocalTimeVsCatchUpThreshold(LogOwner->GetWorld()->GetTimeSeconds(), (HeadRealTimeSeconds - NetworkInterpolationDebugCVars::CatchUpThreshold()) * 1000.f);
				UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, "Catch Up Threshold", LocalTimeVsCatchUpThreshold);

				FVector2D LocalTimeVsCatchUpGoal(LogOwner->GetWorld()->GetTimeSeconds(), (TailRealTimeSeconds - NetworkInterpolationDebugCVars::CatchUpGoal()) * 1000.f);
				UE_VLOG_HISTOGRAM(LogOwner, LogNetInterpolation, Log, ServerSimulationGraphName, "Catch Up Goal", LocalTimeVsCatchUpGoal);

				// VLog the actual motion states
				const TSyncState* DebugTail = &TailFrameState->SyncState;
				const TSyncState* DebugHead = &HeadFrameState->SyncState;

				auto VLogHelper = [&](int32 Frame, EVisualLoggingContext Context, const FString& DebugStr)
				{
					FVisualLoggingParameters VLogParams(Context, Frame, EVisualLoggingLifetime::Transient, DebugStr);
					const TFrameState* FrameState = State.ReadFrame(Frame);

					Driver->InvokeVisualLog(&FrameState->InputCmd, &FrameState->SyncState, State.ReadAux(Frame), VLogParams);
				};

				VLogHelper(State.GetConfirmedFrame(), EVisualLoggingContext::InterpolationBufferTail, LexToString(TailFrameState->TotalSimTime.ToRealTimeMS()));
				VLogHelper(State.GetPendingTickFrame(), EVisualLoggingContext::InterpolationBufferHead, LexToString(HeadFrameState->TotalSimTime.ToRealTimeMS()));

				{
					FVisualLoggingParameters VLogParams(EVisualLoggingContext::InterpolationFrom, InterpolationFrame-1, EVisualLoggingLifetime::Transient, LexToString(FromRealTime));
					Driver->InvokeVisualLog(&State.ReadFrame(InterpolationFrame-1)->InputCmd, &FromState.Sync, &FromState.Aux, VLogParams);
				}

				{
					FVisualLoggingParameters VLogParams(EVisualLoggingContext::InterpolationTo, InterpolationFrame, EVisualLoggingLifetime::Transient, LexToString(ToRealTime));
					Driver->InvokeVisualLog(&State.ReadFrame(InterpolationFrame)->InputCmd, ToState, ToAuxState, VLogParams);
				}

				{
					FVisualLoggingParameters VLogParams(LoggingContext, InterpolationFrame, EVisualLoggingLifetime::Transient, LexToString(NewInterpolationTime));
					Driver->InvokeVisualLog(&State.ReadFrame(InterpolationFrame)->InputCmd, &OutputState.Sync, &OutputState.Aux, VLogParams);
				}
			}
				
			InterpolationTime = NewInterpolationTime;
			InternalIdx ^= 1;
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