// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/StateTreeBrainComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayTasksComponent.h"
#include "VisualLogger/VisualLoggerTypes.h"
#include "VisualLogger/VisualLogger.h"
#include "StateTree.h"
#include "StateTreeEvaluatorBase.h"
#include "Conditions/StateTreeCondition_Common.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"

#define STATETREE_LOG(Verbosity, Format, ...) UE_VLOG(GetOwner(), LogStateTree, Verbosity, Format, ##__VA_ARGS__)
#define STATETREE_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG((Condition), GetOwner(), LogStateTree, Verbosity, Format, ##__VA_ARGS__)


//////////////////////////////////////////////////////////////////////////
// UBrainComponentStateTreeSchema

bool UBrainComponentStateTreeSchema::IsStructAllowed(const UScriptStruct* InScriptStruct) const
{
	return InScriptStruct->IsChildOf(FStateTreeConditionCommonBase::StaticStruct())
			|| InScriptStruct->IsChildOf(FStateTreeEvaluatorCommonBase::StaticStruct());
}

bool UBrainComponentStateTreeSchema::IsClassAllowed(const UClass* InClass) const
{
	return IsChildOfBlueprintBase(InClass);
}

bool UBrainComponentStateTreeSchema::IsExternalItemAllowed(const UStruct& InStruct) const
{
	// Actors and components.
	return InStruct.IsChildOf(AActor::StaticClass())
			|| InStruct.IsChildOf(APawn::StaticClass())
			|| InStruct.IsChildOf(AAIController::StaticClass())
			|| InStruct.IsChildOf(UActorComponent::StaticClass())
			|| InStruct.IsChildOf(UWorldSubsystem::StaticClass());
}

//////////////////////////////////////////////////////////////////////////
// UStateTreeBrainComponent

UStateTreeBrainComponent::UStateTreeBrainComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWantsInitializeComponent = true;
	bIsRunning = false;
	bIsPaused = false;
}

void UStateTreeBrainComponent::InitializeComponent()
{
	if (StateTree == nullptr)
	{
		STATETREE_LOG(Error, TEXT("%s: StateTree asset is not set, cannot initialize."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	if (!StateTreeContext.Init(*GetOwner(), *StateTree, EStateTreeStorage::Internal))
	{
		STATETREE_LOG(Error, TEXT("%s: Failed to init StateTreeContext."), ANSI_TO_TCHAR(__FUNCTION__));
	}
}

void UStateTreeBrainComponent::UninitializeComponent()
{
}

bool UStateTreeBrainComponent::SetContextRequirements()
{
	if (!StateTreeContext.IsValid())
	{
		return false;
	}
	
	UWorld* World = GetWorld();
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
			else if (ItemDesc.Struct->IsChildOf(AActor::StaticClass()))
			{
				if (AIOwner != nullptr)
				{
					AActor* OwnerActor = Cast<AActor>(AIOwner->GetPawn());
					StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(OwnerActor));
				}
				else
				{
					AActor* OwnerActor = Cast<AActor>(GetOwner());
					StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(OwnerActor));
				}
			}
			else if (ItemDesc.Struct->IsChildOf(APawn::StaticClass()))
			{
				if (AIOwner != nullptr)
				{
					APawn* OwnerActor = Cast<APawn>(AIOwner->GetPawn());
					StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(OwnerActor));
				}
				else
				{
					APawn* OwnerActor = Cast<APawn>(GetOwner());
					StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(OwnerActor));
				}
			}
			else if (ItemDesc.Struct->IsChildOf(AAIController::StaticClass()))
			{
				if (AIOwner != nullptr)
				{
					StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(AIOwner));
				}
			}
		}
	}

	return StateTreeContext.AreExternalDataViewsValid();
}

void UStateTreeBrainComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
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

void UStateTreeBrainComponent::StartLogic()
{
	STATETREE_LOG(Log, TEXT("%s: Start Logic"), ANSI_TO_TCHAR(__FUNCTION__));

	// TODO: Move this to UStateTree
	StateTree->ResolvePropertyPaths();

	if (SetContextRequirements())
	{
		StateTreeContext.Start();
		bIsRunning = true;
	}
}

void UStateTreeBrainComponent::RestartLogic()
{
	STATETREE_LOG(Log, TEXT("%s: Restart Logic"), ANSI_TO_TCHAR(__FUNCTION__));

	if (SetContextRequirements())
	{
		StateTreeContext.Start();
		bIsRunning = true;
	}
}

void UStateTreeBrainComponent::StopLogic(const FString& Reason)
{
	STATETREE_LOG(Log, TEXT("%s: Stopping, reason: \'%s\'"), ANSI_TO_TCHAR(__FUNCTION__), *Reason);

	if (SetContextRequirements())
	{
		StateTreeContext.Stop();
		bIsRunning = false;
	}
}

void UStateTreeBrainComponent::Cleanup()
{
}

void UStateTreeBrainComponent::PauseLogic(const FString& Reason)
{
	STATETREE_LOG(Log, TEXT("%s: Execution updates: PAUSED (%s)"), ANSI_TO_TCHAR(__FUNCTION__), *Reason);
	bIsPaused = true;
}

EAILogicResuming::Type UStateTreeBrainComponent::ResumeLogic(const FString& Reason)
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

bool UStateTreeBrainComponent::IsRunning() const
{
	return bIsRunning;
}

bool UStateTreeBrainComponent::IsPaused() const
{
	return bIsPaused;
}

UGameplayTasksComponent* UStateTreeBrainComponent::GetGameplayTasksComponent(const UGameplayTask& Task) const
{
	const UAITask* AITask = Cast<const UAITask>(&Task);
	return (AITask && AITask->GetAIController()) ? AITask->GetAIController()->GetGameplayTasksComponent(Task) : Task.GetGameplayTasksComponent();
}

AActor* UStateTreeBrainComponent::GetGameplayTaskOwner(const UGameplayTask* Task) const
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

AActor* UStateTreeBrainComponent::GetGameplayTaskAvatar(const UGameplayTask* Task) const
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

uint8 UStateTreeBrainComponent::GetGameplayTaskDefaultPriority() const
{
	return static_cast<uint8>(EAITaskPriority::AutonomousAI);
}

void UStateTreeBrainComponent::OnGameplayTaskInitialized(UGameplayTask& Task)
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
FString UStateTreeBrainComponent::GetDebugInfoString() const
{
	return StateTreeContext.GetDebugInfoString();
}
#endif // WITH_GAMEPLAY_DEBUGGER

#undef STATETREE_LOG
#undef STATETREE_CLOG
