// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/StateTreeComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayTasksComponent.h"
#include "VisualLogger/VisualLoggerTypes.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTree.h"
#include "StateTreeInstance.h"
#include "AIController.h"

#define STATETREE_LOG(Verbosity, Format, ...) UE_VLOG(GetOwner(), LogStateTree, Verbosity, Format, ##__VA_ARGS__)
#define STATETREE_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG((Condition), GetOwner(), LogStateTree, Verbosity, Format, ##__VA_ARGS__)

UStateTreeComponent::UStateTreeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWantsInitializeComponent = true;
	bIsRunning = false;
	bIsPaused = false;
}

void UStateTreeComponent::InitializeComponent()
{
	if (StateTree == nullptr)
	{
		STATETREE_LOG(Error, TEXT("%s: StateTree asset is not set, cannot initialize."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	if (!StateTreeInstance.Init(*this, *StateTree))
	{
		STATETREE_LOG(Error, TEXT("%s: Failed to init StateTreeInstance."), ANSI_TO_TCHAR(__FUNCTION__));
	}
}

void UStateTreeComponent::UninitializeComponent()
{
}

void UStateTreeComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsRunning || bIsPaused)
	{
		return;
	}

	StateTreeInstance.Tick(DeltaTime);
}

void UStateTreeComponent::StartLogic()
{
	STATETREE_LOG(Log, TEXT("%s: Start Logic"), ANSI_TO_TCHAR(__FUNCTION__));

	// TODO: Move this to UStateTree
	StateTree->ResolvePropertyPaths();

	StateTreeInstance.Start();

	bIsRunning = true;
}

void UStateTreeComponent::RestartLogic()
{
	STATETREE_LOG(Log, TEXT("%s: Restart Logic"), ANSI_TO_TCHAR(__FUNCTION__));
	StateTreeInstance.Start();

	bIsRunning = true;
}

void UStateTreeComponent::StopLogic(const FString& Reason)
{
	STATETREE_LOG(Log, TEXT("%s: Stopping, reason: \'%s\'"), ANSI_TO_TCHAR(__FUNCTION__), *Reason);

	StateTreeInstance.Stop();

	bIsRunning = false;
}

void UStateTreeComponent::Cleanup()
{
}

void UStateTreeComponent::PauseLogic(const FString& Reason)
{
	STATETREE_LOG(Log, TEXT("%s: Execution updates: PAUSED (%s)"), ANSI_TO_TCHAR(__FUNCTION__), *Reason);
	bIsPaused = true;
}

EAILogicResuming::Type UStateTreeComponent::ResumeLogic(const FString& Reason)
{
	STATETREE_LOG(Log, TEXT("%s: Execution updates: RESUMED (%s)"), ANSI_TO_TCHAR(__FUNCTION__), *Reason);

	const EAILogicResuming::Type SuperResumeResult = Super::ResumeLogic(Reason);

	if (!!bIsPaused)
	{
		bIsPaused = false;

		if (SuperResumeResult == EAILogicResuming::Continue)
		{
			// Nop
		}
		else if (SuperResumeResult == EAILogicResuming::RestartedInstead)
		{
			RestartLogic();
		}
	}

	return SuperResumeResult;
}

bool UStateTreeComponent::IsRunning() const
{
	return bIsRunning;
}

bool UStateTreeComponent::IsPaused() const
{
	return bIsPaused;
}

UGameplayTasksComponent* UStateTreeComponent::GetGameplayTasksComponent(const UGameplayTask& Task) const
{
	const UAITask* AITask = Cast<const UAITask>(&Task);
	return (AITask && AITask->GetAIController()) ? AITask->GetAIController()->GetGameplayTasksComponent(Task) : Task.GetGameplayTasksComponent();
}

AActor* UStateTreeComponent::GetGameplayTaskOwner(const UGameplayTask* Task) const
{
	if (Task == nullptr)
	{
		return GetAIOwner();
	}

	const UAITask* AITask = Cast<const UAITask>(Task);
	if (AITask)
	{
		return AITask->GetAIController();
	}

	const UGameplayTasksComponent* TasksComponent = Task->GetGameplayTasksComponent();
	return TasksComponent ? TasksComponent->GetGameplayTaskOwner(Task) : nullptr;
}

AActor* UStateTreeComponent::GetGameplayTaskAvatar(const UGameplayTask* Task) const
{
	if (Task == nullptr)
	{
		return GetAIOwner() ? GetAIOwner()->GetPawn() : nullptr;
	}

	const UAITask* AITask = Cast<const UAITask>(Task);
	if (AITask)
	{
		return AITask->GetAIController() ? AITask->GetAIController()->GetPawn() : nullptr;
	}

	const UGameplayTasksComponent* TasksComponent = Task->GetGameplayTasksComponent();
	return TasksComponent ? TasksComponent->GetGameplayTaskAvatar(Task) : nullptr;
}

uint8 UStateTreeComponent::GetGameplayTaskDefaultPriority() const
{
	return static_cast<uint8>(EAITaskPriority::AutonomousAI);
}

void UStateTreeComponent::OnGameplayTaskInitialized(UGameplayTask& Task)
{
	const UAITask* AITask = Cast<const UAITask>(&Task);
	if (AITask && (AITask->GetAIController() == nullptr))
	{
		// this means that the task has either been created without specifying 
		// UAITAsk::OwnerController's value (like via BP's Construct Object node)
		// or it has been created in C++ with inappropriate function
		UE_LOG(LogStateTree, Error, TEXT("Missing AIController in AITask %s"), *AITask->GetName());
	}
}

#if ENABLE_VISUAL_LOG
void UStateTreeComponent::DescribeSelfToVisLog(struct FVisualLogEntry* Snapshot) const
{
	if (!IsValid(this))
	{
		return;
	}

	Super::DescribeSelfToVisLog(Snapshot);

	if (Snapshot)
	{
		StateTreeInstance.DescribeSelfToVisLog(*Snapshot);
	}
}
#endif // ENABLE_VISUAL_LOG

#if WITH_GAMEPLAY_DEBUGGER
FString UStateTreeComponent::GetDebugInfoString() const
{
	return StateTreeInstance.GetDebugInfoString();
}
#endif // WITH_GAMEPLAY_DEBUGGER

#undef STATETREE_LOG
#undef STATETREE_CLOG
