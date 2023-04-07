// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "EngineDefines.h"

#include "LearningAgentsRewards.generated.h"

namespace UE::Learning
{
	struct FRewardObject;
	struct FFloatReward;
	struct FScalarVelocityReward;
	struct FLocalDirectionalVelocityReward;
	struct FPlanarPositionDifferencePenalty;
	struct FPositionDifferencePenalty;
	struct FPositionArraySimilarityReward;
}

class ULearningAgentsTrainer;

// For functions in this file, we are favoring having more verbose names such as "AddFloatReward" vs simply "Add" in 
// order to keep it easy to find the correct function in blueprints.

/**
 * Base class for all rewards/penalties. Rewards are used during reinforcement learning to encourage/discourage
 * certain behaviors from occurring.
 */
UCLASS(Abstract, BlueprintType)
class LEARNINGAGENTSTRAINING_API ULearningAgentsReward : public UObject
{
	GENERATED_BODY()

public:

#if ENABLE_VISUAL_LOG
	/** Color used to draw this reward in the visual log */
	FLinearColor VisualLogColor = FColor::Green;

	/** Describes this reward to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const {}
#endif
};

/** A simple float reward. Used as a catch-all for situations where a more type-specific reward does not exist yet. */
UCLASS()
class LEARNINGAGENTSTRAINING_API UFloatReward : public ULearningAgentsReward
{
	GENERATED_BODY()

public:

	/**
	* Adds a new float reward to the given trainer. Call during ULearningAgentsTrainer::SetupRewards event.
	* @param AgentTrainer The trainer to add this reward to.
	* @param Name The name of this new reward. Used for debugging.
	* @param Weight Multiplier for this reward when being summed up for the total reward.
	* @return The newly created reward.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UFloatReward* AddFloatReward(
		ULearningAgentsTrainer* AgentTrainer,
		const FName Name = NAME_None,
		const float Weight = 1.0f);

	/**
	* Sets the data for this reward. Call during ULearningAgentsTrainer::SetRewards event.
	* @param AgentId The agent id this data corresponds to.
	* @param Reward The value currently being rewarded.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetFloatReward(const int32 AgentId, const float Reward);

#if ENABLE_VISUAL_LOG
	/** Describes this reward to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatReward> RewardObject;
};

/** A reward for maximizing speed. */
UCLASS()
class LEARNINGAGENTSTRAINING_API UScalarVelocityReward : public ULearningAgentsReward
{
	GENERATED_BODY()

public:

	/**
	* Adds a new scalar velocity reward to the given trainer. Call during ULearningAgentsTrainer::SetupRewards event.
	* @param AgentTrainer The trainer to add this reward to.
	* @param Name The name of this new reward. Used for debugging.
	* @param Weight Multiplier for this reward when being summed up for the total reward.
	* @param Scale Used to normalize the data for the reward.
	* @return The newly created reward.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UScalarVelocityReward* AddScalarVelocityReward(
		ULearningAgentsTrainer* AgentTrainer,
		const FName Name = NAME_None,
		const float Weight = 1.0f,
		const float Scale = 200.0f);

	/**
	* Sets the data for this reward. Call during ULearningAgentsTrainer::SetRewards event.
	* @param AgentId The agent id this data corresponds to.
	* @param Velocity The current scalar velocity.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetScalarVelocityReward(const int32 AgentId, const float Velocity);

#if ENABLE_VISUAL_LOG
	/** Describes this reward to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FScalarVelocityReward> RewardObject;
};

/** A reward for maximizing velocity along a given local axis. */
UCLASS()
class LEARNINGAGENTSTRAINING_API ULocalDirectionalVelocityReward : public ULearningAgentsReward
{
	GENERATED_BODY()

public:

	/**
	* Adds a new directional velocity reward to the given trainer. Call during ULearningAgentsTrainer::SetupRewards event.
	* @param AgentTrainer The trainer to add this reward to.
	* @param Name The name of this new reward. Used for debugging.
	* @param Weight Multiplier for this reward when being summed up for the total reward.
	* @param Scale Used to normalize the data for the reward.
	* @param Axis The local direction we want to maximize velocity in.
	* @return The newly created reward.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static ULocalDirectionalVelocityReward* AddLocalDirectionalVelocityReward(
		ULearningAgentsTrainer* AgentTrainer,
		const FName Name = NAME_None,
		const float Weight = 1.0f,
		const float Scale = 200.0f,
		const FVector Axis = FVector::ForwardVector);

	/**
	* Sets the data for this reward. Call during ULearningAgentsTrainer::SetRewards event.
	* @param AgentId The agent id this data corresponds to.
	* @param Velocity The current velocity.
	* @param RelativeRotation The frame of reference rotation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetLocalDirectionalVelocityReward(
		const int32 AgentId,
		const FVector Velocity,
		const FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	/** Describes this reward to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FLocalDirectionalVelocityReward> RewardObject;
};

/** A penalty for being far from a goal position in a plane. */
UCLASS()
class LEARNINGAGENTSTRAINING_API UPlanarPositionDifferencePenalty : public ULearningAgentsReward
{
	GENERATED_BODY()

public:

	/**
	* Adds a new planar difference penalty to the given trainer. The axis parameters define the plane.
	* Call during ULearningAgentsTrainer::SetupRewards event.
	* @param AgentTrainer The trainer to add this penalty to.
	* @param Name The name of this new penalty. Used for debugging.
	* @param Weight Multiplier for this penalty when being summed up for the total reward.
	* @param Scale Used to normalize the data for the penalty.
	* @param Threshold Minimal distance to apply this penalty.
	* @param Axis0 The forward axis of the plane.
	* @param Axis1 The right axis of the plane.
	* @return The newly created reward.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarPositionDifferencePenalty* AddPlanarPositionDifferencePenalty(
		ULearningAgentsTrainer* AgentTrainer, 
		const FName Name = NAME_None,
		const float Weight = 1.0f,
		const float Scale = 100.0f,
		const float Threshold = 0.0f,
		const FVector Axis0 = FVector::ForwardVector,
		const FVector Axis1 = FVector::RightVector);

	/**
	* Sets the data for this penalty. Call during ULearningAgentsTrainer::SetRewards event.
	* @param AgentId The agent id this data corresponds to.
	* @param Position0 The current position.
	* @param Position1 The goal position.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarPositionDifferencePenalty(const int32 AgentId, const FVector Position0, const FVector Position1);

#if ENABLE_VISUAL_LOG
	/** Describes this reward to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarPositionDifferencePenalty> RewardObject;
};

/** A reward for minimizing the distances of positions in the given arrays. */
UCLASS()
class LEARNINGAGENTSTRAINING_API UPositionArraySimilarityReward : public ULearningAgentsReward
{
	GENERATED_BODY()

public:

	/**
	* Adds a new position array similarity reward to the given trainer. Call during ULearningAgentsTrainer::SetupRewards event.
	* @param AgentTrainer The trainer to add this reward to.
	* @param Name The name of this new reward. Used for debugging.
	* @param PositionNum The number of positions in the array.
	* @param Scale Used to normalize the data for the reward.
	* @param Weight Multiplier for this reward when being summed up for the total reward.
	* @return The newly created reward.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPositionArraySimilarityReward* AddPositionArraySimilarityReward(
		ULearningAgentsTrainer* AgentTrainer,
		const FName Name = NAME_None,
		const int32 PositionNum = 0,
		const float Scale = 100.0f,
		const float Weight = 1.0f);

	/**
	* Sets the data for this reward. Call during ULearningAgentsTrainer::SetRewards event.
	* @param AgentId The agent id this data corresponds to.
	* @param Positions0 The current positions.
	* @param Positions1 The goal positions.
	* @param RelativePosition0 The vector Positions0 will be offset from.
	* @param RelativePosition1 The vector Positions1 will be offset from.
	* @param RelativeRotation0 The frame of reference rotation for Positions0.
	* @param RelativeRotation1 The frame of reference rotation for Positions1.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPositionArraySimilarityReward(
		const int32 AgentId,
		const TArray<FVector>& Positions0, 
		const TArray<FVector>& Positions1, 
		const FVector RelativePosition0 = FVector::ZeroVector,
		const FVector RelativePosition1 = FVector::ZeroVector,
		const FRotator RelativeRotation0 = FRotator::ZeroRotator,
		const FRotator RelativeRotation1 = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	/** Describes this reward to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPositionArraySimilarityReward> RewardObject;
};
