// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Containers/UnrealString.h"
#include "Model/NetProfilerProvider.h"

namespace Trace
{
	class IAnalysisSession;
}

class FNetTraceAnalyzer
	: public Trace::IAnalyzer
{
public:
	FNetTraceAnalyzer(Trace::IAnalysisSession& Session, Trace::FNetProfilerProvider& NetProfilerProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void OnAnalysisEnd() override;

private:

	enum : uint16
	{
		RouteId_InitEvent,
		RouteId_InstanceDestroyedEvent,
		RouteId_NameEvent,
		RouteId_PacketContentEvent,
		RouteId_PacketEvent,
		RouteId_PacketDroppedEvent,
		RouteId_ConnectionCreatedEvent,
		RouteId_ConnectionClosedEvent,
		RouteId_ObjectCreatedEvent,
		RouteId_ObjectDestroyedEvent,
	};

	Trace::IAnalysisSession& Session;
	Trace::FNetProfilerProvider& NetProfilerProvider;
	uint32 NetTraceVersion;
	uint32 NetTraceReporterVersion;

	// Shared for trace
	TMap<uint16, uint32> TracedNameIdToNetProfilerNameIdMap;

	TMap<uint32, uint32> TraceEventTypeToNetProfilerEventTypeIndexMap;

	struct FNetTraceConnectionState
	{
		// Index into persistent connection array
		uint32 ConnectionIndex;

		// Current packet data
		uint32 CurrentPacketStartIndex[Trace::ENetProfilerConnectionMode::Count];
		Trace::ENetProfilerConnectionMode ConnectionMode;
	};

	struct FNetTraceActiveObjectState
	{
		uint32 ObjectIndex;
		uint32 NameIndex;
	};

	// Map from instance id to index in the persistent instance array
	struct FNetTraceGameInstanceState
	{
		TMap<uint32, TSharedRef<FNetTraceConnectionState>> ActiveConnections;
		TMap<uint32, FNetTraceActiveObjectState> ActiveObjects;

		uint32 GameInstanceIndex;
	};

	uint32 GetTracedEventTypeIndex(uint16 NameIndex, uint8 Level);

	TSharedRef<FNetTraceGameInstanceState> GetOrCreateActiveGameInstanceState(uint32 GameInstanceId);
	void DestroyActiveGameInstanceState(uint32 GameInstanceId);

	TSharedRef<FNetTraceConnectionState> GetActiveConnectionState(uint32 GameInstanceId, uint32 ConnectionId);
	Trace::FNetProfilerTimeStamp GetLastTimestamp() const { return LastTimeStamp; }

	TMap<uint32, TSharedRef<FNetTraceGameInstanceState>> ActiveGameInstances;
	Trace::FNetProfilerTimeStamp LastTimeStamp;
};
