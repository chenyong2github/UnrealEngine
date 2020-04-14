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

	FSimulationData::FConst& WriteSimulationCreated(uint32 SimulationId);
	void WriteSimulationTick(uint32 SimulationId, FSimulationData::FTick&& InTick);
	FSimulationData::FEngineFrame& WriteSimulationEOF(uint32 SimulationId);
	void WriteNetRecv(uint32 SimulationId, FSimulationData::FNetSerializeRecv&& InNetRecv);
	void WriteNetCommit(uint32 SimulationId);
	void WriteSystemFault(uint32 SimulationId, uint64 EngineFrameNumber, const TCHAR* Fmt);
	void WriteOOBStateMod(uint32 SimulationId);
	void WriteOOBStateModStrSync(uint32 SimulationId, const TCHAR* Fmt);
	void WriteOOBStateModStrAux(uint32 SimulationId, const TCHAR* Fmt);
	void WriteProduceInput(uint32 SimulationId);
	void WriteSynthInput(uint32 SimulationId);
	void WriteUserState(uint32 SimulationId, int32 Frame, uint64 EngineFrame, ENP_UserState Type, const TCHAR* UserStr);
	void WritePIEStart();
	
	void WriteSimulationNetRole(uint32 SimulationId, uint64 EngineFrame, ENP_NetRole Role);
	void WriteSimulationNetGUID(uint32 SimulationId, uint32 NetGUID);

	virtual TArrayView<const TSharedRef<FSimulationData>> ReadSimulationData() const override
	{
		return TArrayView<const TSharedRef<FSimulationData>>(ProviderData); 
	}

private:

	TSharedRef<FSimulationData>& FindOrAdd(uint32 SimulationId);

	// Array that stores all of our traced simulation data
	TArray<TSharedRef<FSimulationData>> ProviderData;

	uint64 DataCounter = 0;
	uint32 NetworkPredictionTraceVersion = 0;
	int32 PIESessionCounter = 0;
	Trace::IAnalysisSession& Session;
};