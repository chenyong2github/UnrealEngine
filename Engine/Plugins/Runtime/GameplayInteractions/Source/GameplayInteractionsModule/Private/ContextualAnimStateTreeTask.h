// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayInteractionsTypes.h"
#include "ContextualAnimStateTreeTask.generated.h"

class UContextualAnimSceneAsset;
class UAnimMontage;

USTRUCT()
struct FContextualAnimStateTreeTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	TObjectPtr<UContextualAnimSceneAsset> ContextualAnimAsset = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	TObjectPtr<AActor> InteractableObject = nullptr;
	
	UPROPERTY(EditAnywhere, Category = Parameter)
	float Duration = 0.0f;

	UPROPERTY()
	float ComputedDuration = 0.0f;

	/** Accumulated time used to stop task if a montage is set */
	UPROPERTY()
	float Time = 0.f;
};


USTRUCT(meta = (DisplayName = "Contextual Anim Task"))
struct FContextualAnimStateTreeTask : public FGameplayInteractionStateTreeTask
{
	GENERATED_BODY()

	typedef FContextualAnimStateTreeTaskInstanceData InstanceDataType;

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return InstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	
	TStateTreeInstanceDataPropertyHandle<TObjectPtr<UContextualAnimSceneAsset>> ContextualAnimAssetHandle;
	TStateTreeInstanceDataPropertyHandle<TObjectPtr<AActor>> InteractableObjectHandle;
	TStateTreeInstanceDataPropertyHandle<float> DurationHandle;
	TStateTreeInstanceDataPropertyHandle<float> ComputedDurationHandle;
	TStateTreeInstanceDataPropertyHandle<float> TimeHandle;

	TStateTreeExternalDataHandle<AActor> InteractorActorHandle;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FName InteractorRole;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FName InteractableObjectRole;
};
