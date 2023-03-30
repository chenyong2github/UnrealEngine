// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "LearningAgentsTypeComponent.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"
#include "UObject/NameTypes.h"

#include "LearningAgentsTrainer.generated.h"

namespace UE::Learning
{
	struct FAnyCompletion;
	struct FArrayMap;
	struct FEpisodeBuffer;
	struct FNeuralNetwork;
	struct FNeuralNetworkPolicyFunction;
	struct FReplayBuffer;
	struct FResetInstanceBuffer;
	struct FRewardObject;
	struct FSharedMemoryPPOTrainer;
	struct FSumReward;
	struct FCompletionObject;
}

class ULearningAgentsCompletion;
class ULearningAgentsReward;
class ULearningAgentsType;

UENUM(BlueprintType, Category = "LearningAgents")
enum class ELearningAgentsCompletion : uint8
{
	/** Episode ended early but was still in progress. Value function will be used to estimate final return. */
	Truncation	UMETA(DisplayName = "Truncation"),

	/** Episode ended early and zero reward was expected for all future steps. */
	Termination	UMETA(DisplayName = "Termination"),
};

USTRUCT(BlueprintType, Category = "LearningAgents")
struct FLearningAgentsTrainerSettings
{
	GENERATED_BODY()

public:

	/** Completion type to use when the maximum number of steps for an episode is reached. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsCompletion MaxStepsCompletion = ELearningAgentsCompletion::Truncation;

	/** Max number of steps to take while training before episode automatically terminates */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxStepNum = 300;

	/** Maximum number of episodes to record before running a training iteration */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumRecordedEpisodesPerIteration = 1000;

	/** Maximum number of steps to record before running a training iteration */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumRecordedStepsPerIteration = 10000;
};

USTRUCT(BlueprintType, Category="LearningAgents")
struct FLearningAgentsTrainerGameSettings
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bUseFixedTimeStep = true;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (DisplayName = "Fixed Time Step Frequency (Hz)"), meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FixedTimeStepFrequency = 60.0f;

	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bSetMaxPhysicsStepToFixedTimeStep = true;

	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bDisableVSync = true;

	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bUseUnlitViewportRendering = false;
};

UENUM(BlueprintType, Category = "LearningAgents")
enum class ELearningAgentsTrainerDevice : uint8
{
	CPU,
	GPU,
};

USTRUCT(BlueprintType, Category = "LearningAgents")
struct FLearningAgentsTrainerTrainingSettings
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 NumberOfIterations = 1000000;

	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bReinitializeNetwork = true;

	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bUseTensorboard = false;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float InitialActionScale = 0.1f;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DiscountFactor = 0.99f;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 RandomSeed = 1234;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 NumberOfStepsToTrimAtStartOfEpisode = 0;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 NumberOfStepsToTrimAtEndOfEpisode = 0;

	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsTrainerDevice Device = ELearningAgentsTrainerDevice::GPU;

	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TrainerCommunicationTimeout = 20.0f;
};


UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsTrainer : public ULearningAgentsTypeComponent
{
	GENERATED_BODY()

public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsTrainer();
	ULearningAgentsTrainer(FVTableHelper& Helper);
	virtual ~ULearningAgentsTrainer();

	virtual void OnAgentAdded_Implementation(int32 AgentId, UObject* Agent) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

// ----- Setup -----
public:

	/**
	* Initializes this object and runs the setup functions for observations and actions.
	* If the AgentType has not yet been setup, this call will fail, so consider using the
	* OnAgentTypeSetupComplete event to ensure the timing is correct.
	* 
	* @param AgentTypeOverride This agent type will override any parent agent type we found. Must be provided if this trainer component is not attached to a parent agent type component.
	* @param Settings The settings used to setup this trainer
	* @see OnAgentTypeSetupComplete
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupTrainer(
		ULearningAgentsType* AgentTypeOverride,
		const FLearningAgentsTrainerSettings& Settings = FLearningAgentsTrainerSettings());

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	const bool IsSetupPerformed() const;

//** ----- Rewards ----- */
public:
	
	/**
	 * Event where all rewards should be set up for this agent trainer.
	 * 
	 * @param AgentTrainer This AgentTrainer. This is a convenience for blueprints so you don't have to find the Self pin.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetupRewards(ULearningAgentsTrainer* AgentTrainer);

	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetRewards(const TArray<int32>& AgentIds);

	void AddReward(TObjectPtr<ULearningAgentsReward> Object, const TSharedRef<UE::Learning::FRewardObject>& Reward);

//** ----- Completions ----- */
public:

	/**
	 * Event where all completions should be set up for this agent trainer.
	 * 
	 * @param AgentTrainer This AgentTrainer. This is a convenience for blueprints so you don't have to find the Self pin.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetupCompletions(ULearningAgentsTrainer* AgentTrainer);

	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetCompletions(const TArray<int32>& AgentIds);

	void AddCompletion(TObjectPtr<ULearningAgentsCompletion> Object, const TSharedRef<UE::Learning::FCompletionObject>& Completion);

//** ----- Resets ----- */
public:

	/**
	 * Event where both the agent and its training environment should be reset so that a new training episode could
	 * commence.
	 *
	 * @param AgentId The id of the current agent to be reset
	 * @param Agent A pointer to the current agent to be reset
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void ResetInstance(const TArray<int32>& AgentIds);

//** ----- Training Process ----- */
public:

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	const bool IsTraining() const;

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void StartTraining(
		const FLearningAgentsTrainerTrainingSettings& TrainingSettings = FLearningAgentsTrainerTrainingSettings(),
		const FLearningAgentsTrainerGameSettings& GameSettings = FLearningAgentsTrainerGameSettings());

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void StopTraining();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EvaluateRewards();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EvaluateCompletions();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void IterateTraining();

	/**
	* Manually reset all agents. Does not record the experience gathered up to this point by each agent.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void ResetAllInstances();

//** ----- Private ----- */
private:

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bSetupPerformed = false;

	/** True if training is currently in-progress. Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bIsTraining = false;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsReward>> RewardObjects;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsCompletion>> CompletionObjects;

	TArray<TSharedRef<UE::Learning::FRewardObject>, TInlineAllocator<16>> RewardFeatures;
	TSharedPtr<UE::Learning::FSumReward> Rewards;

	TArray<TSharedRef<UE::Learning::FCompletionObject>, TInlineAllocator<16>> CompletionFeatures;
	TSharedPtr<UE::Learning::FAnyCompletion> Completions;

	TUniquePtr<UE::Learning::FEpisodeBuffer> EpisodeBuffer;
	TUniquePtr<UE::Learning::FReplayBuffer> ReplayBuffer;
	TUniquePtr<UE::Learning::FResetInstanceBuffer> ResetBuffer;
	TUniquePtr<UE::Learning::FSharedMemoryPPOTrainer> Trainer;

	/** One-time Error Logging State */
	bool bPreviouslyLogged = false;

	ELearningAgentsCompletion MaxStepsCompletion = ELearningAgentsCompletion::Truncation;

	float TrainerTimeout = 10.0f;

	void DoneTraining();

//** ----- Private Recording of GameSettings ----- */
private:

	bool bFixedTimestepUsed = false;
	float FixedTimeStepDeltaTime = -1.0f;
	bool bVSyncEnabled = true;
	float MaxPhysicsStep = -1.0f;
	int32 ViewModeIndex = -1;
};
