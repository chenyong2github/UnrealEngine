// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimStateTreeTask.h"

#include "ContextualAnimSceneAsset.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"
#include "GameplayTask_PlayContextualAnim.h"
#include "VisualLogger/VisualLogger.h"
#include "AIResources.h"


#define ST_ANIM_TASK_LOG(Verbosity, Format, ...) UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Verbosity, TEXT("[%s] ") Format, *StaticStruct()->GetName(), ##__VA_ARGS__)
#define ST_ANIM_TASK_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG_UELOG((Condition), Context.GetOwner(), LogStateTree, Verbosity, TEXT("[%s] ") Format, *StaticStruct()->GetName(), ##__VA_ARGS__)

/**
 * FContextualAnimStateTreeTask
 */
bool FContextualAnimStateTreeTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(InteractorActorHandle);
	Linker.LinkInstanceDataProperty(InteractorRoleHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, InteractorRole));

	Linker.LinkInstanceDataProperty(InteractableObjectHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, InteractableObject));
	Linker.LinkInstanceDataProperty(InteractableObjectRoleHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, InteractableObjectRole));

	Linker.LinkInstanceDataProperty(ContextualAnimAssetHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, ContextualAnimAsset));

	Linker.LinkInstanceDataProperty(TaskHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, Task));
	Linker.LinkInstanceDataProperty(AbortContextHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, AbortContext));

	Linker.LinkInstanceDataProperty(DurationHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, Duration));
	Linker.LinkInstanceDataProperty(TimeHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, Time));
	
	return true;
}

EStateTreeRunStatus FContextualAnimStateTreeTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	if (!bEnabled)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	const UContextualAnimSceneAsset* SceneAsset = Context.GetInstanceData(ContextualAnimAssetHandle);
	if (SceneAsset == nullptr)
	{
		ST_ANIM_TASK_LOG(Error, TEXT("ContextualAnimSceneAsset required."));
		return EStateTreeRunStatus::Failed;
	}
	AActor& Interactor = Context.GetExternalData(InteractorActorHandle);
	AActor* InteractableObject = Context.GetInstanceData(InteractableObjectHandle);
	if (InteractableObject == nullptr)
	{
		ST_ANIM_TASK_LOG(Error, TEXT("Interactable object actor required."));
		return EStateTreeRunStatus::Failed;
	}
	
	// Create task which will also be simulated on the clients
	UGameplayTask_PlayContextualAnim*& Task = Context.GetInstanceData(TaskHandle);

	Task = UGameplayTask_PlayContextualAnim::PlayContextualAnim(
		&Interactor,
		Context.GetInstanceData(InteractorRoleHandle),
		InteractableObject,
		Context.GetInstanceData(InteractableObjectRoleHandle),
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

	const float Duration = Context.GetInstanceData(DurationHandle);
	if (Duration > 0.f)
	{
		float& Time = Context.GetInstanceData(TimeHandle);
		Time += DeltaTime;
		if (Time >= Duration)
		{
			Status = EStateTreeRunStatus::Succeeded;
		}
	}
	else
	{
		UGameplayTask_PlayContextualAnim*& Task = Context.GetInstanceData(TaskHandle);
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
	
	UGameplayTask_PlayContextualAnim*& Task = Context.GetInstanceData(TaskHandle);
	if (Task == nullptr)
	{
		ST_ANIM_TASK_LOG(Error, TEXT("Unable to access gameplay task."));
		return;
	}

	// Update exit parameters if we are handling an interruption
	const FGameplayInteractionAbortContext& AbortContext = Context.GetInstanceData(AbortContextHandle);
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
