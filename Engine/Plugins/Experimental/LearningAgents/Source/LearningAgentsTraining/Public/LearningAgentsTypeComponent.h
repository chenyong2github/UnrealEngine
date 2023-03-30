// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "Containers/Array.h"
#include "LearningArray.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsTypeComponent.generated.h"

class ULearningAgentsType;

UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsTypeComponent : public USceneComponent
{
	GENERATED_BODY()

public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsTypeComponent();
	ULearningAgentsTypeComponent(FVTableHelper& Helper);
	virtual ~ULearningAgentsTypeComponent();

	virtual void OnRegister() override;

	const ULearningAgentsType& GetAgentType();

// ----- Blueprint Accessible Interface -----
public:

	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void OnAgentTypeSetupComplete();

	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void OnAgentAdded(int32 AgentId, UObject* Agent);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void AddAgent(int32 AgentId);

	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void OnAgentRemoved(int32 AgentId, UObject* Agent);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgent(int32 AgentId);

	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass"))
	UObject* GetAgent(int32 AgentId, TSubclassOf<UObject> AgentClass);

	const UObject* GetAgent(int32 AgentId) const;

// ----- Protected Blueprint Members -----
protected:

	/** The agent type this component is attached to. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	/** The valid agent ids that this component has selected. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	TArray<int32> SelectedAgentIds;

// ----- Protected Non-blueprint Members -----
protected:

	void UpdateAgentSet();

	UE::Learning::FIndexSet SelectedAgentsSet;
};
