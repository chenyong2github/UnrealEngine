// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "EngineDefines.h"

#include "LearningAgentsObservations.generated.h"

namespace UE::Learning
{
	struct FFeatureObject;
	struct FFloatFeature;
	struct FAngleFeature;
	struct FPlanarDirectionFeature;
	struct FDirectionFeature;
	struct FPlanarPositionFeature;
	struct FPositionFeature;
	struct FPlanarVelocityFeature;
	struct FVelocityFeature;
}

class ULearningAgentsType;

// For functions in this file, we are favoring having more verbose names such as "AddFloatObservation" vs simply "Add" in 
// order to keep it easy to find the correct function in blueprints.

/** The base class for all observations. Observations define the inputs to your agents. */
UCLASS(Abstract, BlueprintType)
class LEARNINGAGENTS_API ULearningAgentsObservation : public UObject
{
	GENERATED_BODY()

public:

#if ENABLE_VISUAL_LOG
	/** Color used to draw this observation in the visual log */
	FLinearColor VisualLogColor = FColor::Red;

	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const {}
#endif
};

/** A simple float observation. Used as a catch-all for situations where a more type-specific observation does not exist yet. */
UCLASS()
class LEARNINGAGENTS_API UFloatObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new float observation to the given agent type. Call during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param Scale Used to normalize the data for the observation.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UFloatObservation* AddFloatObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 1.0f);

	/**
	* Sets the data for this observation. Call during ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Observation The value currently being observed.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetFloatObservation(const int32 AgentId, const float Observation);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

/** A simple observation for an FVector. */
UCLASS()
class LEARNINGAGENTS_API UVectorObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new vector observation to the given agent type. Call during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param Scale Used to normalize the data for the observation.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UVectorObservation* AddVectorObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 1.0f);

	/**
	* Sets the data for this observation. Call during ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Observation The values currently being observed.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetVectorObservation(const int32 AgentId, const FVector Observation);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FFloatFeature> FeatureObject;
};

/** An observation of an angle relative to another angle. */
UCLASS()
class LEARNINGAGENTS_API UAngleObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new angle observation to the given agent type. Call during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param Scale Used to normalize the data for the observation.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UAngleObservation* AddAngleObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 1.0f);

	/**
	* Sets the data for this observation. Call during ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Angle The angle currently being observed.
	* @param RelativeAngle The frame of reference angle.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetAngleObservation(const int32 AgentId, const float Angle, const float RelativeAngle = 0.0f);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FAngleFeature> FeatureObject;
};

/** An observation of a direction vector projected onto a plane. */
UCLASS()
class LEARNINGAGENTS_API UPlanarDirectionObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new planar direction observation to the given agent type. The axis parameters define the plane. Call
	* during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param Scale Used to normalize the data for the observation.
	* @param Axis0 The forward axis of the plane.
	* @param Axis1 The right axis of the plane.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarDirectionObservation* AddPlanarDirectionObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 1.0f, const FVector Axis0 = FVector::ForwardVector, const FVector Axis1 = FVector::RightVector);

	/**
	* Sets the data for this observation. The relative rotation can be used to make this observation relative to the
	* agent's perspective, e.g. by passing the agent's forward rotation. Call during ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Direction The direction currently being observed.
	* @param RelativeRotation The frame of reference rotation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarDirectionObservation(const int32 AgentId, const FVector Direction, const FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarDirectionFeature> FeatureObject;
};

/** An observation of a direction vector. */
UCLASS()
class LEARNINGAGENTS_API UDirectionObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new direction observation to the given agent type. Call during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param Scale Used to normalize the data for the observation.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UDirectionObservation* AddDirectionObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 1.0f);

	/**
	* Sets the data for this observation. The relative rotation can be used to make this observation relative to the
	* agent's perspective, e.g. by passing the agent's forward rotation. Call during ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Direction The direction currently being observed.
	* @param RelativeRotation The frame of reference rotation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetDirectionObservation(const int32 AgentId, const FVector Direction, const FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FDirectionFeature> FeatureObject;
};

/** An observation of a position projected onto a plane. */
UCLASS()
class LEARNINGAGENTS_API UPlanarPositionObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new planar position observation to the given agent type. The axis parameters define the plane. Call
	* during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param Scale Used to normalize the data for the observation.
	* @param Axis0 The forward axis of the plane.
	* @param Axis1 The right axis of the plane.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarPositionObservation* AddPlanarPositionObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 100.0f, const FVector Axis0 = FVector::ForwardVector, const FVector Axis1 = FVector::RightVector);

	/**
	* Sets the data for this observation. The relative position & rotation can be used to make this observation
	* relative to the agent's perspective, e.g. by passing the agent's position & forward rotation. Call during
	* ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Position The position currently being observed.
	* @param RelativePosition The vector Position will be offset from.
	* @param RelativeRotation The frame of reference rotation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarPositionObservation(const int32 AgentId, const FVector Position, const FVector RelativePosition = FVector::ZeroVector, const FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarPositionFeature> FeatureObject;
};

/** An observation of a position vector. */
UCLASS()
class LEARNINGAGENTS_API UPositionObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new position observation to the given agent type. Call during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param Scale Used to normalize the data for the observation.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPositionObservation* AddPositionObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 100.0f);

	/**
	* Sets the data for this observation. The relative position & rotation can be used to make this observation
	* relative to the agent's perspective, e.g. by passing the agent's position & forward rotation. Call during
	* ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Position The position currently being observed.
	* @param RelativePosition The vector Position will be offset from.
	* @param RelativeRotation The frame of reference rotation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPositionObservation(const int32 AgentId, const FVector Position, const FVector RelativePosition = FVector::ZeroVector, const FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPositionFeature> FeatureObject;
};

/** An observation of an array of positions projected onto a plane. */
UCLASS()
class LEARNINGAGENTS_API UPlanarPositionArrayObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new planar position array observation to the given agent type. The axis parameters define the plane.
	* Call during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param PositionNum The number of positions in the array.
	* @param Scale Used to normalize the data for the observation.
	* @param Axis0 The forward axis of the plane.
	* @param Axis1 The right axis of the plane.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarPositionArrayObservation* AddPlanarPositionArrayObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const int32 PositionNum = 0, const float Scale = 100.0f, const FVector Axis0 = FVector::ForwardVector, const FVector Axis1 = FVector::RightVector);


	/**
	* Sets the data for this observation. The relative position & rotation can be used to make this observation
	* relative to the agent's perspective, e.g. by passing the agent's position & forward rotation. Call during
	* ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Positions The positions currently being observed.
	* @param RelativePosition The vector Positions will be offset from.
	* @param RelativeRotation The frame of reference rotation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarPositionArrayObservation(const int32 AgentId, const TArray<FVector>& Positions, const FVector RelativePosition = FVector::ZeroVector, const FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarPositionFeature> FeatureObject;
};

/** An observation of an array of positions. */
UCLASS()
class LEARNINGAGENTS_API UPositionArrayObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new position array observation to the given agent type. Call during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param PositionNum The number of positions in the array.
	* @param Scale Used to normalize the data for the observation.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPositionArrayObservation* AddPositionArrayObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const int32 PositionNum = 0, const float Scale = 100.0f);

	/**
	* Sets the data for this observation. The relative position & rotation can be used to make this observation
	* relative to the agent's perspective, e.g. by passing the agent's position & forward rotation. Call during
	* ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Positions The positions currently being observed.
	* @param RelativePosition The vector Positions will be offset from.
	* @param RelativeRotation The frame of reference rotation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPositionArrayObservation(int32 AgentId, const TArray<FVector>& Positions, const FVector RelativePosition = FVector::ZeroVector, const FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPositionFeature> FeatureObject;
};

/** An observation of a velocity projected onto a plane. */
UCLASS()
class LEARNINGAGENTS_API UPlanarVelocityObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new planar velocity observation to the given agent type. The axis parameters define the plane.
	* Call during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param Scale Used to normalize the data for the observation.
	* @param Axis0 The forward axis of the plane.
	* @param Axis1 The right axis of the plane.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UPlanarVelocityObservation* AddPlanarVelocityObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 200.0f, const FVector Axis0 = FVector::ForwardVector, const FVector Axis1 = FVector::RightVector);

	/**
	* Sets the data for this observation. The relative rotation can be used to make this observation relative to the
	* agent's perspective, e.g. by passing the agent's forward rotation. Call during ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Velocity The velocity currently being observed.
	* @param RelativeRotation The frame of reference rotation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetPlanarVelocityObservation(const int32 AgentId, const FVector Velocity, const FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FPlanarVelocityFeature> FeatureObject;
};

/** An observation of a velocity. */
UCLASS()
class LEARNINGAGENTS_API UVelocityObservation : public ULearningAgentsObservation
{
	GENERATED_BODY()

public:

	/**
	* Adds a new velocity observation to the given agent type. Call during ULearningAgentsType::SetupObservations event.
	* @param AgentType The agent type to add this observation to.
	* @param Name The name of this new observation. Used for debugging.
	* @param Scale Used to normalize the data for the observation.
	* @return The newly created observation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	static UVelocityObservation* AddVelocityObservation(ULearningAgentsType* AgentType, const FName Name = NAME_None, const float Scale = 200.0f);

	/**
	* Sets the data for this observation. The relative rotation can be used to make this observation relative to the
	* agent's perspective, e.g. by passing the agent's forward rotation. Call during ULearningAgentsType::SetObservations event.
	* @param AgentId The agent id this data corresponds to.
	* @param Velocity The velocity currently being observed.
	* @param RelativeRotation The frame of reference rotation.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetVelocityObservation(const int32 AgentId, const FVector Velocity, const FRotator RelativeRotation = FRotator::ZeroRotator);

#if ENABLE_VISUAL_LOG
	/** Describes this observation to the visual logger for debugging purposes. */
	virtual void VisualLog(const UE::Learning::FIndexSet Instances) const override;
#endif

	TSharedPtr<UE::Learning::FVelocityFeature> FeatureObject;
};
