// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Tasks/AITask.h"
#include "SmartObjectRuntime.h"
#include "AITask_UseSmartObject.generated.h"


class AAIController;
class UAITask_MoveTo;
class UGameplayBehavior;

UCLASS()
class SMARTOBJECTSMODULE_API UAITask_UseSmartObject : public UAITask
{
	GENERATED_BODY()

public:
	UAITask_UseSmartObject(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "AI|Tasks", meta = (DefaultToSelf = "Controller" , BlueprintInternalUseOnly = "true"))
	static UAITask_UseSmartObject* UseSmartObject(AAIController* Controller, AActor* SmartObjectActor, USmartObjectComponent* SmartObjectComponent, bool bLockAILogic = true);

	/*void SetQueryTemplate(UEnvQuery& InQueryTemplate) { EQSRequest.QueryTemplate = &InQueryTemplate; }
	void SetNotificationDelegate(const FQueryFinishedSignature& InNotificationDelegate) { NotificationDelegate = InNotificationDelegate; }*/

	static UAITask_UseSmartObject* UseSmartObjectComponent(AAIController& Controller, USmartObjectComponent& SmartObjectComponent, bool bLockAILogic = true);

	void SetClaimHandle(const FSmartObjectClaimHandle& Handle);

	virtual void TickTask(float DeltaTime) override;

protected:
	virtual void Activate() override;

	virtual void OnGameplayTaskDeactivated(UGameplayTask& Task) override;

	virtual void OnDestroy(bool bInOwnerFinished) override;

	void Abort();

	void OnSmartObjectBehaviorFinished(UGameplayBehavior& Behavior, AActor& Avatar, const bool bInterrupted);

	void OnSlotInvalidated(const FSmartObjectClaimHandle& ClaimHandle, const ESmartObjectSlotState State);

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
