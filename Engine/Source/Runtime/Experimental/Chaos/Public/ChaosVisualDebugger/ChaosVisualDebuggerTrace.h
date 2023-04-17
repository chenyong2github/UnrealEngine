// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Vector.h"

#ifndef CHAOS_VISUAL_DEBUGGER_ENABLED
	#define CHAOS_VISUAL_DEBUGGER_ENABLED (WITH_CHAOS_VISUAL_DEBUGGER && UE_TRACE_ENABLED)
#endif

// Define NO-OP versions of our macros if we can't Trace
#if !CHAOS_VISUAL_DEBUGGER_ENABLED
	#ifndef CVD_TRACE_PARTICLE
		#define CVD_TRACE_PARTICLE(ParticleHandle)
	#endif

	#ifndef CVD_TRACE_PARTICLES
		#define CVD_TRACE_PARTICLES(ParticleHandles)
	#endif

	#ifndef CVD_TRACE_SOLVER_START_FRAME
		#define CVD_TRACE_SOLVER_START_FRAME(SolverType, SolverRef)
	#endif

	#ifndef CVD_TRACE_SOLVER_END_FRAME
		#define CVD_TRACE_SOLVER_END_FRAME(SolverType, SolverRef)
	#endif

	#ifndef CVD_SCOPE_TRACE_SOLVER_FRAME
		#define CVD_SCOPE_TRACE_SOLVER_FRAME(SolverType, SolverRef)
	#endif

	#ifndef CVD_TRACE_SOLVER_STEP_START
		#define CVD_TRACE_SOLVER_STEP_START()
	#endif

	#ifndef CVD_TRACE_SOLVER_STEP_END
		#define CVD_TRACE_SOLVER_STEP_END()
	#endif

	#ifndef CVD_SCOPE_TRACE_SOLVER_STEP
		#define CVD_SCOPE_TRACE_SOLVER_STEP()
	#endif
#else

#include "Trace/Trace.h"
#include "Trace/Trace.inl"

#ifndef CVD_DEFINE_TRACE_VECTOR
	#define CVD_DEFINE_TRACE_VECTOR(Type, Name) \
		UE_TRACE_EVENT_FIELD(Type, Name##X) \
		UE_TRACE_EVENT_FIELD(Type, Name##Y) \
		UE_TRACE_EVENT_FIELD(Type, Name##Z)
#endif

#ifndef CVD_DEFINE_TRACE_ROTATOR
	#define CVD_DEFINE_TRACE_ROTATOR(Type, Name) \
	UE_TRACE_EVENT_FIELD(Type, Name##X) \
	UE_TRACE_EVENT_FIELD(Type, Name##Y) \
	UE_TRACE_EVENT_FIELD(Type, Name##Z) \
	UE_TRACE_EVENT_FIELD(Type, Name##W)
#endif

#ifndef CVD_TRACE_VECTOR_ON_EVENT
	#define CVD_TRACE_VECTOR_ON_EVENT(EventName, Name, Vector) \
	EventName.Name##X(Vector.X) \
	<< EventName.Name##Y(Vector.Y) \
	<< EventName.Name##Z(Vector.Z)
#endif

#ifndef CVD_TRACE_ROTATOR_ON_EVENT
	#define CVD_TRACE_ROTATOR_ON_EVENT(EventName, Name, Rotator) \
	EventName.Name##X(Rotator.X) \
	<< EventName.Name##Y(Rotator.Y) \
	<< EventName.Name##Z(Rotator.Z) \
	<< EventName.Name##W(Rotator.W)
#endif

UE_TRACE_CHANNEL_EXTERN(ChaosVDChannel, CHAOS_API)

UE_TRACE_EVENT_BEGIN_EXTERN(ChaosVDLogger, ChaosVDSolverFrameStart)
	UE_TRACE_EVENT_FIELD(int32, SolverID)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, DebugName)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN_EXTERN(ChaosVDLogger, ChaosVDSolverFrameEnd)
	UE_TRACE_EVENT_FIELD(int32, SolverID)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN_EXTERN(ChaosVDLogger, ChaosVDParticle)
	UE_TRACE_EVENT_FIELD(int32, SolverID)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(int32, ParticleID)
	UE_TRACE_EVENT_FIELD(uint8, ParticleType)
	CVD_DEFINE_TRACE_VECTOR(Chaos::FReal, Position)
	CVD_DEFINE_TRACE_ROTATOR(Chaos::FReal, Rotation)
	CVD_DEFINE_TRACE_VECTOR(Chaos::FReal, Velocity)
	CVD_DEFINE_TRACE_VECTOR(Chaos::FReal, AngularVelocity)
	UE_TRACE_EVENT_FIELD(int8, ObjectState)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, DebugName)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN_EXTERN(ChaosVDLogger, ChaosVDSolverStepStart)
	UE_TRACE_EVENT_FIELD(int32, SolverID)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, StepNumber)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN_EXTERN(ChaosVDLogger, ChaosVDSolverStepEnd)
	UE_TRACE_EVENT_FIELD(int32, SolverID)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, StepNumber)
UE_TRACE_EVENT_END()

#ifndef CVD_TRACE_PARTICLE
	#define CVD_TRACE_PARTICLE(ParticleHandle) \
		FChaosVisualDebuggerTrace::TraceParticle(ParticleHandle);
#endif

#ifndef CVD_TRACE_PARTICLES
	#define CVD_TRACE_PARTICLES(ParticleHandles) \
		FChaosVisualDebuggerTrace::TraceParticles(ParticleHandles)
#endif

#ifndef CVD_TRACE_SOLVER_START_FRAME
	#define CVD_TRACE_SOLVER_START_FRAME(SolverType, SolverRef) \
		FChaosVDContext StartContextData; \
		FChaosVisualDebuggerTrace::GetCVDContext<SolverType>(SolverRef, StartContextData); \
		FChaosVisualDebuggerTrace::TraceSolverFrameStart(StartContextData, FChaosVisualDebuggerTrace::GetDebugName<SolverType>(SolverRef));
#endif

#ifndef CVD_TRACE_SOLVER_END_FRAME
	#define CVD_TRACE_SOLVER_END_FRAME(SolverType, SolverRef) \
		FChaosVDContext EndContextData; \
		FChaosVisualDebuggerTrace::GetCVDContext<SolverType>(SolverRef, EndContextData); \
		FChaosVisualDebuggerTrace::TraceSolverFrameEnd(EndContextData);
#endif

#ifndef CVD_SCOPE_TRACE_SOLVER_FRAME
	#define CVD_SCOPE_TRACE_SOLVER_FRAME(SolverType, SolverRef) \
		FChaosVDScopeSolverFrame<SolverType> ScopeSolverFrame(SolverRef);
#endif

#ifndef CVD_TRACE_SOLVER_STEP_START
	#define CVD_TRACE_SOLVER_STEP_START() \
		FChaosVisualDebuggerTrace::TraceSolverStepStart();
#endif

#ifndef CVD_TRACE_SOLVER_STEP_END
	#define CVD_TRACE_SOLVER_STEP_END() \
		FChaosVisualDebuggerTrace::TraceSolverStepEnd();
#endif

#ifndef CVD_SCOPE_TRACE_SOLVER_STEP
	#define CVD_SCOPE_TRACE_SOLVER_STEP() \
		FChaosVDScopeSolverStep ScopeSolverStep;
#endif

struct FChaosVDContext;

namespace Chaos
{
	class FPhysicsSolverBase;
	template <typename T, int d>
	class TGeometryParticleHandles;
}

/** Class containing  all the Tracing logic to record data for the Chaos Visual Debugger tool */
class FChaosVisualDebuggerTrace
{
public:

	static CHAOS_API void TraceParticle(const Chaos::FGeometryParticleHandle* ParticleHandle);
	static CHAOS_API void TraceParticle(const Chaos::FGeometryParticleHandle* ParticleHandle, const FChaosVDContext& ContextData);
	static CHAOS_API void TraceParticles(const Chaos::TGeometryParticleHandles<Chaos::FReal, 3>& ParticleHandles);
	static CHAOS_API void TraceSolverFrameStart(const FChaosVDContext& ContextData, const FString& InDebugName);
	static CHAOS_API void TraceSolverFrameEnd(const FChaosVDContext& ContextData);
	static CHAOS_API void TraceSolverStepStart();
	static CHAOS_API void TraceSolverStepEnd();

	template<typename T>
	static void GetCVDContext(T& ObjectWithContext, FChaosVDContext& OutCVDContext);

	template<typename T>
	static FString GetDebugName(T& ObjectWithContext);
};

template <typename T>
void FChaosVisualDebuggerTrace::GetCVDContext(T& ObjectWithContext, FChaosVDContext& OutCVDContext)
{
	OutCVDContext = ObjectWithContext.GetChaosVDContextData();	
}

template <typename T>
FString FChaosVisualDebuggerTrace::GetDebugName(T& ObjectWithDebugName)
{
#if CHAOS_DEBUG_NAME
	return ObjectWithDebugName.GetDebugName().ToString();
#else
	return FString("COMPILED OUT");
#endif
}

struct FChaosVDScopeSolverStep
{
	FChaosVDScopeSolverStep()
	{
		CVD_TRACE_SOLVER_STEP_START();
	}

	~FChaosVDScopeSolverStep()
	{
		CVD_TRACE_SOLVER_STEP_END();
	}
};

template<typename T>
struct FChaosVDScopeSolverFrame
{
	FChaosVDScopeSolverFrame(T& InSolverRef) : SolverRef(InSolverRef)
	{
		CVD_TRACE_SOLVER_START_FRAME(T, SolverRef);
	}

	~FChaosVDScopeSolverFrame()
	{
		CVD_TRACE_SOLVER_END_FRAME(T, SolverRef);
	}

	T& SolverRef;
};
#endif // CHAOS_VISUAL_DEBUGGER_ENABLED
