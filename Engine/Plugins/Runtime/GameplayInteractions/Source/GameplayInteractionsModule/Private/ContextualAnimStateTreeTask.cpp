// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimStateTreeTask.h"
#include "ContextualAnimManager.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimTypes.h"
#include "StateTreeExecutionContext.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "StateTreeLinker.h"

struct FDataRegistryLookup;
struct FDataRegistryId;
struct FMassEntityHandle;

bool FContextualAnimStateTreeTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(ContextualAnimAssetHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, ContextualAnimAsset));
	Linker.LinkInstanceDataProperty(InteractableObjectHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, InteractableObject));
	Linker.LinkInstanceDataProperty(DurationHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, Duration));
	Linker.LinkInstanceDataProperty(ComputedDurationHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, ComputedDuration));
	Linker.LinkInstanceDataProperty(TimeHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, Time));

	Linker.LinkExternalData(InteractorActorHandle);
	
	return true;
}

EStateTreeRunStatus FContextualAnimStateTreeTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	float& Time = Context.GetInstanceData(TimeHandle);
	Time = 0.f;
	
	float& ComputedDuration = Context.GetInstanceData(ComputedDurationHandle);
	ComputedDuration = Context.GetInstanceData(DurationHandle);

	AActor* InteractableObject = Context.GetInstanceData(InteractableObjectHandle);
	AActor* Interactor = Context.GetExternalDataPtr(InteractorActorHandle);
	const UContextualAnimSceneAsset* ContextualAnimAsset = Context.GetInstanceData(ContextualAnimAssetHandle);
	UAnimMontage* Montage = nullptr;

	// If we have a target use that to find the best contextual anim to play
	if (ContextualAnimAsset != nullptr && InteractableObject != nullptr && Interactor != nullptr)
	{
		const FTransform& InteractableObjectTransform = InteractableObject->GetTransform();
		const FTransform& InteractorTransform = Interactor->GetTransform();

		FContextualAnimQueryParams ContextualAnimQueryParams;
		ContextualAnimQueryParams.bComplexQuery = true;
		ContextualAnimQueryParams.bFindAnimStartTime = true;
		ContextualAnimQueryParams.QueryTransform = InteractorTransform;
		
		// If we don't find a good sync point, grab the closest one.
		FContextualAnimQueryResult ContextualAnimQueryResult;
		if (!ContextualAnimAsset->Query(InteractorRole, ContextualAnimQueryResult, ContextualAnimQueryParams, InteractableObjectTransform))
		{
			ContextualAnimQueryParams.bComplexQuery = false;
			ContextualAnimAsset->Query(InteractorRole, ContextualAnimQueryResult, ContextualAnimQueryParams, InteractableObjectTransform);
		}

		Montage = ContextualAnimQueryResult.Animation.Get();
		if (Montage != nullptr)
		{
			UContextualAnimManager* ContextualAnimManager = UContextualAnimManager::Get(Context.GetWorld());
			FContextualAnimStartSceneParams StartSceneParams;
			StartSceneParams.RoleToActorMap.Add(InteractorRole, Interactor);
			StartSceneParams.RoleToActorMap.Add(InteractableObjectRole, InteractableObject);
	
			UContextualAnimSceneInstance* SceneInstance = ContextualAnimManager->TryStartScene(*ContextualAnimAsset, StartSceneParams);
			if (SceneInstance == nullptr)
			{
				return EStateTreeRunStatus::Failed;
			}
			// @todo: listen anim completed event
		}
	}

	if (Montage == nullptr)
	{
		return EStateTreeRunStatus::Failed;	
	}
	
	// Grab the task duration from the montage.
	ComputedDuration = Montage->GetPlayLength();
	
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FContextualAnimStateTreeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const float ComputedDuration = Context.GetInstanceData(ComputedDurationHandle);
	float& Time = Context.GetInstanceData(TimeHandle);

	Time += DeltaTime;
	return ComputedDuration <= 0.0f ? EStateTreeRunStatus::Running : (Time < ComputedDuration ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded);
}