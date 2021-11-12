// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/StateTreeComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayTasksComponent.h"
#include "VisualLogger/VisualLoggerTypes.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTree.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"

#define STATETREE_LOG(Verbosity, Format, ...) UE_VLOG(GetOwner(), LogStateTree, Verbosity, Format, ##__VA_ARGS__)
#define STATETREE_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG((Condition), GetOwner(), LogStateTree, Verbosity, Format, ##__VA_ARGS__)


//////////////////////////////////////////////////////////////////////////
// UBrainComponentStateTreeSchema

bool UBrainComponentStateTreeSchema::IsStructAllowed(const UScriptStruct* InScriptStruct) const
{
	// @todo: Proper base class for actor nodes.
	return InScriptStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct())
			|| InScriptStruct->IsChildOf(FStateTreeTaskBase::StaticStruct())
			|| InScriptStruct->IsChildOf(FStateTreeConditionBase::StaticStruct());
}


//////////////////////////////////////////////////////////////////////////
// UStateTreeComponent

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

	if (!StateTreeContext.Init(*this, *StateTree, EStateTreeStorage::Internal))
	{
		STATETREE_LOG(Error, TEXT("%s: Failed to init StateTreeContext."), ANSI_TO_TCHAR(__FUNCTION__));
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

	StateTreeContext.Tick(DeltaTime);
}

void UStateTreeComponent::StartLogic()
{
	STATETREE_LOG(Log, TEXT("%s: Start Logic"), ANSI_TO_TCHAR(__FUNCTION__));

	// TODO: Move this to UStateTree
	StateTree->ResolvePropertyPaths();

	StateTreeContext.Start();

	bIsRunning = true;
}

void UStateTreeComponent::RestartLogic()
{
	STATETREE_LOG(Log, TEXT("%s: Restart Logic"), ANSI_TO_TCHAR(__FUNCTION__));
	StateTreeContext.Start();

	bIsRunning = true;
}

void UStateTreeComponent::StopLogic(const FString& Reason)
{
	STATETREE_LOG(Log, TEXT("%s: Stopping, reason: \'%s\'"), ANSI_TO_TCHAR(__FUNCTION__), *Reason);

	StateTreeContext.Stop();

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

#if WITH_GAMEPLAY_DEBUGGER
FString UStateTreeComponent::GetDebugInfoString() const
{
	return StateTreeContext.GetDebugInfoString();
}
#endif // WITH_GAMEPLAY_DEBUGGER

#undef STATETREE_LOG
#undef STATETREE_CLOG
