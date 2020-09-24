// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionAnalyzer.h"
#include "NetworkPredictionProvider.h"
#include "Containers/StringView.h"


FNetworkPredictionAnalyzer::FNetworkPredictionAnalyzer(Trace::IAnalysisSession& InSession, FNetworkPredictionProvider& InNetworkPredictionProvider)
	: Session(InSession)
	, NetworkPredictionProvider(InNetworkPredictionProvider)
{
}

void FNetworkPredictionAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;
	
	Builder.RouteEvent(RouteId_SimulationScope, "NetworkPrediction", "SimulationScope");
	Builder.RouteEvent(RouteId_SimulationCreated, "NetworkPrediction", "SimulationCreated");
	Builder.RouteEvent(RouteId_SimulationConfig, "NetworkPrediction", "SimulationConfig");

	Builder.RouteEvent(RouteId_WorldFrameStart, "NetworkPrediction", "WorldFrameStart");
	Builder.RouteEvent(RouteId_PieBegin, "NetworkPrediction", "PieBegin");
	Builder.RouteEvent(RouteId_SystemFault, "NetworkPrediction", "SystemFault");

	Builder.RouteEvent(RouteId_Tick, "NetworkPrediction", "Tick");
	Builder.RouteEvent(RouteId_SimTick, "NetworkPrediction", "SimTick");

	Builder.RouteEvent(RouteId_NetRecv, "NetworkPrediction", "NetRecv");
	Builder.RouteEvent(RouteId_ShouldReconcile, "NetworkPrediction", "ShouldReconcile");
	Builder.RouteEvent(RouteId_RollbackInject, "NetworkPrediction", "RollbackInject");
	
	Builder.RouteEvent(RouteId_PushInputFrame, "NetworkPrediction", "PushInputFrame");
	Builder.RouteEvent(RouteId_ProduceInput, "NetworkPrediction", "ProduceInput");

	Builder.RouteEvent(RouteId_OOBStateMod, "NetworkPrediction", "OOBStateMod");

	Builder.RouteEvent(RouteId_InputCmd, "NetworkPrediction", "InputCmd");
	Builder.RouteEvent(RouteId_SyncState, "NetworkPrediction", "SyncState");
	Builder.RouteEvent(RouteId_AuxState, "NetworkPrediction", "AuxState");
	Builder.RouteEvent(RouteId_PhysicsState, "NetworkPrediction", "PhysicsState");
}

void FNetworkPredictionAnalyzer::OnAnalysisEnd()
{
}

bool FNetworkPredictionAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);
	const auto& EventData = Context.EventData;

	auto ParseUserState = [&EventData, this](ENP_UserState Type)
	{
		// FIXME: we want to trace ANSI strings but are converting them to WIDE here to store them for analysis.
		// Latest version of insights should have formal ANSI string support so this should be able to go away.
		const ANSICHAR* AnsiStr = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
		NetworkPredictionProvider.WriteUserState(this->TraceID, this->PendingWriteFrame, this->EngineFrameNumber, Type, Session.StoreString(StringCast<TCHAR>(AnsiStr).Get()));
	};

	bool bUnhandled = false;
	switch (RouteId)
	{
		case RouteId_SimulationScope:
		{
			TraceID = EventData.GetValue<int32>("TraceID");
			break;
		}

		case RouteId_SimulationCreated:
		{
			TraceID = EventData.GetValue<int32>("TraceID");
			FSimulationData::FConst& ConstData = NetworkPredictionProvider.WriteSimulationCreated(TraceID);
			ConstData.DebugName = FString(EventData.GetAttachmentSize() / sizeof(TCHAR), reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
			ConstData.ID.SimID = EventData.GetValue<uint32>("SimulationID");
			ConstData.GameInstanceId = EventData.GetValue<uint32>("GameInstanceID");
			break;
		}

		case RouteId_SimulationConfig:
		{
			TraceID = EventData.GetValue<int32>("TraceID");

			NetworkPredictionProvider.WriteSimulationConfig(TraceID,
				EngineFrameNumber,
				(ENP_NetRole)EventData.GetValue<uint8>("NetRole"),
				(bool)EventData.GetValue<uint8>("bHasNetConnection"),
				(ENP_TickingPolicy)EventData.GetValue<uint8>("TickingPolicy"),
				(ENP_NetworkLOD)EventData.GetValue<uint8>("NetworkLOD"),
				EventData.GetValue<int32>("ServiceMask"));
			break;
		}

		case RouteId_WorldFrameStart:
		{
			EngineFrameNumber = EventData.GetValue<uint64>("EngineFrameNumber");
			DeltaTimeSeconds = EventData.GetValue<float>("DeltaTimeSeconds");
			GameInstanceID = EventData.GetValue<uint32>("GameInstanceID");
			break;
		}

		case RouteId_PieBegin:
		{
			NetworkPredictionProvider.WritePIEStart();
			break;
		}

		case RouteId_SystemFault:
		{
			const TCHAR* StoredString = Session.StoreString(reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
			ensureMsgf(TraceID > 0, TEXT("Invalid TraceID when analyzing SystemFault: %s"), StoredString);

			NetworkPredictionProvider.WriteSystemFault(
				EventData.GetValue<uint32>("SimulationID"),
				EngineFrameNumber,
				StoredString);
			break;
		}

		case RouteId_Tick:
		{
			TickStartMS = EventData.GetValue<int32>("StartMS");
			TickDeltaMS = EventData.GetValue<int32>("DeltaMS");
			TickOutputFrame = EventData.GetValue<int32>("OutputFrame");
			TickLocalOffsetFrame = EventData.GetValue<int32>("LocalOffsetFrame");
			break;
		}

		case RouteId_SimTick:
		{
			TraceID = EventData.GetValue<int32>("TraceID");

			FSimulationData::FTick TickData;
			TickData.EngineFrame = EngineFrameNumber;
			TickData.StartMS = TickStartMS;
			TickData.EndMS = TickStartMS + TickDeltaMS;
			TickData.OutputFrame = TickOutputFrame;
			TickData.LocalOffsetFrame = TickLocalOffsetFrame;
			NetworkPredictionProvider.WriteSimulationTick(TraceID, MoveTemp(TickData));

			PendingWriteFrame = TickOutputFrame;
			ensure(PendingWriteFrame >= 0);
			break;
		}

		case RouteId_InputCmd:
		{
			ParseUserState(ENP_UserState::Input);
			break;
		}
		case RouteId_SyncState:
		{
			ParseUserState(ENP_UserState::Sync);
			break;
		}
		case RouteId_AuxState:
		{
			ParseUserState(ENP_UserState::Aux);
			break;
		}

		case RouteId_PhysicsState:
		{
			ParseUserState(ENP_UserState::Physics);
			break;
		}

		case RouteId_NetRecv:
		{
			ensure(TraceID > 0);
			PendingWriteFrame = EventData.GetValue<int32>("Frame");
			ensure(PendingWriteFrame >= 0);

			FSimulationData::FNetSerializeRecv NetRecv;
			NetRecv.EngineFrame = EngineFrameNumber;
			NetRecv.SimTimeMS = EventData.GetValue<int32>("TimeMS");
			NetRecv.Frame = PendingWriteFrame;

			NetworkPredictionProvider.WriteNetRecv(TraceID, MoveTemp(NetRecv));
			break;
		}

		case RouteId_ShouldReconcile:
		{
			TraceID = EventData.GetValue<int32>("TraceID");
			break;
		}

		case RouteId_RollbackInject:
		{
			// This isn't accurate anymore. We should remove "NetCommit" from the provider side and distinguish between "caused a rollback" and "participated in a rollback"
			// (E.g, ShouldReconcile vs RollbackIbject)
			TraceID = EventData.GetValue<int32>("TraceID");
			NetworkPredictionProvider.WriteNetCommit(TraceID);
			break;
		}

		case RouteId_PushInputFrame:
		{
			PendingWriteFrame = EventData.GetValue<int32>("Frame");
			ensure(PendingWriteFrame >= 0);
			break;
		}

		case RouteId_ProduceInput:
		{
			TraceID = EventData.GetValue<int32>("TraceID");
			NetworkPredictionProvider.WriteProduceInput(TraceID);
			break;
		}

		case RouteId_OOBStateMod:
		{
			TraceID = EventData.GetValue<int32>("TraceID");
			PendingWriteFrame = EventData.GetValue<int32>("Frame");
			ensure(PendingWriteFrame >= 0);

			NetworkPredictionProvider.WriteOOBStateMod(TraceID);

			// FIXME: ANSI to TCHAR conversion should be removable. See UserState comment above	
			const ANSICHAR* AttachmentData = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
			int32 AttachmentSize = EventData.GetAttachmentSize();
			
			NetworkPredictionProvider.WriteOOBStateModStr(TraceID, Session.StoreString(StringCast<TCHAR>(AttachmentData, AttachmentSize).Get()));
			break;
		}
	}

	if (!bUnhandled)
	{
		NetworkPredictionProvider.IncrementDataCounter();
	}

	return true;
}