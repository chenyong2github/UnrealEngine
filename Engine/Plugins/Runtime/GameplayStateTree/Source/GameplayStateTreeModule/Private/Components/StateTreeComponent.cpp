// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/StateTreeComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayTasksComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTree.h"
#include "StateTreeEvaluatorBase.h"
#include "Conditions/StateTreeCondition_Common.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/World.h"

#define STATETREE_LOG(Verbosity, Format, ...) UE_VLOG(GetOwner(), LogStateTree, Verbosity, Format, ##__VA_ARGS__)
#define STATETREE_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG((Condition), GetOwner(), LogStateTree, Verbosity, Format, ##__VA_ARGS__)

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
	if (StateTreeRef.StateTree == nullptr)
	{
		STATETREE_LOG(Error, TEXT("%s: StateTree asset is not set, cannot initialize."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	if (!StateTreeContext.Init(*GetOwner(), *StateTreeRef.StateTree, EStateTreeStorage::Internal))
	{
		STATETREE_LOG(Error, TEXT("%s: Failed to init StateTreeContext."), ANSI_TO_TCHAR(__FUNCTION__));
	}
}

#if WITH_EDITOR
void UStateTreeComponent::PostLoad()
{
	Super::PostLoad();
	
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (StateTree_DEPRECATED != nullptr)
	{
		StateTreeRef.StateTree = StateTree_DEPRECATED;
		StateTreeRef.SyncParameters();
		StateTree_DEPRECATED = nullptr;
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
#endif //WITH_EDITOR

void UStateTreeComponent::UninitializeComponent()
{
}

bool UStateTreeComponent::SetContextRequirements()
{
	if (!StateTreeContext.IsValid())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	for (const FStateTreeExternalDataDesc& ItemDesc : StateTreeContext.GetExternalDataDescs())
	{
		if (ItemDesc.Struct != nullptr)
		{
			if (ItemDesc.Struct->IsChildOf(UWorldSubsystem::StaticClass()))
			{
				UWorldSubsystem* Subsystem = World->GetSubsystemBase(Cast<UClass>(const_cast<UStruct*>(ItemDesc.Struct)));
				StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(Subsystem));
			}
			else if (ItemDesc.Struct->IsChildOf(UActorComponent::StaticClass()))
			{
				UActorComponent* Component = GetOwner()->FindComponentByClass(Cast<UClass>(const_cast<UStruct*>(ItemDesc.Struct)));
				StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(Component));
			}
			else if (ItemDesc.Struct->IsChildOf(APawn::StaticClass()))
			{
				APawn* OwnerPawn = (AIOwner != nullptr) ? AIOwner->GetPawn() : Cast<APawn>(GetOwner());
				StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(OwnerPawn));
			}
			else if (ItemDesc.Struct->IsChildOf(AAIController::StaticClass()))
			{
				AAIController* OwnerController = (AIOwner != nullptr) ? AIOwner.Get() : Cast<AAIController>(GetOwner());
				StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(OwnerController));
			}
			else if (ItemDesc.Struct->IsChildOf(AActor::StaticClass()))
			{
				AActor* OwnerActor = (AIOwner != nullptr) ? AIOwner->GetPawn() : GetOwner();
				StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(OwnerActor));
			}
		}
	}

	return StateTreeContext.AreExternalDataViewsValid();
}

void UStateTreeComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsRunning || bIsPaused)
	{
		return;
	}
	
	if (SetContextRequirements())
	{
		StateTreeContext.Tick(DeltaTime);
	}
}

void UStateTreeComponent::StartLogic()
{
	STATETREE_LOG(Log, TEXT("%s: Start Logic"), ANSI_TO_TCHAR(__FUNCTION__));

	if (SetContextRequirements())
	{
		StateTreeContext.SetParameters(StateTreeRef.Parameters);

		StateTreeContext.Start();
		bIsRunning = true;
	}
}

void UStateTreeComponent::RestartLogic()
{
	STATETREE_LOG(Log, TEXT("%s: Restart Logic"), ANSI_TO_TCHAR(__FUNCTION__));

	if (SetContextRequirements())
	{
		StateTreeContext.SetParameters(StateTreeRef.Parameters);

		StateTreeContext.Start();
		bIsRunning = true;
	}
}

void UStateTreeComponent::StopLogic(const FString& Reason)
{
	STATETREE_LOG(Log, TEXT("%s: Stopping, reason: \'%s\'"), ANSI_TO_TCHAR(__FUNCTION__), *Reason);

	if (SetContextRequirements())
	{
		StateTreeContext.Stop();
		bIsRunning = false;
	}
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
