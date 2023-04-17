// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVisualDebugger/ChaosVisualDebuggerTrace.h"

#if CHAOS_VISUAL_DEBUGGER_ENABLED
#include "Chaos/ParticleHandle.h"
#include "Chaos/Framework/PhysicsSolverBase.h"

UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDSolverFrameStart)

UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDSolverFrameEnd)

UE_TRACE_CHANNEL_DEFINE(ChaosVDChannel);
UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDParticle)
UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDSolverStepStart)
UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDSolverStepEnd)

void FChaosVisualDebuggerTrace::TraceParticle(const Chaos::FGeometryParticleHandle* ParticleHandle)
{
	const FChaosVDContext* CVDContextData = FChaosVDThreadContext::Get().GetCurrentContext();
	if (!ensure(CVDContextData))
	{
		return;
	}

	TraceParticle(ParticleHandle, *CVDContextData);
}

void FChaosVisualDebuggerTrace::TraceParticle(const Chaos::FGeometryParticleHandle* ParticleHandle, const FChaosVDContext& ContextData)
{
	if (!TRACE_PRIVATE_CHANNELEXPR_IS_ENABLED(ChaosVDChannel))
	{
		return;
	}

	if (!ParticleHandle)
	{
		UE_LOG(LogChaos, Warning, TEXT("Tried to Trace a null particle %hs"), __FUNCTION__);
		return;
	}

	FString TempDebugName;
#if CHAOS_DEBUG_NAME
	if (const TSharedPtr<FString, ESPMode::ThreadSafe> DebugNamePtr = ParticleHandle->DebugName())
	{
		TempDebugName = *DebugNamePtr.Get();
	}
#else
	TempDebugName = TEXT("NOT AVAILABLE - COMPILED OUT");
#endif
	
	UE_TRACE_LOG(ChaosVDLogger, ChaosVDParticle, ChaosVDChannel)
		<< ChaosVDParticle.SolverID(ContextData.Id)
		<< ChaosVDParticle.Cycle(FPlatformTime::Cycles64())

		<< ChaosVDParticle.ParticleID(ParticleHandle->UniqueIdx().Idx)
		<< ChaosVDParticle.ParticleType(static_cast<uint8>(ParticleHandle->Type))
		<< ChaosVDParticle.DebugName(*TempDebugName, TempDebugName.Len())

		<< CVD_TRACE_VECTOR_ON_EVENT(ChaosVDParticle, Position, ParticleHandle->X())
		<< CVD_TRACE_ROTATOR_ON_EVENT(ChaosVDParticle, Rotation, ParticleHandle->R())
		<< CVD_TRACE_VECTOR_ON_EVENT(ChaosVDParticle, Velocity, Chaos::FConstGenericParticleHandle(ParticleHandle)->V())
		<< CVD_TRACE_VECTOR_ON_EVENT(ChaosVDParticle, AngularVelocity, Chaos::FConstGenericParticleHandle(ParticleHandle)->W())

		<< ChaosVDParticle.ObjectState(static_cast<int8>(ParticleHandle->ObjectState()));
}

void FChaosVisualDebuggerTrace::TraceParticles(const Chaos::TGeometryParticleHandles<Chaos::FReal, 3>& ParticleHandles)
{
	if (!TRACE_PRIVATE_CHANNELEXPR_IS_ENABLED(ChaosVDChannel))
	{
		return;
	}

	const FChaosVDContext* CVDContextData = FChaosVDThreadContext::Get().GetCurrentContext();
	if (!ensure(CVDContextData))
	{
		return;
	}

	for (uint32 ParticleIndex = 0; ParticleIndex < ParticleHandles.Size(); ParticleIndex++)
	{
		// TODO: We should only trace the Particles that changed. probably using the Dirty Flags?
		// Geometry data "uniqueness" will be handled by the trace helper
		TraceParticle(ParticleHandles.Handle(ParticleIndex).Get(), *CVDContextData);
	}
}

void FChaosVisualDebuggerTrace::TraceSolverFrameStart(const FChaosVDContext& ContextData, const FString& InDebugName)
{
	if (!ensure(ContextData.Id != INDEX_NONE))
	{
		return;
	}

	FChaosVDThreadContext::Get().PushContext(ContextData);

	UE_TRACE_LOG(ChaosVDLogger, ChaosVDSolverFrameStart, ChaosVDChannel)
		<< ChaosVDSolverFrameStart.SolverID(ContextData.Id)
		<< ChaosVDSolverFrameStart.Cycle(FPlatformTime::Cycles64())
		<< ChaosVDSolverFrameStart.DebugName(*InDebugName, InDebugName.Len());
}

void FChaosVisualDebuggerTrace::TraceSolverFrameEnd(const FChaosVDContext& ContextData)
{
	FChaosVDThreadContext::Get().PopContext();

	if (!ensure(ContextData.Id != INDEX_NONE))
	{
		return;
	}

	UE_TRACE_LOG(ChaosVDLogger, ChaosVDSolverFrameEnd, ChaosVDChannel)
		<< ChaosVDSolverFrameEnd.SolverID(ContextData.Id)
		<< ChaosVDSolverFrameEnd.Cycle(FPlatformTime::Cycles64());
}

void FChaosVisualDebuggerTrace::TraceSolverStepStart()
{
	const FChaosVDContext* CVDContextData = FChaosVDThreadContext::Get().GetCurrentContext();
	if (!ensure(CVDContextData))
	{
		return;
	}

	UE_TRACE_LOG(ChaosVDLogger, ChaosVDSolverStepStart, ChaosVDChannel)
		<< ChaosVDSolverStepStart.Cycle(FPlatformTime::Cycles64())
		<< ChaosVDSolverStepStart.SolverID(CVDContextData->Id);
}

void FChaosVisualDebuggerTrace::TraceSolverStepEnd()
{
	const FChaosVDContext* CVDContextData = FChaosVDThreadContext::Get().GetCurrentContext();
	if (!ensure(CVDContextData))
	{
		return;
	}

	UE_TRACE_LOG(ChaosVDLogger, ChaosVDSolverStepEnd, ChaosVDChannel)
		<< ChaosVDSolverStepEnd.Cycle(FPlatformTime::Cycles64())
		<< ChaosVDSolverStepEnd.SolverID(CVDContextData->Id);
}
#endif
