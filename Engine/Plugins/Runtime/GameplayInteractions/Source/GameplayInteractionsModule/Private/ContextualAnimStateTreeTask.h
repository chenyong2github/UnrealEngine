// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContextualAnimTypes.h"
#include "GameplayInteractionsTypes.h"
#include "ContextualAnimStateTreeTask.generated.h"

class UGameplayTask_PlayContextualAnim;
class UContextualAnimSceneAsset;

/**
 * FContextualAnimStateTreeTask instance data
 * @see FContextualAnimStateTreeTask
 */
USTRUCT()
struct FContextualAnimStateTreeTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	UContextualAnimSceneAsset* ContextualAnimAsset = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	AActor* InteractableObject = nullptr;

	UPROPERTY()
	UGameplayTask_PlayContextualAnim* Task = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Duration = 0.0f;

	UPROPERTY()
	float Time = 0.f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FName InteractorRole;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FName InteractableObjectRole;

	UPROPERTY(VisibleAnywhere, Category = Parameter)
	FGameplayInteractionAbortContext AbortContext;
};


/**
 * Builds context and creates GameplayTask that will control playback of a ContextualAnimScene
 */
USTRUCT(meta = (DisplayName = "Contextual Anim Task"))
struct FContextualAnimStateTreeTask : public FGameplayInteractionStateTreeTask
{
	GENERATED_BODY()

protected:
	typedef FContextualAnimStateTreeTaskInstanceData FInstanceDataType;

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;

	TStateTreeExternalDataHandle<AActor> InteractorActorHandle;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FName Section;
	
	UPROPERTY(EditAnywhere, Category = Parameter)
	FName ExitSection;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bEnabled = true;
};
