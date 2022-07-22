// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimStateTreeTask.h"

#include "ContextualAnimSceneAsset.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"
#include "GameplayTask_PlayContextualAnim.h"
#include "VisualLogger/VisualLogger.h"


#define ST_ANIM_TASK_LOG(Verbosity, Format, ...) UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Verbosity, TEXT("[%s] ") Format, *StaticStruct()->GetName(), ##__VA_ARGS__)
#define ST_ANIM_TASK_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG_UELOG((Condition), Context.GetOwner(), LogStateTree, Verbosity, TEXT("[%s] ") Format, *StaticStruct()->GetName(), ##__VA_ARGS__)

/**
 * FContextualAnimStateTreeTask
 */
bool FContextualAnimStateTreeTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(InteractorActorHandle);
	
	return true;
}

EStateTreeRunStatus FContextualAnimStateTreeTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	if (!bEnabled)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	const UContextualAnimSceneAsset* SceneAsset = InstanceData.ContextualAnimAsset;
	if (SceneAsset == nullptr)
	{
		ST_ANIM_TASK_LOG(Error, TEXT("ContextualAnimSceneAsset required."));
		return EStateTreeRunStatus::Failed;
	}
	AActor& Interactor = Context.GetExternalData(InteractorActorHandle);
	AActor* InteractableObject = InstanceData.InteractableObject;
	if (InteractableObject == nullptr)
	{
		ST_ANIM_TASK_LOG(Error, TEXT("Interactable object actor required."));
		return EStateTreeRunStatus::Failed;
	}
	
	// Create task which will also be simulated on the clients
	UE_TRANSITIONAL_OBJECT_PTR(UGameplayTask_PlayContextualAnim)& Task = InstanceData.Task;

	Task = UGameplayTask_PlayContextualAnim::PlayContextualAnim(
		&Interactor,
		InstanceData.InteractorRole,
		InteractableObject,
		InstanceData.InteractableObjectRole,
		Section,
		ExitSection,
		SceneAsset
	);
	
	if (Task == nullptr)
	{
		ST_ANIM_TASK_LOG(Error, TEXT("Unable to create gameplay task."));
		return EStateTreeRunStatus::Failed;
	}

	Task->ReadyForActivation();
	
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FContextualAnimStateTreeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;

	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	const float Duration = InstanceData.Duration;
	if (Duration > 0.f)
	{
		float& Time = InstanceData.Time;
		Time += DeltaTime;
		if (Time >= Duration)
		{
			Status = EStateTreeRunStatus::Succeeded;
		}
	}
	else
	{
		UE_TRANSITIONAL_OBJECT_PTR(UGameplayTask_PlayContextualAnim)& Task = InstanceData.Task;
		if (Task == nullptr)
		{
			return EStateTreeRunStatus::Running;
		}

		const EPlayContextualAnimStatus GameplayTaskStatus = Task->GetStatus();
		ensureAlwaysMsgf(GameplayTaskStatus != EPlayContextualAnimStatus::Unset, TEXT("TaskStatus is expected to be set before ticking the task"));
		if (GameplayTaskStatus != EPlayContextualAnimStatus::Playing)
		{
			Status = (GameplayTaskStatus == EPlayContextualAnimStatus::DonePlaying ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed);
		}
	}

	return Status;
}

void FContextualAnimStateTreeTask::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	if (!bEnabled)
	{
		return;
	}

	FInstanceDataType& InstanceData = Context.GetInstanceData<FInstanceDataType>(*this);
	UE_TRANSITIONAL_OBJECT_PTR(UGameplayTask_PlayContextualAnim)& Task = InstanceData.Task;
	if (Task == nullptr)
	{
		ST_ANIM_TASK_LOG(Error, TEXT("Unable to access gameplay task."));
		return;
	}

	// Update exit parameters if we are handling an interruption
	const FGameplayInteractionAbortContext& AbortContext = InstanceData.AbortContext;
	if (AbortContext.Reason != EGameplayInteractionAbortReason::Unset)
	{
		switch (AbortContext.Reason)
		{
		case EGameplayInteractionAbortReason::ExternalAbort:	Task->SetExit(EPlayContextualAnimExitMode::DefaultExit, ExitSection);	break;
		case EGameplayInteractionAbortReason::InternalAbort:	Task->SetExit(EPlayContextualAnimExitMode::Teleport, NAME_None);		break;
		//case EGameplayInteractionAbortReason::SomeOtherValue:	Task->SetExit(EPlayContextualAnimExitMode::FastExit, FastExitSection);	break;
		case EGameplayInteractionAbortReason::Unset:
			// no need to update the default setup
			break;
		default:
			ensureMsgf(false, TEXT("Unhandled abort reason: %s"), *UEnum::GetValueAsString(AbortContext.Reason));
		}
	}
}
