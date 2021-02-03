// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Framework/Parallel.h"
#include "Async/ParallelFor.h"
#include "Framework/Threading.h"

using namespace Chaos;

namespace Chaos
{
#if !UE_BUILD_SHIPPING
	bool bDisablePhysicsParallelFor = false;
	CHAOS_API bool bDisableParticleParallelFor = false;
	CHAOS_API bool bDisableCollisionParallelFor = false;

	FAutoConsoleVariableRef CVarDisablePhysicsParallelFor(TEXT("p.Chaos.DisablePhysicsParallelFor"), bDisablePhysicsParallelFor, TEXT("Disable parallel execution in Chaos Evolution"));
	FAutoConsoleVariableRef CVarDisableParticleParallelFor(TEXT("p.Chaos.DisableParticleParallelFor"), bDisableParticleParallelFor, TEXT("Disable parallel execution for Chaos Particles (Collisions, "));
	FAutoConsoleVariableRef CVarDisableCollisionParallelFor(TEXT("p.Chaos.DisableCollisionParallelFor"), bDisableCollisionParallelFor, TEXT("Disable parallel execution for Chaos Collisions (also disabled by DisableParticleParallelFor)"));
#else
	const bool bDisablePhysicsParallelFor = false;
#endif
}

void Chaos::PhysicsParallelFor(int32 InNum, TFunctionRef<void(int32)> InCallable, bool bForceSingleThreaded)
{
	using namespace Chaos;
	// Passthrough for now, except with global flag to disable parallel
	
	auto PassThrough = [InCallable, bIsInPhysicsSimContext = IsInPhysicsThreadContext(), bIsInGameThreadContext = IsInGameThreadContext()](int32 Idx)
	{
#if PHYSICS_THREAD_CONTEXT
		FPhysicsThreadContextScope PTScope(bIsInPhysicsSimContext);
		FGameThreadContextScope GTScope(bIsInGameThreadContext);
#endif
		InCallable(Idx);
	};
	::ParallelFor(InNum, PassThrough, bDisablePhysicsParallelFor || bForceSingleThreaded);
}

//class FRecursiveDivideTask
//{
//	TFuture<void> ThisFuture;
//	TFunctionRef<void(int32)> Callable;
//
//	int32 Begin;
//	int32 End;
//
//	FRecursiveDivideTask(int32 InBegin, int32 InEnd, TFunctionRef<void(int32)> InCallable)
//		: Begin(InBegin)
//		, End(InEnd)
//		, Callable(InCallable)
//	{
//
//	}
//
//	static FORCEINLINE TStatId GetStatId()
//	{
//		return GET_STATID(STAT_ParallelForTask);
//	}
//
//	static FORCEINLINE ENamedThreads::Type GetDesiredThread()
//	{
//		return ENamedThreads::AnyHiPriThreadHiPriTask;
//	}
//
//	static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode()
//	{
//		return ESubsequentsMode::FireAndForget;
//	}
//
//	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
//	{
//		for(int32 Index = Begin; Index < End; ++Index)
//		{
//			Callable(Index);
//		}
//
//		ThisFuture.Share()
//	}
//};
//
//void Chaos::PhysicsParallelFor_RecursiveDivide(int32 InNum, TFunctionRef<void(int32)> InCallable, bool bForceSingleThreaded /*= false*/)
//{
//	const int32 TaskThreshold = 15;
//	const int32 NumAvailable = FTaskGraphInterface::Get().GetNumWorkerThreads();
//	const int32 BatchSize = InNum / NumAvailable;
//	const int32 BatchCount = InNum / BatchSize;
//
//	const bool bUseThreads = !bForceSingleThreaded && FApp::ShouldUseThreadingForPerformance() && InNum > (2 * TaskThreshold);
//	const int32 NumToSpawn = bUseThreads ? FMath::Min<int32>(NumAvailable);
//
//	if(!bUseThreads)
//	{
//		for(int32 Index = 0; Index < InNum; ++Index)
//		{
//			InCallable(Index);
//		}
//	}
//	else
//	{
//		const int32 MidPoint = InNum / 2;
//	}
//}
