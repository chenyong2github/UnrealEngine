// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "Components/ActorComponent.h"

#include "LearningAgentsController.generated.h"

UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsController : public UActorComponent
{
	GENERATED_BODY()

// ----- Setup -----
public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsController();
	ULearningAgentsController(FVTableHelper& Helper);
	virtual ~ULearningAgentsController();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupController(ULearningAgentsType* InAgentType);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsControllerSetupPerformed() const;

// ----- Agent Management -----
public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void AddAgent(int32 AgentId);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgent(int32 AgentId);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgent(int32 AgentId) const;

	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass"))
	ULearningAgentsType* GetAgentType(TSubclassOf<ULearningAgentsType> AgentClass);

// ----- Actions -----
public:

	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetActions(const TArray<int32>& AgentIds);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EncodeActions();

// ----- Private Data -----
private:

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<int32> SelectedAgentIds;

private:

	UE::Learning::FIndexSet SelectedAgentsSet;
};
