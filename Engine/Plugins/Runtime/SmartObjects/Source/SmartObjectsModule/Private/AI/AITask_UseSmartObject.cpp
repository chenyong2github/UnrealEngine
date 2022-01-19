// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/AITask_UseSmartObject.h"
#include "AIController.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectComponent.h"
#include "Tasks/AITask_MoveTo.h"
#include "SmartObjectSubsystem.h"
#include "GameplayBehavior.h"
#include "GameplayBehaviorConfig.h"
#include "GameplayBehaviorManager.h"
#include "GameplayTagAssetInterface.h"
#include "VisualLogger/VisualLogger.h"
#include "Misc/ScopeExit.h"
#include "DrawDebugHelpers.h"

UAITask_UseSmartObject::UAITask_UseSmartObject(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//bIsPausable = false;
	bBehaviorFinished = false;

	// Uncomment for debug draw while moving toward SO location
	// bTickingTask = true;
}

UAITask_UseSmartObject* UAITask_UseSmartObject::UseSmartObject(AAIController* Controller, AActor* SmartObjectActor, USmartObjectComponent* SmartObjectComponent, const bool bLockAILogic)
{
	if (SmartObjectComponent == nullptr && SmartObjectActor != nullptr)
	{
		SmartObjectComponent = Cast<USmartObjectComponent>(SmartObjectActor->GetComponentByClass(USmartObjectComponent::StaticClass()));
	}

	return (SmartObjectComponent && Controller)
		? UseSmartObjectComponent(*Controller, *SmartObjectComponent, bLockAILogic)
		: nullptr;
}

UAITask_UseSmartObject* UAITask_UseSmartObject::UseSmartObjectComponent(AAIController& Controller, const USmartObjectComponent& SmartObjectComponent, const bool bLockAILogic)
{
	AActor* Pawn = Controller.GetPawn();
	if (Pawn == nullptr)
	{
		UE_LOG(LogSmartObject, Error, TEXT("Pawn required on controller: %s."), *Controller.GetName());
		return nullptr;
	}

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(Pawn->GetWorld());
	if (SmartObjectSubsystem == nullptr)
	{
		UE_LOG(LogSmartObject, Error, TEXT("No SmartObjectSubsystem in world: %s."), *GetNameSafe(Pawn->GetWorld()));
		return nullptr;
	}

	FSmartObjectRequestFilter Filter;
	Filter.BehaviorDefinitionClass = USmartObjectGameplayBehaviorDefinition::StaticClass();
	const IGameplayTagAssetInterface* TagsSource = Cast<const IGameplayTagAssetInterface>(Pawn);
	if (TagsSource != nullptr)
	{
		TagsSource->GetOwnedGameplayTags(Filter.UserTags);
	}

	const FSmartObjectClaimHandle ClaimHandle = SmartObjectSubsystem->Claim(SmartObjectComponent.GetRegisteredHandle(), Filter);
	return UseClaimedSmartObject(Controller, ClaimHandle, bLockAILogic);
}

UAITask_UseSmartObject* UAITask_UseSmartObject::UseClaimedSmartObject(AAIController* Controller, const FSmartObjectClaimHandle ClaimHandle, const bool bLockAILogic)
{
	if (Controller == nullptr)
	{
		UE_LOG(LogSmartObject, Error, TEXT("AI Controller required to use smart object."));
		return nullptr;
	}
	
	AActor* Pawn = Controller->GetPawn();
	if (Pawn == nullptr)
	{
		UE_LOG(LogSmartObject, Error, TEXT("Pawn required on controller: %s."), *Controller->GetName());
		return nullptr;
	}

	return UseClaimedSmartObject(*Controller, ClaimHandle, bLockAILogic);
}

UAITask_UseSmartObject* UAITask_UseSmartObject::UseClaimedSmartObject(AAIController& Controller, const FSmartObjectClaimHandle ClaimHandle, const bool bLockAILogic)
{
	UAITask_UseSmartObject* MyTask = UAITask::NewAITask<UAITask_UseSmartObject>(Controller, EAITaskPriority::High);
	if (MyTask == nullptr)
	{
		return nullptr;
	}

	MyTask->SetClaimHandle(ClaimHandle);

	if (bLockAILogic)
	{
		MyTask->RequestAILogicLocking();
	}

	return MyTask;
}

void UAITask_UseSmartObject::SetClaimHandle(const FSmartObjectClaimHandle& Handle)
{
	ClaimedHandle = Handle;
}

void UAITask_UseSmartObject::TickTask(const float DeltaTime)
{
	Super::TickTask(DeltaTime);

#if ENABLE_DRAW_DEBUG
	if (MoveToTask != nullptr)
	{
		DrawDebugDirectionalArrow(OwnerController->GetWorld(), OwnerController->GetPawn()->GetActorLocation(), MoveToTask->GetMoveRequestRef().GetGoalLocation(), 10000.f, FColor::Yellow);
	}
#endif // ENABLE_DRAW_DEBUG
}

void UAITask_UseSmartObject::Activate()
{
	Super::Activate();

	bool bSuccess = false;
	ON_SCOPE_EXIT
	{
		if (!bSuccess)
		{
			EndTask();
		}
	};

	if (!ensureMsgf(ClaimedHandle.IsValid(), TEXT("SmartObject handle must be valid at this point.")))
	{
		return;
	}

	if (OwnerController->GetPawn() == nullptr)
	{
		UE_VLOG(OwnerController, LogSmartObject, Error, TEXT("Pawn required to use claim handle: %s."), *LexToString(ClaimedHandle));
		return;
	}

	USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(OwnerController->GetWorld());
	if (!ensureMsgf(SmartObjectSubsystem != nullptr, TEXT("SmartObjectSubsystem must be accessible at this point.")))
	{
		return;
	}

	const TOptional<FVector> GoalLocation = SmartObjectSubsystem->GetSlotLocation(ClaimedHandle);
	if (!ensureMsgf(GoalLocation.IsSet(), TEXT("Unable to extract a valid slot location.")))
	{
		return;
	}

	// Register a callback to be notified if the claimed slot became unavailable
	SmartObjectSubsystem->RegisterSlotInvalidationCallback(ClaimedHandle, FOnSlotInvalidated::CreateUObject(this, &UAITask_UseSmartObject::OnSlotInvalidated));

	FAIMoveRequest MoveReq(GoalLocation.GetValue());
	MoveReq.SetUsePathfinding(true);

	MoveToTask = UAITask::NewAITask<UAITask_MoveTo>(*OwnerController, *this, EAITaskPriority::High, TEXT("SmartObject"));
	MoveToTask->SetUp(OwnerController, MoveReq);
	MoveToTask->ReadyForActivation();

	bSuccess = true;
}

void UAITask_UseSmartObject::OnGameplayTaskDeactivated(UGameplayTask& Task)
{
	UAITask::NewAITask<UAITask_MoveTo>(*OwnerController, *this, EAITaskPriority::High, TEXT("SmartObject"));
	check(OwnerController);
	check(OwnerController->GetPawn());

	if (MoveToTask == &Task)
	{
		bool bBehaviorActive = false;

		if (Task.IsFinished())
		{
			UWorld* World = OwnerController->GetWorld();
			MoveToTask = nullptr;
			USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(World);
			if (ensure(SmartObjectSubsystem))
			{
				const USmartObjectGameplayBehaviorDefinition* SmartObjectGameplayBehaviorDefinition = SmartObjectSubsystem->Use<USmartObjectGameplayBehaviorDefinition>(ClaimedHandle);
				const UGameplayBehaviorConfig* GameplayBehaviorConfig = SmartObjectGameplayBehaviorDefinition != nullptr ? SmartObjectGameplayBehaviorDefinition->GameplayBehaviorConfig : nullptr;
				GameplayBehavior = GameplayBehaviorConfig != nullptr ? GameplayBehaviorConfig->GetBehavior(*World) : nullptr;
				if (GameplayBehavior != nullptr)
				{
					bBehaviorActive = UGameplayBehaviorManager::TriggerBehavior(*GameplayBehavior, *OwnerController->GetPawn(), GameplayBehaviorConfig);
					// Behavior can be successfully triggered AND ended synchronously. We are only interested to register callback when still running
					if (bBehaviorActive)
					{
						OnBehaviorFinishedNotifyHandle = GameplayBehavior->GetOnBehaviorFinishedDelegate().AddUObject(this, &UAITask_UseSmartObject::OnSmartObjectBehaviorFinished);
					}
				}
			}
		}

		if (bBehaviorActive == false)
		{
			// MoveToTask is deactivated by pausing, possibly from the BP of Behavior itself
			EndTask();
		}
	}

	Super::OnGameplayTaskDeactivated(Task);
}

void UAITask_UseSmartObject::OnDestroy(const bool bInOwnerFinished)
{
	Abort();

	if (ClaimedHandle.IsValid())
	{
		USmartObjectSubsystem* SmartObjectSubsystem = USmartObjectSubsystem::GetCurrent(OwnerController->GetWorld());
		check(SmartObjectSubsystem);
		SmartObjectSubsystem->Release(ClaimedHandle);
		ClaimedHandle.Invalidate();
	}

	if (TaskState != EGameplayTaskState::Finished)
	{
		OnFinished.Broadcast();

		Super::OnDestroy(bInOwnerFinished);
	}
}

void UAITask_UseSmartObject::OnSlotInvalidated(const FSmartObjectClaimHandle& ClaimHandle, ESmartObjectSlotState State)
{
	Abort();

	EndTask();
}

void UAITask_UseSmartObject::Abort()
{
	check(OwnerController);
	check(OwnerController->GetPawn());

	if (MoveToTask)
	{
		// clear before triggering 'the end' so that OnGameplayTaskDeactivated
		// ignores the incoming info about task end
		UAITask_MoveTo* Task = MoveToTask;
		MoveToTask = nullptr;
		Task->ExternalCancel();
	}
	else if (!bBehaviorFinished)
	{
		if (GameplayBehavior != nullptr)
		{
			GameplayBehavior->GetOnBehaviorFinishedDelegate().Remove(OnBehaviorFinishedNotifyHandle);
			GameplayBehavior->AbortBehavior(*OwnerController->GetPawn());
		}
	}
}

void UAITask_UseSmartObject::OnSmartObjectBehaviorFinished(UGameplayBehavior& Behavior, AActor& Avatar, const bool bInterrupted)
{
	// Adding an ensure in case the assumptions change in the future.
	ensure(OwnerController != nullptr);

	// make sure we handle the right pawn - we can get this notify for a different
	// Avatar if the behavior sending it out is not instanced (CDO is being used to perform actions)
	if (OwnerController && OwnerController->GetPawn() == &Avatar)
	{
		Behavior.GetOnBehaviorFinishedDelegate().Remove(OnBehaviorFinishedNotifyHandle);
		bBehaviorFinished = true;
		EndTask();
	}
}
