// Copyright Epic Games, Inc. All Rights Reserved.


#include "Trace/NetworkPredictionTrace.h"
#include "CoreMinimal.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "HAL/PlatformTime.h"
#include "UObject/ObjectKey.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Engine/NetConnection.h"
#include "UObject/CoreNet.h"
#include "Engine/PackageMapClient.h"
#include "Logging/LogMacros.h"
#include "Trace/Trace.inl"

UE_TRACE_CHANNEL_DEFINE(NetworkPredictionChannel)

UE_TRACE_EVENT_BEGIN(NetworkPrediction, GameInstanceRegister)
	UE_TRACE_EVENT_FIELD(uint32, GameInstanceId)
	UE_TRACE_EVENT_FIELD(bool, IsServer)
UE_TRACE_EVENT_END()

// Trace a simulation creation. GroupName is attached as attachment.
UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimulationCreated)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(uint32, GameInstanceId)
UE_TRACE_EVENT_END()

// Trace a simulation's local netrole, which can change during the sim's liftetime.
UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimulationNetRole)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(uint8, NetRole)
UE_TRACE_EVENT_END()

// Trace a simulation's NetGUID assignment, which unfortunately hasn't always happened at creation.
UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimulationNetGUID)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(uint32, NetGUID)
UE_TRACE_EVENT_END()

// Traces a simulation tick in any context
UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimulationTick)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
	UE_TRACE_EVENT_FIELD(int32, StartMS)
	UE_TRACE_EVENT_FIELD(int32, EndMS)
	UE_TRACE_EVENT_FIELD(int32, OutputFrame)	

UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimulationEOF)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
	UE_TRACE_EVENT_FIELD(double, EngineFrameDeltaTime)	// FIXME: Need better tracking of engine frame vs simulation EOF updates
	UE_TRACE_EVENT_FIELD(double, EngineCurrentTime)		// FIXME: ^^
	UE_TRACE_EVENT_FIELD(int32, TotalProcessedMS)
	UE_TRACE_EVENT_FIELD(int32, TotalAllowedMS)
	UE_TRACE_EVENT_FIELD(int32, LastSentKeyframe)
	UE_TRACE_EVENT_FIELD(int32, LastReceivedKeyframe)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, NetSerializeRecv)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
	UE_TRACE_EVENT_FIELD(int32, ReceivedSimTimeMS)
	UE_TRACE_EVENT_FIELD(int32, ReceivedFrame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, InputCmd)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(int32, SimulationFrame)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, SyncState)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(int32, SimulationFrame)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, AuxState)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(int32, SimulationFrame)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, NetSerializeCommit)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction,NetSerializeFault)
	UE_TRACE_EVENT_FIELD(uint32,SimulationId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, OOBStateMod)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, ProduceInput)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, SynthInput)
UE_TRACE_EVENT_FIELD(uint32, SimulationId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, PieBegin)
UE_TRACE_EVENT_END()


// ----------------------------------------------------------------------------
//	Internal stack to track which simulation is active. 
//		-Avoid passing or duplicating SimulationId to rep controllers, interpolaters, etc
//		-Allows us to trace dependent simulations a bit easier
// ----------------------------------------------------------------------------

TArray<uint32, TInlineAllocator<4>> SimulationIdStack;
uint32 PeakSimulationIdChecked()
{
	check(SimulationIdStack.Num() > 0);
	return SimulationIdStack.Last();
}

// Assign Id to UGameInstance ObjectKey. Assignment
struct FGameInstanceIdMap
{
	uint32 GetId(UGameInstance* Instance)
	{
		FObjectKey Key(Instance);
		
		int32 FoundIdx=INDEX_NONE;
		if (AssignedInstances.Find(Key, FoundIdx))
		{
			return (uint32)(FoundIdx+1);
		}

		AssignedInstances.Add(Key);
		uint32 Id = (uint32)AssignedInstances.Num();

		const bool bIsServer = Instance->GetWorld()->GetNetMode() != ENetMode::NM_Client;

		UE_TRACE_LOG(NetworkPrediction, GameInstanceRegister, NetworkPredictionChannel)
			<< GameInstanceRegister.GameInstanceId(Id)
			<< GameInstanceRegister.IsServer(bIsServer);
		
		return Id;
	}

private:
	TArray<FObjectKey> AssignedInstances;

} GameInstanceMap;


//Tracks which SimulationIDs we've successfully traced NetGUIDs of
TArray<uint32> TracedSimulationNetGUIDs;
TMap<uint32, TWeakObjectPtr<const AActor>> OwningActorMap;

// ---------------------------------------------------------------------------

FStringOutputDevice FNetworkPredictionTrace::StrOut;

void FNetworkPredictionTrace::PushSimulationId(uint32 SimulationId)
{
	SimulationIdStack.Push(SimulationId);
}

void FNetworkPredictionTrace::PopSimulationId()
{
	SimulationIdStack.Pop();
}

void FNetworkPredictionTrace::TraceSimulationCreated(const AActor* OwningActor, const FName& GroupName)
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	uint32 GameInstanceId = GameInstanceMap.GetId(OwningActor->GetGameInstance());
	FString DisplayNameStr = FString::Printf(TEXT("%s, %s"), *OwningActor->GetName(), *GroupName.ToString());

	const uint16 AttachmentSize = (uint16)((DisplayNameStr.Len()+1) * sizeof(TCHAR));

	OwningActorMap.Add(SimulationId, OwningActor);

	UE_TRACE_LOG(NetworkPrediction, SimulationCreated, NetworkPredictionChannel, AttachmentSize)
		<< SimulationCreated.SimulationId(SimulationId)
		<< SimulationCreated.GameInstanceId(GameInstanceId)
		<< SimulationCreated.Attachment(*DisplayNameStr, AttachmentSize);
}

void FNetworkPredictionTrace::TraceSimulationNetRole(uint32 SimulationId, ENetRole NetRole)
{
	UE_TRACE_LOG(NetworkPrediction, SimulationNetRole, NetworkPredictionChannel)
		<< SimulationNetRole.EngineFrameNumber(GFrameNumber)
		<< SimulationNetRole.SimulationId(SimulationId)
		<< SimulationNetRole.NetRole((uint8)NetRole);
}

void FNetworkPredictionTrace::TraceSimulationTick(int32 OutputFrame, const FNetworkSimTime& FrameDeltaTime, const FSimulationTickState& TickState)
{
	UE_TRACE_LOG(NetworkPrediction, SimulationTick, NetworkPredictionChannel)
		<< SimulationTick.SimulationId(PeakSimulationIdChecked())
		<< SimulationTick.EngineFrameNumber(GFrameNumber)
		<< SimulationTick.StartMS(TickState.GetTotalProcessedSimulationTime())
		<< SimulationTick.EndMS(TickState.GetTotalProcessedSimulationTime() + FrameDeltaTime)
		<< SimulationTick.OutputFrame(OutputFrame);
}

void FNetworkPredictionTrace::TraceSimulationEOF(const FSimulationTickState& TickState, int32 LastSentKeyframe, int32 LastReceivedKeyframe)
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	UE_TRACE_LOG(NetworkPrediction, SimulationEOF, NetworkPredictionChannel)
		<< SimulationEOF.SimulationId(SimulationId)
		<< SimulationEOF.EngineFrameNumber(GFrameNumber)
		<< SimulationEOF.EngineFrameDeltaTime(FApp::GetDeltaTime())	// FIXME
		<< SimulationEOF.EngineCurrentTime(FApp::GetCurrentTime())	// FIXME
		<< SimulationEOF.TotalProcessedMS(TickState.GetTotalProcessedSimulationTime())
		<< SimulationEOF.TotalAllowedMS(TickState.GetTotalAllowedSimulationTime())
		<< SimulationEOF.LastSentKeyframe(LastSentKeyframe)
		<< SimulationEOF.LastReceivedKeyframe(LastReceivedKeyframe);
	


	// Do we need to trace the NetGUID? This stinks having to be here
	if (TracedSimulationNetGUIDs.Contains(SimulationId) == false)
	{
		uint32 NetGUID = 0;
		if (const AActor* OwningActor = OwningActorMap.FindChecked(SimulationId).Get())
		{
			if (UWorld* World = OwningActor->GetWorld())
			{
				if (UNetDriver* NetDriver = World->GetNetDriver())
				{
					if (NetDriver->GuidCache.IsValid())
					{
						NetGUID = NetDriver->GuidCache->GetNetGUID(OwningActor).Value;
					}
				}
			}
		}

		if (NetGUID > 1)
		{
			UE_TRACE_LOG(NetworkPrediction, SimulationNetGUID, NetworkPredictionChannel)
				<< SimulationNetGUID.SimulationId(SimulationId)
				<< SimulationNetGUID.NetGUID(NetGUID);
		}
	}
}

void FNetworkPredictionTrace::TraceNetSerializeRecv(const FNetworkSimTime& ReceivedTime, int32 ReceivedFrame)
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	UE_TRACE_LOG(NetworkPrediction, NetSerializeRecv, NetworkPredictionChannel)
		<< NetSerializeRecv.SimulationId(SimulationId)
		<< NetSerializeRecv.EngineFrameNumber(GFrameNumber)
		<< NetSerializeRecv.ReceivedSimTimeMS(ReceivedTime)
		<< NetSerializeRecv.ReceivedFrame(ReceivedFrame);
}

void FNetworkPredictionTrace::TraceNetSerializeCommit()
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	UE_TRACE_LOG(NetworkPrediction, NetSerializeCommit, NetworkPredictionChannel)
		<< NetSerializeCommit.SimulationId(SimulationId);
}

void FNetworkPredictionTrace::TraceNetSerializeFault()
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	UE_TRACE_LOG(NetworkPrediction, NetSerializeFault, NetworkPredictionChannel)
		<< NetSerializeFault.SimulationId(SimulationId);
}

void FNetworkPredictionTrace::TraceProduceInput()
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	UE_TRACE_LOG(NetworkPrediction, ProduceInput, NetworkPredictionChannel)
		<< ProduceInput.SimulationId(SimulationId);
}

void FNetworkPredictionTrace::TraceSynthInput()
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	UE_TRACE_LOG(NetworkPrediction, SynthInput, NetworkPredictionChannel)
		<< SynthInput.SimulationId(SimulationId);
}

void FNetworkPredictionTrace::TraceOOBStateMod()
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	UE_TRACE_LOG(NetworkPrediction, OOBStateMod, NetworkPredictionChannel)
		<< OOBStateMod.SimulationId(SimulationId);
}

void FNetworkPredictionTrace::TraceUserState_Internal(int32 Frame, ETraceUserState StateType)
{
	const uint32 SimulationId = PeakSimulationIdChecked();
	const uint16 AttachmentSize = (uint16)((StrOut.Len()+1) * sizeof(TCHAR));

	switch(StateType)
	{
		case ETraceUserState::Input:
		{
			UE_TRACE_LOG(NetworkPrediction, InputCmd, NetworkPredictionChannel, AttachmentSize)
				<< InputCmd.SimulationId(SimulationId)
				<< InputCmd.SimulationFrame(Frame)
				<< InputCmd.EngineFrameNumber(GFrameNumber)
				<< InputCmd.Attachment(*StrOut, AttachmentSize);
			break;
		}
		case ETraceUserState::Sync:
		{
			UE_TRACE_LOG(NetworkPrediction, SyncState, NetworkPredictionChannel, AttachmentSize)
				<< SyncState.SimulationId(SimulationId)
				<< SyncState.SimulationFrame(Frame)
				<< SyncState.EngineFrameNumber(GFrameNumber)
				<< SyncState.Attachment(*StrOut, AttachmentSize);
			break;
		}
		case ETraceUserState::Aux:
		{
			UE_TRACE_LOG(NetworkPrediction, AuxState, NetworkPredictionChannel, AttachmentSize)
				<< AuxState.SimulationId(SimulationId)
				<< AuxState.SimulationFrame(Frame)
				<< AuxState.EngineFrameNumber(GFrameNumber)
				<< AuxState.Attachment(*StrOut, AttachmentSize);
			break;
		}
	}
}

void FNetworkPredictionTrace::TracePIEStart()
{
	UE_TRACE_LOG(NetworkPrediction, PieBegin, NetworkPredictionChannel);
}