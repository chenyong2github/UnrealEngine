// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "EngineDefines.h"

#include "LearningAgentsActions.generated.h"

namespace UE::Learning
{
	struct FFeatureObject;
	struct FFloatFeature;
	struct FRotationVectorFeature;
}

class ULearningAgentsType;

// For functions in this file, we are favoring having more verbose names such as "AddFloatAction" vs simply "Add" in 
// order to keep it easy to find the correct function in blueprints.

/** The base class for all actions. Actions define the outputs from your agents. */
UCLASS(Abstract, BlueprintType)
class LEARNINGAGENTS_API ULearningAgentsAction : public UObject
{
	GENERATED_BODY()

public:

#if ENABLE_VISUAL_LOG
	/** Color used to draw this action in the visual log */
	FLinearColor VisualLogColor = FColor::Blue;

	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const {}
#endif
};

/** A simple float action. Used as a catch-all for situations where a more type-specific action does not exist yet. */
UCLASS()
class LEARNINGAGENTS_API UFloatAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:

	/**
	* Adds a new float action to the given agent type. Call during ULearningAgentsType::SetupActions event.
	* @param AgentType The agent type to add this action to.
	* @param Name The name of this new action. Used for debugging.
	* @param Scale Used to normalize the data for the action.
	* @return The newly created action.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UFloatAction* AddFloatAction(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 1.0f);

	/**
	* Gets the data for this action. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @return The current action value.
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	float GetFloatAction(const int32 AgentId);

	/**
	* Sets the data for this action. Call during ULearningAgentsController::EncodeActions event.
	* @param AgentId The agent id to set data for.
	* @param Value The current action value.
	*/	
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetFloatAction(const int32 AgentId, const float Value);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

/** A simple FVector action. */
UCLASS()
class LEARNINGAGENTS_API UVectorAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:

	/**
	* Adds a new vector action to the given agent type. Call during ULearningAgentsType::SetupActions event.
	* @param AgentType The agent type to add this action to.
	* @param Name The name of this new action. Used for debugging.
	* @param Scale Used to normalize the data for the action.
	* @return The newly created action.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UVectorAction* AddVectorAction(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 1.0f);

	/**
	* Gets the data for this action. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @return The current action values.
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	FVector GetVectorAction(const int32 AgentId);

	/**
	* Sets the data for this action. Call during ULearningAgentsController::EncodeActions event.
	* @param AgentId The agent id to set data for.
	* @param Value The current action values.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetVectorAction(const int32 AgentId, const FVector Action);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

/** An array of rotation vector actions. */
UCLASS()
class LEARNINGAGENTS_API URotationVectorArrayAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:

	/**
	* Adds a new rotation vector array action to the given agent type. Call during ULearningAgentsType::SetupActions event.
	* @param AgentType The agent type to add this action to.
	* @param Name The name of this new action. Used for debugging.
	* @param RotationVectorNum The number of rotations in the array.
	* @param Scale Used to normalize the data for the action.
	* @return The newly created action.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static URotationVectorArrayAction* AddRotationVectorArrayAction(ULearningAgentsType* AgentType, const FName Name = NAME_None, const int32 RotationVectorNum = 0, const float Scale = 180.0f);

	/**
	* Gets the data for this action as rotation vectors. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @param OutRotationVectors The current action values.
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	void GetRotationVectorArrayAction(const int32 AgentId, TArray<FVector>& OutRotationVectors);

	/**
	* Gets the data for this action as quaternions. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @param OutRotations The current action values.
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	void GetRotationVectorArrayActionAsQuats(const int32 AgentId, TArray<FQuat>& OutRotations);

	/**
	* Gets the data for this action as rotators. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @param OutRotations The current action values.
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	void GetRotationVectorArrayActionAsRotators(const int32 AgentId, TArray<FRotator>& OutRotations);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FRotationVectorFeature> FeatureObject;
};
