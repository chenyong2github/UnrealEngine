// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsNeuralNetwork.h" // Included for ELearningAgentsActivationFunction
#include "LearningArray.h"

#include "Components/ActorComponent.h"
#include "UObject/ObjectPtr.h"
#include "EngineDefines.h"

#include "LearningAgentsPolicy.generated.h"

namespace UE::Learning
{
	struct FNeuralNetwork;
	struct FNeuralNetworkPolicyFunction;
}

struct FDirectoryPath;
class ULearningAgentsNeuralNetwork;

//------------------------------------------------------------------

USTRUCT(BlueprintType, Category = "LearningAgents")
struct LEARNINGAGENTS_API FLearningAgentsPolicySettings
{
	GENERATED_BODY()

public:

	/** Seed for the action noise used by the policy  */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 ActionNoiseSeed = 1234;

	/** Minimum action noise used by the policy */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ActionNoiseMin = 0.25f;

	/** Maximum action noise used by the policy */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ActionNoiseMax = 0.25f;

	/** Total layers for policy network including input, hidden, and output layers */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "2", UIMin = "2"))
	int32 LayerNum = 3;

	/** Number of neurons in each hidden layer of the policy network */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 HiddenLayerSize = 128;

	/** Activation function to use on hidden layers of the policy network */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsActivationFunction ActivationFunction = ELearningAgentsActivationFunction::ELU;
};

//------------------------------------------------------------------

UCLASS(BlueprintType, Blueprintable)
class LEARNINGAGENTS_API ULearningAgentsPolicy : public UActorComponent
{
	GENERATED_BODY()

// ----- Setup -----
public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsPolicy();
	ULearningAgentsPolicy(FVTableHelper& Helper);
	virtual ~ULearningAgentsPolicy();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupPolicy(ULearningAgentsType* InAgentType, const FLearningAgentsPolicySettings& PolicySettings = FLearningAgentsPolicySettings());

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsPolicySetupPerformed() const;

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
	void LoadPolicyFromSnapshot(const FDirectoryPath& Directory, const FString Filename);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SavePolicyToSnapshot(const FDirectoryPath& Directory, const FString Filename) const;

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void LoadPolicyFromAsset(const ULearningAgentsNeuralNetwork* NeuralNetworkAsset);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents", Meta = (DevelopmentOnly))
	void SavePolicyToAsset(ULearningAgentsNeuralNetwork* NeuralNetworkAsset) const;

// ----- Evaluation -----
public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EvaluatePolicy();

// ----- Non-blueprint public interface -----
public:

	UE::Learning::FNeuralNetwork& GetPolicyNetwork();
	UE::Learning::FNeuralNetworkPolicyFunction& GetPolicyObject();

// ----- Private Data -----
private:

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<int32> SelectedAgentIds;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bPolicySetupPerformed = false;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsNeuralNetwork> Network;

private:

	TSharedPtr<UE::Learning::FNeuralNetworkPolicyFunction> PolicyObject;

	UE::Learning::FIndexSet SelectedAgentsSet;

private:

#if ENABLE_VISUAL_LOG
	/** Color used to draw this action in the visual log */
	FLinearColor VisualLogColor = FColor::Purple;

	void VisualLog(const UE::Learning::FIndexSet Instances) const;
#endif
};
