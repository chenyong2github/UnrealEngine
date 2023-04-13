// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"
#include "LearningAgentsDebug.h"

#include "Templates/SharedPointer.h"
#include "UObject/Object.h"

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

//------------------------------------------------------------------

/**
* The base class for all actions. Actions define the outputs from your agents. Action getters are marked non-pure by
* convention as many of them do non-trivial amounts of work that can cause performance issues when marked pure in 
* blueprints.
*/
UCLASS(Abstract, BlueprintType)
class LEARNINGAGENTS_API ULearningAgentsAction : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	/** Color used to draw this action in the visual log */
	FLinearColor VisualLogColor = FColor::Blue;

	/** Describes this action to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const {}
#endif
};

//------------------------------------------------------------------

/** A simple float action. Used as a catch-all for situations where a more type-specific action does not exist yet. */
UCLASS()
class LEARNINGAGENTS_API UFloatAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:

	/**
	* Adds a new float action to the given agent type. Call during ULearningAgentsType::SetupActions event.
	* @param InAgentType The agent type to add this action to.
	* @param Name The name of this new action. Used for debugging.
	* @param Scale Used to normalize the data for the action.
	* @return The newly created action.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UFloatAction* AddFloatAction(ULearningAgentsType* InAgentType, const FName Name = NAME_None, const float Scale = 1.0f);

	/**
	* Gets the data for this action. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @return The current action value.
	*/
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	float GetFloatAction(const int32 AgentId) const;

	/**
	* Sets the data for this action. Call during ULearningAgentsController::EncodeActions event.
	* @param AgentId The agent id to set data for.
	* @param Value The current action value.
	*/	
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AgentId = "-1"))
	void SetFloatAction(const int32 AgentId, const float Value);

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	/** Describes this action to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

/** A simple float array action. Used as a catch-all for situations where a more type-specific action does not exist yet. */
UCLASS()
class LEARNINGAGENTS_API UFloatArrayAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:

	/**
	* Adds a new float array action to the given agent type. Call during ULearningAgentsType::SetupActions event.
	* @param InAgentType The agent type to add this action to.
	* @param Name The name of this new action. Used for debugging.
	* @param Num The number of floats in the array
	* @param Scale Used to normalize the data for the action.
	* @return The newly created action.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UFloatArrayAction* AddFloatArrayAction(ULearningAgentsType* InAgentType, const FName Name = NAME_None, const int32 Num = 1, const float Scale = 1.0f);

	/**
	* Gets the data for this action. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @param OutValues The output array of floats
	*/
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	void GetFloatArrayAction(const int32 AgentId, TArray<float>& OutValues) const;

	/**
	* Sets the data for this action. Call during ULearningAgentsController::EncodeActions event.
	* @param AgentId The agent id to set data for.
	* @param Values The current action values.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AgentId = "-1"))
	void SetFloatArrayAction(const int32 AgentId, const TArray<float>& Values);

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	/** Describes this action to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

//------------------------------------------------------------------

/** A simple FVector action. */
UCLASS()
class LEARNINGAGENTS_API UVectorAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:

	/**
	* Adds a new vector action to the given agent type. Call during ULearningAgentsType::SetupActions event.
	* @param InAgentType The agent type to add this action to.
	* @param Name The name of this new action. Used for debugging.
	* @param Scale Used to normalize the data for the action.
	* @return The newly created action.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UVectorAction* AddVectorAction(ULearningAgentsType* InAgentType, const FName Name = NAME_None, const float Scale = 1.0f);

	/**
	* Gets the data for this action. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @return The current action values.
	*/
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	FVector GetVectorAction(const int32 AgentId) const;

	/**
	* Sets the data for this action. Call during ULearningAgentsController::EncodeActions event.
	* @param AgentId The agent id to set data for.
	* @param Value The current action values.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AgentId = "-1"))
	void SetVectorAction(const int32 AgentId, const FVector Value);

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	/** Describes this action to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

/** A simple array of FVector action. */
UCLASS()
class LEARNINGAGENTS_API UVectorArrayAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:

	/**
	* Adds a new vector action to the given agent type. Call during ULearningAgentsType::SetupActions event.
	* @param InAgentType The agent type to add this action to.
	* @param Name The name of this new action. Used for debugging.
	* @param Num The number of vectors in the array
	* @param Scale Used to normalize the data for the action.
	* @return The newly created action.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UVectorArrayAction* AddVectorArrayAction(ULearningAgentsType* InAgentType, const FName Name = NAME_None, const int32 Num = 1, const float Scale = 1.0f);

	/**
	* Gets the data for this action. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @param OutVectors The current action values.
	*/
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	void GetVectorArrayAction(const int32 AgentId, TArray<FVector>& OutVectors) const;

	/**
	* Sets the data for this action. Call during ULearningAgentsController::EncodeActions event.
	* @param AgentId The agent id to set data for.
	* @param Vectors The current action values.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AgentId = "-1"))
	void SetVectorArrayAction(const int32 AgentId, const TArray<FVector>& Vectors);

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	/** Describes this action to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

//------------------------------------------------------------------

/** A rotation action. */
UCLASS()
class LEARNINGAGENTS_API URotationAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:

	/**
	* Adds a new rotation action to the given agent type. Call during ULearningAgentsType::SetupActions event.
	* @param InAgentType The agent type to add this action to.
	* @param Name The name of this new action. Used for debugging.
	* @param Scale Used to normalize the data for the action.
	* @return The newly created action.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static URotationAction* AddRotationAction(ULearningAgentsType* InAgentType, const FName Name = NAME_None, const float Scale = 180.0f);

	/**
	* Gets the data for this action as a rotator. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @return The current action value.
	*/
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	FRotator GetRotationAction(const int32 AgentId) const;

	/**
	* Gets the data for this action as a rotation vector. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @return The current action value.
	*/
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	FVector GetRotationActionAsRotationVector(const int32 AgentId) const;

	/**
	* Gets the data for this action as a quaternion. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @return The current action value.
	*/
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	FQuat GetRotationActionAsQuat(const int32 AgentId) const;

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	/** Describes this action to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FRotationVectorFeature> FeatureObject;
};

/** An array of rotation actions. */
UCLASS()
class LEARNINGAGENTS_API URotationArrayAction : public ULearningAgentsAction
{
	GENERATED_BODY()

public:

	/**
	* Adds a new rotation array action to the given agent type. Call during ULearningAgentsType::SetupActions event.
	* @param InAgentType The agent type to add this action to.
	* @param Name The name of this new action. Used for debugging.
	* @param RotationNum The number of rotations in the array.
	* @param Scale Used to normalize the data for the action.
	* @return The newly created action.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static URotationArrayAction* AddRotationArrayAction(ULearningAgentsType* InAgentType, const FName Name = NAME_None, const int32 RotationNum = 1, const float Scale = 180.0f);

	/**
	* Gets the data for this action as rotators. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @param OutRotations The current action values.
	*/
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	void GetRotationArrayAction(const int32 AgentId, TArray<FRotator>& OutRotations) const;

	/**
	* Gets the data for this action as rotation vectors. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @param OutRotationVectors The current action values.
	*/
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	void GetRotationArrayActionAsRotationVectors(const int32 AgentId, TArray<FVector>& OutRotationVectors) const;

	/**
	* Gets the data for this action as quaternions. Call during ULearningAgentsType::GetActions event.
	* @param AgentId The agent id to get data for.
	* @param OutRotations The current action values.
	*/
	UFUNCTION(BlueprintPure = false, Category = "LearningAgents", meta = (AgentId = "-1"))
	void GetRotationArrayActionAsQuats(const int32 AgentId, TArray<FQuat>& OutRotations) const;

#if UE_LEARNING_AGENTS_ENABLE_VISUAL_LOG
	/** Describes this action to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FRotationVectorFeature> FeatureObject;
};
