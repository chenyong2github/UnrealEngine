// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "TraceServices/Model/AnalysisSession.h"

class FNetworkPredictionProvider;
namespace Trace { class IAnalysisSession; }

// Analyzes events that are contained in a trace,
// Works by subscribing to events by name along with user-provided "route" identifiers
// To analyze a trace, concrete IAnalyzer-derived objects are registered with a
// FAnalysisContext which is then asked to launch and coordinate the analysis.
//
// The analyzer is what populates the data in the Provider.

class FNetworkPredictionAnalyzer : public Trace::IAnalyzer
{
public:

	FNetworkPredictionAnalyzer(Trace::IAnalysisSession& InSession, FNetworkPredictionProvider& InNetworkPredictionProvider);

	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnAnalysisEnd() override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:

	enum : uint16
	{
		RouteId_SimulationScope,
		RouteId_SimulationCreated,
		RouteId_SimulationConfig,
		RouteId_WorldFrameStart,
		RouteId_PieBegin,
		RouteId_SystemFault,
		RouteId_Tick,
		RouteId_SimTick,
		RouteId_InputCmd,
		RouteId_SyncState,
		RouteId_AuxState,
		RouteId_PhysicsState,
		RouteId_NetRecv,
		RouteId_ShouldReconcile,
		RouteId_RollbackInject,
		RouteId_PushInputFrame,
		RouteId_ProduceInput,
		RouteId_OOBStateMod

		/*
		RouteId_GameInstanceRegister,
		RouteId_WorldFrameStart,
		RouteId_SimulationCreated,
		RouteId_SimulationNetRole,
		RouteId_SimulationNetGUID,
		RouteId_SimulationTick,
		RouteId_OOBStateMod,
		RouteId_OOBStateModStrSync,
		RouteId_OOBStateModStrAux,
		RouteId_ProduceInput,
		RouteId_SynthInput,
		RouteId_SimulationEOF,
		RouteId_NetSerializeRecv,
		RouteId_NetSerializeCommit,
		
		RouteId_PieBegin,
		RouteId_SystemFault
		*/
	};


	Trace::IAnalysisSession& Session;
	FNetworkPredictionProvider& NetworkPredictionProvider;

	// Current values
	uint64 EngineFrameNumber;
	float DeltaTimeSeconds;
	uint32 GameInstanceID;
	int32 TraceID=INDEX_NONE;

	int32 TickStartMS;
	int32 TickDeltaMS;
	int32 TickOutputFrame;
	int32 TickLocalOffsetFrame;

	int32 PendingWriteFrame;
};