// Copyright Epic Games, Inc. All Rights Reserved.

#include "Abilities/Tasks/AbilityTask.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemStats.h"

#if !UE_BUILD_SHIPPING
static void DebugRecordAbilityTaskCreated(const UAbilityTask* NewTask);
static void DebugRecordAbilityTaskDestroyed(const UAbilityTask* NewTask);
static void DebugPrintAbilityTasksByClass();
#endif // !UE_BUILD_SHIPPING

namespace AbilityTaskCVars
{
	static int32 AbilityTaskMaxCount = 1000;
	static FAutoConsoleVariableRef CVarMaxAbilityTaskCount(
		TEXT("AbilitySystem.AbilityTask.MaxCount"),
		AbilityTaskMaxCount,
		TEXT("Global limit on the number of extant AbilityTasks. Use 'AbilitySystem.Debug.RecordingEnabled' and 'AbilitySystem.AbilityTask.Debug.PrintCounts' to debug why you are hitting this before raising the cap.")
	);

#if !UE_BUILD_SHIPPING
	static bool bRecordAbilityTaskCounts = false;
	static FAutoConsoleVariableRef CVarRecordAbilityTaskCounts(
		TEXT("AbilitySystem.AbilityTask.Debug.RecordingEnabled"),
		bRecordAbilityTaskCounts,
		TEXT("If this is enabled, all new AbilityTasks will be counted by type. Use 'AbilitySystem.AbilityTask.Debug.PrintCounts' to print out the current counts.")
	);

	static bool bRecordAbilityTaskSourceAbilityCounts = false;
	static FAutoConsoleVariableRef CVarRecordAbilityTaskSourceAbilityCounts(
		TEXT("AbilitySystem.AbilityTask.Debug.SourceRecordingEnabled"),
		bRecordAbilityTaskSourceAbilityCounts,
		TEXT("Requires bRecordAbilityTaskCounts to be set to true for this value to do anything.  If both are enabled, all new AbilityTasks (after InitTask is called in NewAbilityTask) will be counted by the class of the ability that created them.  Use 'AbilitySystem.AbilityTask.Debug.PrintCounts' to print out the current counts.")
	);

	static FAutoConsoleCommand AbilityTaskPrintAbilityTaskCountsCmd(
		TEXT("AbilitySystem.AbilityTask.Debug.PrintCounts"),
		TEXT("Print out the current AbilityTask counts by class. 'AbilitySystem.AbilityTask.Debug.RecordingEnabled' must be turned on for this to function."),
		FConsoleCommandDelegate::CreateStatic(DebugPrintAbilityTasksByClass)
	);
#endif
}

static int32 GlobalAbilityTaskCount = 0;

UAbilityTask::UAbilityTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaitStateBitMask = static_cast<uint8>(EAbilityTaskWaitState::WaitingOnGame);

#if !UE_BUILD_SHIPPING
	if (AbilityTaskCVars::bRecordAbilityTaskCounts)
	{
		DebugRecordAbilityTaskCreated(this);
	}
#endif  // !UE_BUILD_SHIPPING

	++GlobalAbilityTaskCount;
	SET_DWORD_STAT(STAT_AbilitySystem_TaskCount, GlobalAbilityTaskCount);
	if (!ensure(GlobalAbilityTaskCount < AbilityTaskCVars::AbilityTaskMaxCount))
	{
		ABILITY_LOG(Warning, TEXT("Way too many AbilityTasks are currently active! %d. %s"), GlobalAbilityTaskCount, *GetClass()->GetName());

#if !UE_BUILD_SHIPPING
		// Auto dump the counts if we hit the limit
		if (AbilityTaskCVars::bRecordAbilityTaskCounts)
		{
			static bool bHasDumpedAbilityTasks = false;  // The dump is spammy, so we only want to auto-dump once

			if (!bHasDumpedAbilityTasks)
			{
				DebugPrintAbilityTasksByClass();
				bHasDumpedAbilityTasks = true;
			}
		}
#endif  // !UE_BUILD_SHIPPING
	}
}

void UAbilityTask::OnDestroy(bool bInOwnerFinished)
{
	checkf(GlobalAbilityTaskCount > 0, TEXT("Mismatched AbilityTask counting"));
	--GlobalAbilityTaskCount;
	SET_DWORD_STAT(STAT_AbilitySystem_TaskCount, GlobalAbilityTaskCount);

	bWasSuccessfullyDestroyed = true;

#if !UE_BUILD_SHIPPING
	if (AbilityTaskCVars::bRecordAbilityTaskCounts)
	{
		DebugRecordAbilityTaskDestroyed(this);
	}
#endif  // !UE_BUILD_SHIPPING

	Super::OnDestroy(bInOwnerFinished);
}

void UAbilityTask::BeginDestroy()
{
	Super::BeginDestroy();

	if (!bWasSuccessfullyDestroyed)
	{
		// this shouldn't happen, it means that ability was destroyed while being active, but we need to keep GlobalAbilityTaskCount in sync anyway
		checkf(GlobalAbilityTaskCount > 0, TEXT("Mismatched AbilityTask counting"));
		--GlobalAbilityTaskCount;
		SET_DWORD_STAT(STAT_AbilitySystem_TaskCount, GlobalAbilityTaskCount);
		bWasSuccessfullyDestroyed = true;

#if !UE_BUILD_SHIPPING
		if (AbilityTaskCVars::bRecordAbilityTaskCounts)
		{
			DebugRecordAbilityTaskDestroyed(this);
		}
#endif  // !UE_BUILD_SHIPPING
	}
}

FGameplayAbilitySpecHandle UAbilityTask::GetAbilitySpecHandle() const
{
	return Ability ? Ability->GetCurrentAbilitySpecHandle() : FGameplayAbilitySpecHandle();
}

void UAbilityTask::SetAbilitySystemComponent(UAbilitySystemComponent* InAbilitySystemComponent)
{
	AbilitySystemComponent = InAbilitySystemComponent;
}

void UAbilityTask::InitSimulatedTask(UGameplayTasksComponent& InGameplayTasksComponent)
{
	UGameplayTask::InitSimulatedTask(InGameplayTasksComponent);

	SetAbilitySystemComponent(Cast<UAbilitySystemComponent>(TasksComponent.Get()));
}

FPredictionKey UAbilityTask::GetActivationPredictionKey() const
{
	return Ability ? Ability->GetCurrentActivationInfo().GetActivationPredictionKey() : FPredictionKey();
}

int32 AbilityTaskWarnIfBroadcastSuppress = 0;
static FAutoConsoleVariableRef CVarAbilityTaskWarnIfBroadcastSuppress(TEXT("AbilitySystem.AbilityTaskWarnIfBroadcastSuppress"), AbilityTaskWarnIfBroadcastSuppress, TEXT("Print warning if an ability task broadcast is suppressed because the ability has ended"), ECVF_Default );

bool UAbilityTask::ShouldBroadcastAbilityTaskDelegates() const
{
	bool ShouldBroadcast = (Ability && Ability->IsActive());

	if (!ShouldBroadcast && AbilityTaskWarnIfBroadcastSuppress)
	{
		ABILITY_LOG(Warning, TEXT("Suppressing ability task %s broadcsat"), *GetDebugString());
	}

	return ShouldBroadcast;
}

bool UAbilityTask::IsPredictingClient() const
{
	return Ability && Ability->IsPredictingClient();
}

bool UAbilityTask::IsForRemoteClient() const
{
	return Ability && Ability->IsForRemoteClient();
}

bool UAbilityTask::IsLocallyControlled() const
{
	return Ability && Ability->IsLocallyControlled();
}

bool UAbilityTask::CallOrAddReplicatedDelegate(EAbilityGenericReplicatedEvent::Type Event, FSimpleMulticastDelegate::FDelegate Delegate)
{
	if (!AbilitySystemComponent->CallOrAddReplicatedDelegate(Event, GetAbilitySpecHandle(), GetActivationPredictionKey(), Delegate))
	{
		SetWaitingOnRemotePlayerData();
		return false;
	}
	return true;
}

void UAbilityTask::SetWaitingOnRemotePlayerData()
{
	if (IsValid(Ability) && AbilitySystemComponent.IsValid())
	{
		WaitStateBitMask |= (uint8)EAbilityTaskWaitState::WaitingOnUser;
		Ability->NotifyAbilityTaskWaitingOnPlayerData(this);
	}
}

void UAbilityTask::ClearWaitingOnRemotePlayerData()
{
	WaitStateBitMask &= ~((uint8)EAbilityTaskWaitState::WaitingOnUser);
}

bool UAbilityTask::IsWaitingOnRemotePlayerdata() const
{
	return (WaitStateBitMask & (uint8)EAbilityTaskWaitState::WaitingOnUser) != 0;
}

void UAbilityTask::SetWaitingOnAvatar()
{
	if (IsValid(Ability) && AbilitySystemComponent.IsValid())
	{
		WaitStateBitMask |= (uint8)EAbilityTaskWaitState::WaitingOnAvatar;
		Ability->NotifyAbilityTaskWaitingOnAvatar(this);
	}
}

void UAbilityTask::ClearWaitingOnAvatar()
{
	WaitStateBitMask &= ~((uint8)EAbilityTaskWaitState::WaitingOnAvatar);
}

bool UAbilityTask::IsWaitingOnAvatar() const
{
	return (WaitStateBitMask & (uint8)EAbilityTaskWaitState::WaitingOnAvatar) != 0;
}

#if !UE_BUILD_SHIPPING
static TMap<const UClass*, int32> StaticAbilityTasksByClass = {};
static TMap<const UClass*, int32> StaticAbilityTasksByAbilityClass = {};

void DebugRecordAbilityTaskCreated(const UAbilityTask* NewTask)
{
	const UClass* ClassPtr = (NewTask != nullptr) ? NewTask->GetClass() : nullptr;
	if (ClassPtr != nullptr)
	{
		if (StaticAbilityTasksByClass.Contains(ClassPtr))
		{
			StaticAbilityTasksByClass[ClassPtr]++;
		}
		else
		{
			StaticAbilityTasksByClass.Add(ClassPtr, 1);
		}
	}
}

void UAbilityTask::DebugRecordAbilityTaskCreatedByAbility(const UObject* Ability)
{
	if (!AbilityTaskCVars::bRecordAbilityTaskSourceAbilityCounts || !AbilityTaskCVars::bRecordAbilityTaskCounts)
	{	// Both the more detailed and the basic recording is required for the detailed recording to work properly.
		return;
	}

	const UClass* ClassPtr = (Ability != nullptr) ? Ability->GetClass() : nullptr;
	if (ClassPtr != nullptr)
	{
		if (StaticAbilityTasksByAbilityClass.Contains(ClassPtr))
		{
			StaticAbilityTasksByAbilityClass[ClassPtr]++;
		}
		else
		{
			StaticAbilityTasksByAbilityClass.Add(ClassPtr, 1);
		}
	}
}

static void DebugRecordAbilityTaskDestroyed(const UAbilityTask* DestroyedTask)
{
	const UClass* ClassPtr = (DestroyedTask != nullptr) ? DestroyedTask->GetClass() : nullptr;
	if (ClassPtr != nullptr)
	{
		if (AbilityTaskCVars::bRecordAbilityTaskSourceAbilityCounts)
		{
			const UClass* AbilityClassPtr = (DestroyedTask->Ability != nullptr) ? DestroyedTask->Ability->GetClass() : nullptr;
			if (AbilityClassPtr != nullptr)
			{
				if (StaticAbilityTasksByAbilityClass.Contains(AbilityClassPtr))
				{
					StaticAbilityTasksByAbilityClass[AbilityClassPtr]--;

					if (StaticAbilityTasksByAbilityClass[AbilityClassPtr] <= 0)
					{
						StaticAbilityTasksByAbilityClass.Remove(AbilityClassPtr);
					}
				}
			}
		}

		if (StaticAbilityTasksByClass.Contains(ClassPtr))
		{
			StaticAbilityTasksByClass[ClassPtr]--;

			if (StaticAbilityTasksByClass[ClassPtr] <= 0)
			{
				StaticAbilityTasksByClass.Remove(ClassPtr);
			}
		}
	}
}

static void DebugPrintAbilityTasksByClass()
{
	if (AbilityTaskCVars::bRecordAbilityTaskCounts)
	{
		int32 AccumulatedAbilityTasks = 0;
		ABILITY_LOG(Display, TEXT("Logging global UAbilityTask counts:"));
		StaticAbilityTasksByClass.ValueSort(TGreater<int32>());
		for (const TPair<const UClass*, int32>& Pair : StaticAbilityTasksByClass)
		{
			FString SafeName = GetNameSafe(Pair.Key);
			ABILITY_LOG(Display, TEXT("- Class '%s': %d"), *SafeName, Pair.Value);
			AccumulatedAbilityTasks += Pair.Value;
		}

		const int32 UnaccountedAbilityTasks = GlobalAbilityTaskCount - AccumulatedAbilityTasks;
		if (UnaccountedAbilityTasks > 0)
		{
			// It's possible to allocate AbilityTasks before AbilityTaskCVars::bRecordAbilityTaskCounts was set to 'true', even if set via command line.
			// However, if this value increases during play, there is an issue.
			ABILITY_LOG(Display, TEXT("- Unknown (allocated before recording): %d"), UnaccountedAbilityTasks);
		}

		if (AbilityTaskCVars::bRecordAbilityTaskSourceAbilityCounts)
		{
			ABILITY_LOG(Display, TEXT("UAbilityTask counts per Ability Class:"));
			StaticAbilityTasksByAbilityClass.ValueSort(TGreater<int32>());
			for (const TPair<const UClass*, int32>& Pair : StaticAbilityTasksByAbilityClass)
			{
				FString SafeName = GetNameSafe(Pair.Key);
				ABILITY_LOG(Display, TEXT("- Ability Class '%s': %d"), *SafeName, Pair.Value);
			}
		}

		ABILITY_LOG(Display, TEXT("Total AbilityTask count: %d"), GlobalAbilityTaskCount);
	}
	else
	{
		ABILITY_LOG(Display, TEXT("Recording of UAbilityTask counts is disabled! Enable 'AbilitySystem.AbilityTask.Debug.RecordingEnabled' to turn on recording."))
	}
}
#endif  // !UE_BUILD_SHIPPING
