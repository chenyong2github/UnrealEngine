// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshComponentBudgeted.h"
#include "AnimationBudgetAllocator.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(AnimationBudget);

FOnCalculateSignificance USkeletalMeshComponentBudgeted::OnCalculateSignificanceDelegate;

USkeletalMeshComponentBudgeted::USkeletalMeshComponentBudgeted(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, AnimationBudgetHandle(INDEX_NONE)
	, AnimationBudgetAllocator(nullptr)
	, bAutoRegisterWithBudgetAllocator(true)
	, bAutoCalculateSignificance(false)
	, bShouldUseActorRenderedFlag(false)
{
}

void USkeletalMeshComponentBudgeted::BeginPlay()
{
	Super::BeginPlay();

	if(bAutoRegisterWithBudgetAllocator && !UKismetSystemLibrary::IsDedicatedServer(this))
	{
		if (UWorld* LocalWorld = GetWorld())
		{
			if (IAnimationBudgetAllocator* LocalAnimationBudgetAllocator = IAnimationBudgetAllocator::Get(LocalWorld))
			{
				LocalAnimationBudgetAllocator->RegisterComponent(this);
			}
		}
	}
}

void USkeletalMeshComponentBudgeted::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Dont unregister if we are in the process of being destroyed in a GC.
	// As reciprocal ptrs are null, handles are all invalid.
	if(!IsUnreachable())	
	{
		if (UWorld* LocalWorld = GetWorld())
		{
			if (IAnimationBudgetAllocator* LocalAnimationBudgetAllocator = IAnimationBudgetAllocator::Get(LocalWorld))
			{
				LocalAnimationBudgetAllocator->UnregisterComponent(this);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void USkeletalMeshComponentBudgeted::SetComponentTickEnabled(bool bEnabled)
{
	if (AnimationBudgetAllocator)
	{
		AnimationBudgetAllocator->SetComponentTickEnabled(this, bEnabled);
	}
	else
	{
		Super::SetComponentTickEnabled(bEnabled);
	}
}

void USkeletalMeshComponentBudgeted::SetComponentSignificance(float Significance, bool bNeverSkip, bool bTickEvenIfNotRendered, bool bAllowReducedWork, bool bForceInterpolate)
{
	if (AnimationBudgetAllocator)
	{
		AnimationBudgetAllocator->SetComponentSignificance(this, Significance, bNeverSkip, bTickEvenIfNotRendered, bAllowReducedWork, bForceInterpolate);
	}
	else if (HasBegunPlay())
	{
		UE_LOG(LogSkeletalMesh, Warning, TEXT("SetComponentSignificance called on [%s] before registering with budget allocator"), *GetName());
	}
}

void USkeletalMeshComponentBudgeted::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
#if !UE_BUILD_SHIPPING
	CSV_SCOPED_TIMING_STAT(AnimationBudget, BudgetedAnimation);
#endif

	if(AnimationBudgetAllocator)
	{
		uint64 StartTime = FPlatformTime::Cycles64();

		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

		AnimationBudgetAllocator->SetGameThreadLastTickTimeMs(AnimationBudgetHandle, FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartTime));
	}
	else
	{
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	}
}

void USkeletalMeshComponentBudgeted::CompleteParallelAnimationEvaluation(bool bDoPostAnimEvaluation)
{
#if !UE_BUILD_SHIPPING
	CSV_SCOPED_TIMING_STAT(AnimationBudget, BudgetedAnimation);
#endif

	if(AnimationBudgetAllocator)
	{
		uint64 StartTime = FPlatformTime::Cycles64();

		Super::CompleteParallelAnimationEvaluation(bDoPostAnimEvaluation);

		AnimationBudgetAllocator->SetGameThreadLastCompletionTimeMs(AnimationBudgetHandle, FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartTime));
	}
	else
	{
		Super::CompleteParallelAnimationEvaluation(bDoPostAnimEvaluation);
	}
}