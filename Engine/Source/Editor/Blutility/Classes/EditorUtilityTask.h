// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Templates/UniquePtr.h"
#include "Misc/AsyncTaskNotification.h"

#include "EditorUtilityTask.generated.h"

class UEditorUtilitySubsystem;

/**
 * 
 */
UCLASS(Abstract, Blueprintable, meta = (ShowWorldContextPin))
class BLUTILITY_API UEditorUtilityTask : public UObject
{
	GENERATED_BODY()

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

	void RequestCancel();
	bool WasCancelRequested() const;

protected:
	virtual void BeginExecution() {}

	UFUNCTION(BlueprintImplementableEvent, Category=Task, meta=(DisplayName="BeginExecution"))
	void ReceiveBeginExecution();

private:
	void CreateNotification();

	// Calls BeginExecution() and ReceiveBeginExecution()
	void StartExecutingTask();

private:
	UPROPERTY(Transient)
	UEditorUtilitySubsystem* MyTaskManager;

	UPROPERTY(Transient)
	bool bCancelRequested = false;

	bool Cached_GIsRunningUnattendedScript = false;

	TUniquePtr<FAsyncTaskNotification> TaskNotification;

	friend UEditorUtilitySubsystem;
};
