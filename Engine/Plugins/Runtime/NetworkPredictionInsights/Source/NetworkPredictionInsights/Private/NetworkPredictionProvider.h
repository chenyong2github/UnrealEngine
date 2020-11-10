// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "INetworkPredictionProvider.h"


class FNetworkPredictionProvider : public INetworkPredictionProvider
{
public:
	static FName ProviderName;

	FNetworkPredictionProvider(Trace::IAnalysisSession& InSession);

	// -----------------------------------------------------
	virtual uint32 GetNetworkPredictionTraceVersion() const override { return NetworkPredictionTraceVersion; }
	virtual uint64 GetNetworkPredictionDataCounter() const { return DataCounter; }

	// -----------------------------------------------------
	
	void IncrementDataCounter() { DataCounter++; }
	void SetNetworkPredictionTraceVersion(uint32 Version);

	// ------------------------------------------------------------------------------
	//
	//	The assumption is always that we are writing *forward* in time. This will blow up 
	//	if we expect to be able to write frames in the past
	//
	// ------------------------------------------------------------------------------

	FSimulationData::FConst& WriteSimulationCreated(int32 TraceID);
	void WriteSimulationTick(int32 TraceID, FSimulationData::FTick&& InTick);
	FSimulationData::FEngineFrame& WriteSimulationEOF(uint32 SimulationId);
	void WriteNetRecv(int32 TraceID, FSimulationData::FNetSerializeRecv&& InNetRecv);
	void WriteNetCommit(uint32 SimulationId);
	void WriteSystemFault(uint32 SimulationId, uint64 EngineFrameNumber, const TCHAR* Fmt);
	void WriteOOBStateMod(uint32 SimulationId);
	void WriteOOBStateModStr(uint32 SimulationId, const TCHAR* Fmt);
	void WriteProduceInput(uint32 SimulationId);
	void WriteSynthInput(uint32 SimulationId);
	void WriteUserState(int32 TraceID, int32 Frame, uint64 EngineFrame, ENP_UserState Type, const TCHAR* UserStr);
	void WritePIEStart();

	void WriteSimulationConfig(int32 TraceID, uint64 EngineFrame, ENP_NetRole NetRole, bool bHasNetConnection, ENP_TickingPolicy TickingPolicy, ENP_NetworkLOD NetworkLOD, int32 ServiceMask);

	virtual TArrayView<const TSharedRef<FSimulationData>> ReadSimulationData() const override
	{
		return TArrayView<const TSharedRef<FSimulationData>>(ProviderData); 
	}

private:

	TSharedRef<FSimulationData>& FindOrAdd(int32 TraceID);

	// Array that stores all of our traced simulation data
	TArray<TSharedRef<FSimulationData>> ProviderData;

	uint64 DataCounter = 0;
	uint32 NetworkPredictionTraceVersion = 0;
	int32 PIESessionCounter = 0;
	Trace::IAnalysisSession& Session;
};