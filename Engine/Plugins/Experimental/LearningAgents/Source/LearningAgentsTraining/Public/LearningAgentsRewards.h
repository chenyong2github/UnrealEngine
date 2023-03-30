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

/** Base class for all rewards */
UCLASS(Abstract, BlueprintType)
class LEARNINGAGENTSTRAINING_API ULearningAgentsReward : public UObject
{
	GENERATED_BODY()

public:

#if ENABLE_VISUAL_LOG
	/** Color used to draw this reward in the visual log */
	FLinearColor VisualLogColor = FColor::Green;

	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const {}
#endif
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTSTRAINING_API UFloatReward : public ULearningAgentsReward
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UFloatReward* AddFloatReward(ULearningAgentsTrainer* AgentTrainer, FName Name = NAME_None, float Weight = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetFloatReward(int32 AgentId, float Reward);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatReward> RewardObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTSTRAINING_API UScalarVelocityReward : public ULearningAgentsReward
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UScalarVelocityReward* AddScalarVelocityReward(ULearningAgentsTrainer* AgentTrainer, FName Name = NAME_None, float Weight = 1.0f, float Scale = 200.0f);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetScalarVelocityReward(int32 AgentId, float Velocity);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FScalarVelocityReward> RewardObject;
};

UCLASS()
class LEARNINGAGENTSTRAINING_API ULocalDirectionalVelocityReward : public ULearningAgentsReward
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static ULocalDirectionalVelocityReward* AddLocalDirectionalVelocityReward(ULearningAgentsTrainer* AgentTrainer, FName Name = NAME_None, float Weight = 1.0f, float Scale = 200.0f, FVector Axis = FVector::ForwardVector);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetLocalDirectionalVelocityReward(int32 AgentId, FVector Velocity, FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FLocalDirectionalVelocityReward> RewardObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTSTRAINING_API UPlanarPositionDifferencePenalty : public ULearningAgentsReward
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarPositionDifferencePenalty* AddPlanarPositionDifferencePenalty(
		ULearningAgentsTrainer* AgentTrainer, 
		FName Name = NAME_None,
		float Weight = 1.0f, 
		float Scale = 100.0f, 
		float Threshold = 0.0f, 
		FVector Axis0 = FVector::ForwardVector, 
		FVector Axis1 = FVector::RightVector);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarPositionDifferencePenalty(int32 AgentId, FVector Position0, FVector Position1);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarPositionDifferencePenalty> RewardObject;
};

//------------------------------------------------------------------

UCLASS()
class LEARNINGAGENTSTRAINING_API UPositionArraySimilarityReward : public ULearningAgentsReward
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPositionArraySimilarityReward* AddPositionArraySimilarityReward(ULearningAgentsTrainer* AgentTrainer, FName Name = NAME_None, int32 PositionNum = 0, float Scale = 100.0f, float Weight = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPositionArraySimilarityReward(
		int32 AgentId, 
		const TArray<FVector>& Positions0, 
		const TArray<FVector>& Positions1, 
		FVector RelativePosition0 = FVector::ZeroVector,
		FVector RelativePosition1 = FVector::ZeroVector,
		FRotator RelativeRotation0 = FRotator::ZeroRotator,
		FRotator RelativeRotation1 = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPositionArraySimilarityReward> RewardObject;
};

//------------------------------------------------------------------
