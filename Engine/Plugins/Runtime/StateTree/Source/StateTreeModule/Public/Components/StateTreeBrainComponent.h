// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "StateTreeExecutionContext.h"
#include "BrainComponent.h"
#include "Tasks/AITask.h"
#include "StateTreeBrainComponent.generated.h"

class UStateTree;


UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "Brain Component"))
class STATETREEMODULE_API UBrainComponentStateTreeSchema : public UStateTreeSchema
{
	GENERATED_BODY()

public:
	virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override;
	virtual bool IsClassAllowed(const UClass* InScriptStruct) const override;
	virtual bool IsExternalItemAllowed(const UStruct& InStruct) const override;
};


UCLASS(ClassGroup = AI, HideCategories = (Activation, Collision), meta = (BlueprintSpawnableComponent))
class STATETREEMODULE_API UStateTreeBrainComponent : public UBrainComponent, public IGameplayTaskOwnerInterface
{
	GENERATED_BODY()
public:
	UStateTreeBrainComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

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

private:

protected:

	bool SetContextRequirements();
	
	UPROPERTY(EditDefaultsOnly, Category = AI, meta=(RequiredAssetDataTags="Schema=BrainComponentStateTreeSchema"))
	UStateTree* StateTree;

	UPROPERTY()
	FStateTreeExecutionContext StateTreeContext;

	/** if set, state tree execution is allowed */
	uint8 bIsRunning : 1;

	/** if set, execution requests will be postponed */
	uint8 bIsPaused : 1;
};
