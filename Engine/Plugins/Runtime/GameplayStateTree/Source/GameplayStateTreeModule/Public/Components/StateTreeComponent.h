// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "StateTreeExecutionContext.h"
#include "BrainComponent.h"
#include "Tasks/AITask.h"
#include "StateTreeComponent.generated.h"

class UStateTree;

UCLASS(ClassGroup = AI, HideCategories = (Activation, Collision), meta = (BlueprintSpawnableComponent))
class GAMEPLAYSTATETREEMODULE_API UStateTreeComponent : public UBrainComponent, public IGameplayTaskOwnerInterface
{
	GENERATED_BODY()
public:
	UStateTreeComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// BEGIN UActorComponent overrides
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	// END UActorComponent overrides

	// BEGIN UBrainComponent overrides
	virtual void StartLogic() override;
	virtual void RestartLogic() override;
	virtual void StopLogic(const FString& Reason)  override;
	virtual void Cleanup() override;
	virtual void PauseLogic(const FString& Reason) override;
	virtual EAILogicResuming::Type ResumeLogic(const FString& Reason)  override;
	virtual bool IsRunning() const override;
	virtual bool IsPaused() const override;
	// END UBrainComponent overrides

	// BEGIN IGameplayTaskOwnerInterface
	virtual UGameplayTasksComponent* GetGameplayTasksComponent(const UGameplayTask& Task) const override;
	virtual AActor* GetGameplayTaskOwner(const UGameplayTask* Task) const override;
	virtual AActor* GetGameplayTaskAvatar(const UGameplayTask* Task) const override;
	virtual uint8 GetGameplayTaskDefaultPriority() const override;
	virtual void OnGameplayTaskInitialized(UGameplayTask& Task) override;
	// END IGameplayTaskOwnerInterface

#if WITH_GAMEPLAY_DEBUGGER
	virtual FString GetDebugInfoString() const override;
#endif // WITH_GAMEPLAY_DEBUGGER

protected:

	bool SetContextRequirements();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = AI, meta=(RequiredAssetDataTags="Schema=StateTreeComponentSchema"))
	UStateTree* StateTree;

	UPROPERTY(Transient)
	FStateTreeExecutionContext StateTreeContext;

	/** if set, state tree execution is allowed */
	uint8 bIsRunning : 1;

	/** if set, execution requests will be postponed */
	uint8 bIsPaused : 1;
};
