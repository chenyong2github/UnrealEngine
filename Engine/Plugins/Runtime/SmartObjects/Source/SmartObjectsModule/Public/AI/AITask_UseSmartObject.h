// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/AITask.h"
#include "SmartObjectRuntime.h"
#include "AITask_UseSmartObject.generated.h"


class AAIController;
class UAITask_MoveTo;
class UGameplayBehavior;
class USmartObjectComponent;

UCLASS()
class SMARTOBJECTSMODULE_API UAITask_UseSmartObject : public UAITask
{
	GENERATED_BODY()

public:
	UAITask_UseSmartObject(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "AI|Tasks", meta = (DefaultToSelf = "Controller" , BlueprintInternalUseOnly = "true"))
	static UAITask_UseSmartObject* UseSmartObject(AAIController* Controller, AActor* SmartObjectActor, USmartObjectComponent* SmartObjectComponent, bool bLockAILogic = true);

	UFUNCTION(BlueprintCallable, Category = "AI|Tasks", meta = (DefaultToSelf = "Controller" , BlueprintInternalUseOnly = "true"))
	static UAITask_UseSmartObject* UseClaimedSmartObject(AAIController* Controller, FSmartObjectClaimHandle ClaimHandle, bool bLockAILogic = true);

	void SetClaimHandle(const FSmartObjectClaimHandle& Handle);

	virtual void TickTask(float DeltaTime) override;

protected:
	virtual void Activate() override;

	virtual void OnGameplayTaskDeactivated(UGameplayTask& Task) override;

	virtual void OnDestroy(bool bInOwnerFinished) override;

	void Abort();

	void OnSmartObjectBehaviorFinished(UGameplayBehavior& Behavior, AActor& Avatar, const bool bInterrupted);

	void OnSlotInvalidated(const FSmartObjectClaimHandle& ClaimHandle, const ESmartObjectSlotState State);

	static UAITask_UseSmartObject* UseSmartObjectComponent(AAIController& Controller, const USmartObjectComponent& SmartObjectComponent, bool bLockAILogic);
	static UAITask_UseSmartObject* UseClaimedSmartObject(AAIController& Controller, FSmartObjectClaimHandle ClaimHandle, bool bLockAILogic);

protected:
	UPROPERTY(BlueprintAssignable)
	FGenericGameplayTaskDelegate OnFinished;

	UPROPERTY()
	UAITask_MoveTo* MoveToTask;

	UPROPERTY()
	UGameplayBehavior* GameplayBehavior;

	FSmartObjectClaimHandle ClaimedHandle;
	FDelegateHandle OnBehaviorFinishedNotifyHandle;
	uint32 bBehaviorFinished : 1;
};
