// Copyright Epic Games, Inc. All Rights Reserved.


#include "Trace/NetworkPredictionTrace.h"
#include "CoreMinimal.h"
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
//#include "Trace/Trace.inl" this will need to be included after next copy up from Dev-Core

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
	UE_TRACE_EVENT_FIELD(int32, StartMS)
	UE_TRACE_EVENT_FIELD(int32, EndMS)
	UE_TRACE_EVENT_FIELD(int32, OutputFrame)	

UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, SimulationEOF)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(float, EngineFrameDeltaTime)
	UE_TRACE_EVENT_FIELD(int32, BufferSize)
	UE_TRACE_EVENT_FIELD(int32, PendingTickFrame)
	UE_TRACE_EVENT_FIELD(int32, LatestInputFrame)
	UE_TRACE_EVENT_FIELD(int32, TotalSimTime)
	UE_TRACE_EVENT_FIELD(int32, AllowedSimTime)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, NetSerializeRecv)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(int32, ReceivedSimTimeMS)
	UE_TRACE_EVENT_FIELD(int32, ReceivedFrame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, InputCmd)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(int32, SimulationFrame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, SyncState)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(int32, SimulationFrame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, AuxState)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
	UE_TRACE_EVENT_FIELD(int32, SimulationFrame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, NetSerializeCommit)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
UE_TRACE_EVENT_END()

// General system fault. Log message is in attachment
UE_TRACE_EVENT_BEGIN(NetworkPrediction, SystemFault)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
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
	UE_TRACE_EVENT_FIELD(uint32, DummyData)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, WorldFrameStart)
	UE_TRACE_EVENT_FIELD(uint64, EngineFrameNumber)
	UE_TRACE_EVENT_FIELD(float, DeltaSeconds)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, OOBStateModStrSync)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(NetworkPrediction, OOBStateModStrAux)
	UE_TRACE_EVENT_FIELD(uint32, SimulationId)
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
		<< SimulationNetRole.SimulationId(SimulationId)
		<< SimulationNetRole.NetRole((uint8)NetRole);
}

void FNetworkPredictionTrace::TraceSimulationTick(int32 OutputFrame, const FNetworkSimTime& FrameDeltaTime, const FNetSimTimeStep& TimeStep)
{
	UE_TRACE_LOG(NetworkPrediction, SimulationTick, NetworkPredictionChannel)
		<< SimulationTick.SimulationId(PeakSimulationIdChecked())
		<< SimulationTick.StartMS(TimeStep.TotalSimulationTime)
		<< SimulationTick.EndMS(TimeStep.TotalSimulationTime + TimeStep.StepMS)
		<< SimulationTick.OutputFrame(OutputFrame);
}

void FNetworkPredictionTrace::TraceEOF_Internal(int32 BufferSize, int32 PendingTickFrame, int32 LatestInputFrame, FNetworkSimTime TotalSimTime, FNetworkSimTime AllowedSimTime)
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	UE_TRACE_LOG(NetworkPrediction, SimulationEOF, NetworkPredictionChannel)
		<< SimulationEOF.SimulationId(SimulationId)
		<< SimulationEOF.EngineFrameDeltaTime(FApp::GetDeltaTime())
		<< SimulationEOF.BufferSize(BufferSize)
		<< SimulationEOF.PendingTickFrame(PendingTickFrame)
		<< SimulationEOF.LatestInputFrame(LatestInputFrame)
		<< SimulationEOF.TotalSimTime(TotalSimTime)
		<< SimulationEOF.AllowedSimTime(AllowedSimTime);

	// Trace this simulation's NetGUID if we haven't already
	// (This stinks having to be here: but we can't get an event for NetGUID assignment and it wont always be assigned when sim is created)
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
		<< NetSerializeRecv.ReceivedSimTimeMS(ReceivedTime)
		<< NetSerializeRecv.ReceivedFrame(ReceivedFrame);
}

void FNetworkPredictionTrace::TraceNetSerializeCommit()
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	UE_TRACE_LOG(NetworkPrediction, NetSerializeCommit, NetworkPredictionChannel)
		<< NetSerializeCommit.SimulationId(SimulationId);
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

void FNetworkPredictionTrace::TraceOOBStateMod(int32 SimulationId)
{
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
				<< InputCmd.Attachment(*StrOut, AttachmentSize);
			break;
		}
		case ETraceUserState::Sync:
		{
			UE_TRACE_LOG(NetworkPrediction, SyncState, NetworkPredictionChannel, AttachmentSize)
				<< SyncState.SimulationId(SimulationId)
				<< SyncState.SimulationFrame(Frame)
				<< SyncState.Attachment(*StrOut, AttachmentSize);
			break;
		}
		case ETraceUserState::Aux:
		{
			UE_TRACE_LOG(NetworkPrediction, AuxState, NetworkPredictionChannel, AttachmentSize)
				<< AuxState.SimulationId(SimulationId)
				<< AuxState.SimulationFrame(Frame)
				<< AuxState.Attachment(*StrOut, AttachmentSize);
			break;
		}
	}
}

void FNetworkPredictionTrace::TracePIEStart()
{
	UE_TRACE_LOG(NetworkPrediction, PieBegin, NetworkPredictionChannel)
		<< PieBegin.DummyData(0); // temp to quiet clang
}

void FNetworkPredictionTrace::TraceWorldFrameStart(float DeltaSeconds)
{
	UE_TRACE_LOG(NetworkPrediction, WorldFrameStart, NetworkPredictionChannel)
		<< WorldFrameStart.EngineFrameNumber(GFrameNumber)
		<< WorldFrameStart.DeltaSeconds(DeltaSeconds);
}

void FNetworkPredictionTrace::TraceOOBStrInternal(int32 SimulationId, ETraceUserState StateType, const TCHAR* Fmt)
{
	const uint16 AttachmentSize = (FCString::Strlen(Fmt)+1) * sizeof(TCHAR);
	switch(StateType)
	{
		case ETraceUserState::Input:
		{
			ensure(false); // unexpected. We don't do OOB mods to input state
			break;
		}
		case ETraceUserState::Sync:
		{
			UE_TRACE_LOG(NetworkPrediction, OOBStateModStrSync, NetworkPredictionChannel, AttachmentSize)
				<< OOBStateModStrSync.SimulationId(SimulationId)	
				<< OOBStateModStrSync.Attachment(Fmt, AttachmentSize);
			break;
		}
		case ETraceUserState::Aux:
		{
			UE_TRACE_LOG(NetworkPrediction, OOBStateModStrAux, NetworkPredictionChannel, AttachmentSize)
				<< OOBStateModStrAux.SimulationId(SimulationId)	
				<< OOBStateModStrAux.Attachment(Fmt, AttachmentSize);
			break;
		}
	}
}

#include "CoreTypes.h"
#include "Misc/VarArgs.h"
#include "HAL/UnrealMemory.h"
#include "Templates/UnrealTemplate.h"

// Copied from VarargsHelper.h
#define GROWABLE_LOGF(SerializeFunc) \
	int32	BufferSize	= 1024; \
	TCHAR*	Buffer		= NULL; \
	int32	Result		= -1; \
	/* allocate some stack space to use on the first pass, which matches most strings */ \
	TCHAR	StackBuffer[512]; \
	TCHAR*	AllocatedBuffer = NULL; \
\
	/* first, try using the stack buffer */ \
	Buffer = StackBuffer; \
	GET_VARARGS_RESULT( Buffer, UE_ARRAY_COUNT(StackBuffer), UE_ARRAY_COUNT(StackBuffer) - 1, Fmt, Fmt, Result ); \
\
	/* if that fails, then use heap allocation to make enough space */ \
	while(Result == -1) \
	{ \
		FMemory::SystemFree(AllocatedBuffer); \
		/* We need to use malloc here directly as GMalloc might not be safe. */ \
		Buffer = AllocatedBuffer = (TCHAR*) FMemory::SystemMalloc( BufferSize * sizeof(TCHAR) ); \
		if (Buffer == NULL) \
		{ \
			return; \
		} \
		GET_VARARGS_RESULT( Buffer, BufferSize, BufferSize-1, Fmt, Fmt, Result ); \
		BufferSize *= 2; \
	}; \
	Buffer[Result] = 0; \
	; \
\
	SerializeFunc; \
	FMemory::SystemFree(AllocatedBuffer);


void FNetworkPredictionTrace::TraceSystemFault(const TCHAR* Fmt, ...)
{
	const uint32 SimulationId = PeakSimulationIdChecked();

	GROWABLE_LOGF( 
		
		check(Result >= 0 );
		const uint16 AttachmentSize = (Result+1) * sizeof(TCHAR);

		UE_TRACE_LOG(NetworkPrediction, SystemFault, NetworkPredictionChannel, AttachmentSize)
			<< SystemFault.SimulationId(SimulationId)
			<< SystemFault.Attachment(Buffer, AttachmentSize);

		UE_LOG(LogNetworkSim, Warning, TEXT("SystemFault: %s"), Buffer);
	);
}