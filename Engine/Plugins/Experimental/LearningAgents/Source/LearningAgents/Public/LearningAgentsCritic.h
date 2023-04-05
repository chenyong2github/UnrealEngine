// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsNeuralNetwork.h" // Included for ELearningAgentsActivationFunction
#include "LearningArray.h"

#include "Components/ActorComponent.h"
#include "UObject/ObjectPtr.h"
#include "EngineDefines.h"

#include "LearningAgentsCritic.generated.h"

namespace UE::Learning
{
	struct FNeuralNetwork;
	struct FNeuralNetworkCriticFunction;
}

struct FDirectoryPath;
class ULearningAgentsNeuralNetwork;

//------------------------------------------------------------------

USTRUCT(BlueprintType, Category = "LearningAgents")
struct LEARNINGAGENTS_API FLearningAgentsCriticSettings
{
	GENERATED_BODY()

public:

	/** Total layers for critic network including input, hidden, and output layers */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "2", UIMin = "2"))
	int32 LayerNum = 3;

	/** Number of neurons in each hidden layer of the critic network */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 HiddenLayerSize = 128;

	/** Activation function to use on hidden layers of the critic network */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsActivationFunction ActivationFunction = ELearningAgentsActivationFunction::ELU;
};

//------------------------------------------------------------------

UCLASS(BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsCritic : public UActorComponent
{
	GENERATED_BODY()

// ----- Setup -----
public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsCritic();
	ULearningAgentsCritic(FVTableHelper& Helper);
	virtual ~ULearningAgentsCritic();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupCritic(ULearningAgentsType* InAgentType, const FLearningAgentsCriticSettings& CriticSettings = FLearningAgentsCriticSettings());

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsCriticSetupPerformed() const;

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

// ----- Load / Save -----
public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void LoadCriticFromSnapshot(const FDirectoryPath& Directory, const FString Filename);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SaveCriticToSnapshot(const FDirectoryPath& Directory, const FString Filename) const;

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void LoadCriticFromAsset(const ULearningAgentsNeuralNetwork* NeuralNetworkAsset);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents", Meta=(DevelopmentOnly))
	void SaveCriticToAsset(ULearningAgentsNeuralNetwork* NeuralNetworkAsset) const;

// ----- Evaluation -----
public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EvaluateCritic();

// ----- Non-blueprint public interface -----
public:

	UE::Learning::FNeuralNetwork& GetCriticNetwork();
	UE::Learning::FNeuralNetworkCriticFunction& GetCriticObject();

// ----- Private Data -----
private:

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<int32> SelectedAgentIds;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bCriticSetupPerformed = false;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsNeuralNetwork> Network;

private:

	TSharedPtr<UE::Learning::FNeuralNetworkCriticFunction> CriticObject;

	UE::Learning::FIndexSet SelectedAgentsSet;

private:

#if ENABLE_VISUAL_LOG
	/** Color used to draw this action in the visual log */
	FLinearColor VisualLogColor = FColor::Orange;

	void VisualLog(const UE::Learning::FIndexSet Instances) const;
#endif
};
