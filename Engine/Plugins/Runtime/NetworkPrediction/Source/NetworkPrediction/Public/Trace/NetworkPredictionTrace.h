// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetworkedSimulationModelTick.h"
#include "Trace/Trace.h"
#include "Engine/EngineTypes.h" // For ENetRole urgh
#include "Containers/UnrealString.h"
#include "NetworkPredictionTypes.h"

#ifndef UE_NP_TRACE_ENABLED
#define UE_NP_TRACE_ENABLED !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#endif

// Tracing user state (content) can generate a lot of data. So this can be turned off here
#ifndef UE_NP_TRACE_USER_STATES_ENABLED
#define UE_NP_TRACE_USER_STATES_ENABLED 1
#endif


#if UE_NP_TRACE_ENABLED

// Scope Push/Pop for active simulation
#define UE_NP_TRACE_SET_SCOPE_SIM(SimulationId) FScopedSimulationTraceHelper _ScopedSimIdTraceHelper(SimulationId)

// Called when simulation is created. (Note this also sets a Scope for tracing the initial user states next)
#define UE_NP_TRACE_SIM_CREATED(OwningActor, GroupName) FNetworkPredictionTrace::TraceSimulationCreated(OwningActor, GroupName)

#define UE_NP_TRACE_NETROLE(SimulationId, NetRole) FNetworkPredictionTrace::TraceSimulationNetRole(SimulationId, NetRole)

// Called when simulation ticks in any context (includes resimulates, sim extrapolates, etc)
#define UE_NP_TRACE_SIM_TICK(OutputFrame, FrameDeltaTime, TimeStep) FNetworkPredictionTrace::TraceSimulationTick(OutputFrame, FrameDeltaTime, TimeStep)

// Called at the end of an engine frame (EOF) to update the current tick state of a simulation
#define UE_NP_TRACE_SIM_EOF(Buffers) FNetworkPredictionTrace::TraceSimulationEOF(Buffers)

// Called whenever we receive network data: note this only indicates that data was received, not necessarily that it was committed to the simulation buffers
#define UE_NP_TRACE_NET_SERIALIZE_RECV(ReceivedTime, ReceivedFrame) FNetworkPredictionTrace::TraceNetSerializeRecv(ReceivedTime, ReceivedFrame)

// Called whenever a new user state has been inserted into the buffers. Analysis will determine "how" it got there from previous trace events (SIM_TICK, NET_SERIALIZE_RECV)
#define UE_NP_TRACE_USER_STATE_INPUT(UserState, Frame) FNetworkPredictionTrace::TraceUserState(UserState, Frame, FNetworkPredictionTrace::ETraceUserState::Input)
#define UE_NP_TRACE_USER_STATE_SYNC(UserState, Frame) FNetworkPredictionTrace::TraceUserState(UserState, Frame, FNetworkPredictionTrace::ETraceUserState::Sync)
#define UE_NP_TRACE_USER_STATE_AUX(UserState, Frame) FNetworkPredictionTrace::TraceUserState(UserState, Frame, FNetworkPredictionTrace::ETraceUserState::Aux)

// Called to indicate that the previous received NetSerialized data was committed to the buffers. (Not all data is commited, but we want to trace all received data)
#define UE_NP_TRACE_NET_SERIALIZE_COMMIT() FNetworkPredictionTrace::TraceNetSerializeCommit()

// Called prior to sampling local input
#define UE_NP_TRACE_PRODUCE_INPUT() FNetworkPredictionTrace::TraceProduceInput()

// Called prior to generating synthesized input
#define UE_NP_TRACE_SYNTH_INPUT() FNetworkPredictionTrace::TraceSynthInput()

// Called to indicate we are about to write state to the buffers outside of the normal simulation tick/netrecive. TODO: add char* identifier to debug where the mod came from
#define UE_NP_TRACE_OOB_STATE_MOD(SimulationId) FNetworkPredictionTrace::TraceOOBStateMod(SimulationId)

// Called when a PIE session is started. This is so we can keep our *sets* of worlds/simulations seperate.
#define UE_NP_TRACE_PIE_START() FNetworkPredictionTrace::TracePIEStart()

#define UE_NP_TRACE_SYSTEM_FAULT(Format, ...) FNetworkPredictionTrace::TraceSystemFault(TEXT(Format), ##__VA_ARGS__)

#define UE_NP_TRACE_WORLD_FRAME_START(DeltaSeconds) FNetworkPredictionTrace::TraceWorldFrameStart(DeltaSeconds)

#define UE_NP_TRACE_OOB_STR_SYNC(SimulationId, Str) FNetworkPredictionTrace::TraceOOBStr(SimulationId, FNetworkPredictionTrace::ETraceUserState::Sync, Str);
#define UE_NP_TRACE_OOB_STR_AUX(SimulationId, Str) FNetworkPredictionTrace::TraceOOBStr(SimulationId, FNetworkPredictionTrace::ETraceUserState::Aux, Str);

#else

// Compiled out
#define UE_NP_TRACE_SET_SCOPE_SIM(SimulationId)
#define UE_NP_TRACE_SIM_CREATED(...)
#define UE_NP_TRACE_NETROLE(...)
#define UE_NP_TRACE_SIM_TICK(...)
#define UE_NP_TRACE_SIM_EOF(...)
#define UE_NP_TRACE_NET_SERIALIZE_RECV(...)
#define UE_NP_TRACE_USER_STATE_INPUT(...)
#define UE_NP_TRACE_USER_STATE_SYNC(...)
#define UE_NP_TRACE_USER_STATE_AUX(...)
#define UE_NP_TRACE_ROLLBACK(...)
#define UE_NP_TRACE_NET_SERIALIZE_COMMIT(...)
#define UE_NP_TRACE_PRODUCE_INPUT(...)
#define UE_NP_TRACE_SYNTH_INPUT(...)
#define UE_NP_TRACE_OOB_STATE_MOD(...)
#define UE_NP_TRACE_PIE_START(...)
#define UE_NP_TRACE_SYSTEM_FAULT(Format, ...) UE_LOG(LogNetworkSim, Warning, TEXT(Format), ##__VA_ARGS__);
#define UE_NP_TRACE_WORLD_FRAME_START(...)
#define UE_NP_TRACE_OOB_STR_SYNC(...)
#define UE_NP_TRACE_OOB_STR_AUX(...)

#endif // UE_NP_TRACE_ENABLED

NETWORKPREDICTION_API UE_TRACE_CHANNEL_EXTERN(NetworkPredictionChannel);

class AActor;

template<typename Model>
struct TNetworkedSimulationState;

class NETWORKPREDICTION_API FNetworkPredictionTrace
{
public:

	enum ETraceVersion
	{
		Initial = 1,
	};

	static void TraceSimulationCreated(const AActor* OwningActor, const FName& GroupName);
	static void TraceSimulationNetRole(uint32 SimulationId, ENetRole NetRole);
	static void TraceSimulationTick(int32 OutputFrame, const FNetworkSimTime& FrameDeltaTime, const FNetSimTimeStep& TimeStep);
	static void TraceNetSerializeRecv(const FNetworkSimTime& ReceivedTime, int32 ReceivedFrame);
	static void TraceNetSerializeCommit();
	static void TraceProduceInput();
	static void TraceSynthInput();
	static void TraceOOBStateMod(int32 SimulationId);
	static void TracePIEStart();
	static void TraceSystemFault(const TCHAR* Fmt, ...);
	static void TraceWorldFrameStart(float DeltaSeconds);

	enum ETraceUserState
	{
		Input,
		Sync,
		Aux
	};

	static void TraceOOBStr(int32 SimulationId, ETraceUserState StateType, const TCHAR* Fmt)
	{
		if (Fmt)
		{
			TraceOOBStrInternal(SimulationId, StateType, Fmt);
		}
	}
	
	template<typename TUserState>
	static void TraceUserState(const TUserState& State, int32 Frame, ETraceUserState StateType)
	{
#if UE_NP_TRACE_USER_STATES_ENABLED
		if (UE_TRACE_CHANNELEXPR_IS_ENABLED(NetworkPredictionChannel))
		{
			StrOut.SetAutoEmitLineTerminator(true);
			StrOut.Reset();

			FStandardLoggingParameters LoggingParameters(&StrOut, EStandardLoggingContext::Full, Frame);
			State.Log(LoggingParameters);

			TraceUserState_Internal(Frame, StateType);
		}
#endif
	}

	template<typename Model>
	static void TraceSimulationEOF(const TNetworkedSimulationState<Model>& State)
	{
		TraceEOF_Internal(State.GetCapacity(), State.GetPendingTickFrame(), State.GetLatestInputFrame(), State.GetTotalProcessedSimulationTime(), State.GetTotalAllowedSimulationTime());
	}

private:

	static void PushSimulationId(uint32 SimulationId);
	static void PopSimulationId();

	static void TraceUserState_Internal(int32 Frame, ETraceUserState StateType);
	static void TraceEOF_Internal(int32 BufferSize, int32 PendingTickFrame, int32 LatestInputFrame, FNetworkSimTime TotalSimTime, FNetworkSimTime AllowedSimTime);
	static void TraceOOBStrInternal(int32 SimulationId, ETraceUserState StateType, const TCHAR* Fmt);

	static FStringOutputDevice StrOut;

	friend struct FScopedSimulationTraceHelper;
};

struct FScopedSimulationTraceHelper
{
	FScopedSimulationTraceHelper(uint32 SimulationId)
	{
		FNetworkPredictionTrace::PushSimulationId(SimulationId);
	}
	~FScopedSimulationTraceHelper()
	{
		FNetworkPredictionTrace::PopSimulationId();
	}
};