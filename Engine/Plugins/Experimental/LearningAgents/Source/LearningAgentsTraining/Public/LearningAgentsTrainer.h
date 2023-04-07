// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "LearningAgentsCritic.h" // Included for FLearningAgentsCriticSettings()

#include "Templates/SharedPointer.h"
#include "UObject/ObjectPtr.h"
#include "Engine/EngineTypes.h"

#include "LearningAgentsTrainer.generated.h"

namespace UE::Learning
{
	struct FAnyCompletion;
	struct FArrayMap;
	struct FEpisodeBuffer;
	struct FReplayBuffer;
	struct FResetInstanceBuffer;
	struct FRewardObject;
	struct FSharedMemoryPPOTrainer;
	struct FSumReward;
	struct FCompletionObject;
}

class ULearningAgentsType;
class ULearningAgentsCompletion;
class ULearningAgentsReward;
class ULearningAgentsPolicy;
class ULearningAgentsCritic;

/** Completion modes for episodes. */
UENUM(BlueprintType, Category = "LearningAgents")
enum class ELearningAgentsCompletion : uint8
{
	/** Episode ended early but was still in progress. Critic will be used to estimate final return. */
	Truncation	UMETA(DisplayName = "Truncation"),

	/** Episode ended early and zero reward was expected for all future steps. */
	Termination	UMETA(DisplayName = "Termination"),
};

/** The configurable settings for a ULearningAgentsTrainer. */
USTRUCT(BlueprintType, Category = "LearningAgents")
struct FLearningAgentsTrainerSettings
{
	GENERATED_BODY()

public:

	/** Completion type to use when the maximum number of steps for an episode is reached. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsCompletion MaxStepsCompletion = ELearningAgentsCompletion::Truncation;

	/** Max number of steps to take while training before episode automatically completes. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxStepNum = 300;

	/** Maximum number of episodes to record before running a training iteration. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumRecordedEpisodesPerIteration = 1000;

	/** Maximum number of steps to record before running a training iteration. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumRecordedStepsPerIteration = 10000;

	/** Time in seconds to wait for the training subprocess before timing out. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TrainerCommunicationTimeout = 20.0f;
};

/**
* The configurable game settings for a ULearningAgentsTrainer. These allow the timestep and physics tick to be fixed
* during training, which can enable ticking faster than real-time.
*/
USTRUCT(BlueprintType, Category="LearningAgents")
struct FLearningAgentsTrainerGameSettings
{
	GENERATED_BODY()

public:

	/**
	* If true, the game will run in fixed time step mode (i.e the frame's delta times will always be the same
	* regardless of how much wall time has passed). This can enable faster than real-time training if your game runs
	* quickly. If false, the time steps will match real wall time.
	*/
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bUseFixedTimeStep = true;

	/**
	* Determines the amount of time for each frame when bUseFixedTimeStep is true; Ignored if false. You want this
	* time step to match as closely as possible to the expected inference time steps, otherwise your training results
	* may not generalize to your game.
	*/
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (DisplayName = "Fixed Time Step Frequency (Hz)"), meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FixedTimeStepFrequency = 60.0f;

	/** If true, set the physics delta time to match the fixed time step. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bSetMaxPhysicsStepToFixedTimeStep = true;

	/** If true, VSync will be disabled; Otherwise, it will not. Disabling VSync can speed up the game simulation. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bDisableVSync = true;

	/** If true, the viewport rendering will be unlit; Otherwise, it will not. Disabling lighting can speed up the game simulation. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bUseUnlitViewportRendering = false;
};

/** Enumeration of the training devices. */
UENUM(BlueprintType, Category = "LearningAgents")
enum class ELearningAgentsTrainerDevice : uint8
{
	CPU,
	GPU,
};

/** The configurable settings for the training process. */
USTRUCT(BlueprintType, Category = "LearningAgents")
struct FLearningAgentsTrainerTrainingSettings
{
	GENERATED_BODY()

public:

	/** The number of iterations to run before training is complete. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 NumberOfIterations = 1000000;

	/** If true, TensorBoard logs will be emitted to Intermediate\TensorBoard. Otherwise, no logs will be emitted. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bUseTensorboard = false;

	/**
	* The initial scaling for the weights of the output layer of the neural network. Typically, you would use this to
	* scale down the initial weights as it can stabilize the initial training and speed up convergence.
	*/
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float InitialActionScale = 0.1f;

	/**
	* The discount factor to use during training. This affects how much the agent cares about future rewards vs
	* near-term rewards. Should typically be a value less than but near 1. 
	*/
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DiscountFactor = 0.99f;

	/** The seed used for any random sampling the trainer will perform, e.g. for weight initialization. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 RandomSeed = 1234;

	/** 
	* The number of steps to trim from the start of the episode, e.g. can be useful if some things are still getting
	* setup at the start of the episode.
	*/
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 NumberOfStepsToTrimAtStartOfEpisode = 0;

	/**
	* The number of steps to trim from the end of the episode. Can be useful if the end of the episode contains
	* irrelevant data.
	*/
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0"))
	int32 NumberOfStepsToTrimAtEndOfEpisode = 0;

	/** The device to train on. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	ELearningAgentsTrainerDevice Device = ELearningAgentsTrainerDevice::GPU;
};

/**
* The ULearningAgentsTrainer is the core class for reinforcement learning training. It has a few responsibilities:
*   1) It keeps track of which agents are gathering training data.
*   2) It defines how those agents' rewards, completions, and resets are implemented.
*   3) It provides methods for orchestrating the training process.
*
* To use this class, you need to implement the SetupRewards and SetupCompletions functions (as well as their
* corresponding SetRewards and SetCompletions functions), which will define the rewards and penalties the agent
* receives and what conditions cause an episode to end. Before you can begin training, you need to call SetupTrainer,
* which will initialize the underlying data structures, and you need to call AddAgent for each agent you want to gather
* training data from.
*
* @see ULearningAgentsType to understand how observations and actions work.
*/
UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsTrainer : public UActorComponent
{
	GENERATED_BODY()

// ----- Setup -----
public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsTrainer();
	ULearningAgentsTrainer(FVTableHelper& Helper);
	virtual ~ULearningAgentsTrainer();

	/** Will automatically call EndTraining if training is still in-progress when play is ending. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	* Initializes this object and runs the setup functions for rewards and completions.
	* @param InAgentType The agent type we are training with.
	* @param InPolicy The policy to be trained.
	* @param InCritic Optional - only needs to be provided if we want the critic to be accessible at runtime.
	* @param Settings The trainer settings to use.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupTrainer(
		ULearningAgentsType* InAgentType,
		ULearningAgentsPolicy* InPolicy,
		ULearningAgentsCritic* InCritic = nullptr,
		const FLearningAgentsTrainerSettings& Settings = FLearningAgentsTrainerSettings());

	/** Returns true if SetupTrainer has been run successfully; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsTrainerSetupPerformed() const;

// ----- Agent Management -----
public:

	/**
	 * Adds an agent to this trainer.
	 * @param AgentId The id of the agent to be added.
	 * @warning The agent id must exist for the agent type.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void AddAgent(const int32 AgentId);

	/**
	* Removes an agent from this trainer.
	* @param AgentId The id of the agent to be removed.
	* @warning The agent id must exist for this trainer already.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgent(const int32 AgentId);

	/** Returns true if the given id has been previously added to this trainer; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgent(const int32 AgentId) const;

	/**
	* Gets the agent type this trainer is associated with.
	* @param AgentClass The class to cast the agent type to (in blueprint).
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass"))
	ULearningAgentsType* GetAgentType(const TSubclassOf<ULearningAgentsType> AgentClass);

public:

	/** Gets the agent corresponding to the given id. */
	const UObject* GetAgent(const int32 AgentId) const;

	/** Gets the agent corresponding to the given id. */
	UObject* GetAgent(const int32 AgentId);

	/** Gets the associated agent type. */
	const ULearningAgentsType* GetAgentType() const;

	/** Gets the associated agent type. */
	ULearningAgentsType* GetAgentType();

// ----- Rewards -----
public:
	
	/**
	 * During this event, all rewards/penalties should be added to this trainer.
	 * @param AgentTrainer This trainer. This is a convenience for blueprints so you don't have to find the Self pin.
	 * @see LearningAgentsRewards.h for the list of available rewards/penalties.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetupRewards(ULearningAgentsTrainer* AgentTrainer);

	/**
	* During this event, all rewards/penalties should be set for each agent.
	* @param AgentIds The list of agent ids to set rewards/penalties for.
	* @see LearningAgentsRewards.h for the list of available rewards/penalties.
	* @see GetAgent to get the agent corresponding to each id.
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetRewards(const TArray<int32>& AgentIds);

	/**
	* Used by objects derived from ULearningAgentsReward to add themselves to this trainer during their creation.
	* You shouldn't need to call this directly.
	*/
	void AddReward(TObjectPtr<ULearningAgentsReward> Object, const TSharedRef<UE::Learning::FRewardObject>& Reward);

// ----- Completions ----- 
public:

	/**
	 * During this event, all completions should be added to this trainer.
	 * @param AgentTrainer This trainer. This is a convenience for blueprints so you don't have to find the Self pin.
	 * @see LearningAgentsCompletions.h for the list of available completions.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetupCompletions(ULearningAgentsTrainer* AgentTrainer);

	/**
	* During this event, all completions should be set for each agent.
	* @param AgentIds The list of agent ids to set completions for.
	* @see LearningAgentsCompletions.h for the list of available completions.
	* @see GetAgent to get the agent corresponding to each id.
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void SetCompletions(const TArray<int32>& AgentIds);

	/**
	* Used by objects derived from ULearningAgentsCompletion to add themselves to this trainer during their creation.
	* You shouldn't need to call this directly.
	*/
	void AddCompletion(TObjectPtr<ULearningAgentsCompletion> Object, const TSharedRef<UE::Learning::FCompletionObject>& Completion);

// ----- Resets ----- 
public:

	/**
	 * During this event, you will receive the ids of each agent that needs to be reset. Both the agent's actor and its
	 * training environment should be reset for a new episode to commence.
	 * @param AgentIds The ids of the agents that need resetting.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "LearningAgents")
	void ResetInstance(const TArray<int32>& AgentIds);

// ----- Training Process -----
public:

	/** Returns true if the trainer is currently training; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	const bool IsTraining() const;

	/**
	* Begins the training process with the provided settings.
	* @param TrainingSettings The settings for this training run.
	* @param GameSettings The settings that will affect the game's simulation.
	* @param CriticSettings The settings for the critic (if we are using one).
	* @param bReinitializePolicyNetwork If true, reinitialize the policy. Set this to false if your policy is pre-trained, e.g. with imitation learning.
	* @param bReinitializeCriticNetwork If true, reinitialize the critic. Set this to false if your critic is pre-trained.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void BeginTraining(
		const FLearningAgentsTrainerTrainingSettings& TrainingSettings = FLearningAgentsTrainerTrainingSettings(),
		const FLearningAgentsTrainerGameSettings& GameSettings = FLearningAgentsTrainerGameSettings(),
		const FLearningAgentsCriticSettings& CriticSettings = FLearningAgentsCriticSettings(),
		const bool bReinitializePolicyNetwork = true,
		const bool bReinitializeCriticNetwork = true);

	/** Stops the training process. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EndTraining();

	/**
	* Call this function when it is time to evaluate the rewards for your agents. This should be done at the beginning
	* of each iteration of your training loop after the initial step, i.e. after taking an action, you want to get into
	* the next state before evaluating the rewards.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EvaluateRewards();

	/**
	* Call this function when it is time to evaluate the completions for your agents. This should be done at the beginning
	* of each iteration of your training loop after the initial step, i.e. after taking an action, you want to get into
	* the next state before evaluating the completions.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EvaluateCompletions();

	/**
	* Call this function at the end of each step of your training loop. This takes the current observations/actions/
	* rewards and moves them into the current episode's experience buffer. Finished episodes will have their agents
	* reset and their data will be sent to the external training process. Finally, the latest iteration of the
	* trained policy will be synced back to UE so further experience can be acquired on-policy.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void IterateTraining();

	/** Manually reset all agents. Does not record the experience gathered up to this point by each agent. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void ResetAllInstances();

// ----- Private Data ----- 
private:

	/** The agent type this trainer is associated with. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	/** The agent ids this trainer is managing. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<int32> SelectedAgentIds;

	/** The current policy for experience gathering. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsPolicy> Policy;

	/** The current critic. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsCritic> Critic;

	/** True if this trainer's SetupTrainer has been run; Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bTrainerSetupPerformed = false;

	/** True if training is currently in-progress. Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bIsTraining = false;

	/** The list of current reward objects. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsReward>> RewardObjects;

	/** The list of current completion objects. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsCompletion>> CompletionObjects;

	TArray<TSharedRef<UE::Learning::FRewardObject>, TInlineAllocator<16>> RewardFeatures;
	TArray<TSharedRef<UE::Learning::FCompletionObject>, TInlineAllocator<16>> CompletionFeatures;

	TSharedPtr<UE::Learning::FSumReward> Rewards;
	TSharedPtr<UE::Learning::FAnyCompletion> Completions;

	TUniquePtr<UE::Learning::FEpisodeBuffer> EpisodeBuffer;
	TUniquePtr<UE::Learning::FReplayBuffer> ReplayBuffer;
	TUniquePtr<UE::Learning::FResetInstanceBuffer> ResetBuffer;
	TUniquePtr<UE::Learning::FSharedMemoryPPOTrainer> Trainer;

	ELearningAgentsCompletion MaxStepsCompletion = ELearningAgentsCompletion::Truncation;

	float TrainerTimeout = 10.0f;

	void DoneTraining();

	UE::Learning::FIndexSet SelectedAgentsSet;

// ----- Private Recording of GameSettings ----- 
private:

	bool bFixedTimestepUsed = false;
	float FixedTimeStepDeltaTime = -1.0f;
	bool bVSyncEnabled = true;
	float MaxPhysicsStep = -1.0f;
	int32 ViewModeIndex = -1;
};
