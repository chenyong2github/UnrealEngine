// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Templates/UniquePtr.h"
#include "Misc/AsyncTaskNotification.h"

#include "EditorUtilityTask.generated.h"

class UEditorUtilitySubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEditorUtilityTaskDynamicDelegate, UEditorUtilityTask*, Task);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEditorUtilityTaskDelegate, UEditorUtilityTask* /*Task*/);

/**
 * 
 */
UCLASS(Abstract, Blueprintable, meta = (ShowWorldContextPin))
class BLUTILITY_API UEditorUtilityTask : public UObject
{
	GENERATED_BODY()

public:
	FOnEditorUtilityTaskDelegate OnFinished;

public:
	UEditorUtilityTask();

	UFUNCTION()
	void Run();

	virtual UWorld* GetWorld() const override;

public:
	UFUNCTION(BlueprintCallable, Category=Task)
	void FinishExecutingTask();

	UFUNCTION(BlueprintCallable, Category = Task)
	void SetTaskNotificationText(const FText& Text);

	// Calls CancelRequested() and ReceiveCancelRequested()
	void RequestCancel();

	UFUNCTION(BlueprintCallable, Category = Task)
	bool WasCancelRequested() const;

protected:
	virtual void BeginExecution() {}
	virtual void CancelRequested() {}

	UFUNCTION(BlueprintImplementableEvent, Category=Task, meta=(DisplayName="BeginExecution"))
	void ReceiveBeginExecution();

	UFUNCTION(BlueprintImplementableEvent, Category=Task, meta=(DisplayName="CancelRequested"))
	void ReceiveCancelRequested();

private:
	void CreateNotification();

	// Calls BeginExecution() and ReceiveBeginExecution()
	void StartExecutingTask();

private:
	UPROPERTY(Transient)
	TObjectPtr<UEditorUtilitySubsystem> MyTaskManager;

	UPROPERTY(Transient)
	TObjectPtr<UEditorUtilityTask> MyParentTask;

	UPROPERTY(Transient)
	bool bCancelRequested = false;

	bool Cached_GIsRunningUnattendedScript = false;

	TUniquePtr<FAsyncTaskNotification> TaskNotification;

	friend UEditorUtilitySubsystem;
};
