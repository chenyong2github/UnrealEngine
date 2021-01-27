// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "MovieScenePropertySystem.generated.h"

class UMovieScenePropertyInstantiatorSystem;


/** Abstract base class for any property system that deals with a property registered with FBuiltInComponentTypes::PropertyRegistry */
UCLASS(Abstract)
class MOVIESCENETRACKS_API UMovieScenePropertySystem : public UMovieSceneEntitySystem
{
public:
	GENERATED_BODY()

	UMovieScenePropertySystem(const FObjectInitializer& ObjInit);

	/**
	 * Must be called on construction of derived classes to initialize the members necessary for this system to animate its property
	 */
	template<typename PropertyType, typename OperationalType>
	void BindToProperty(const UE::MovieScene::TPropertyComponents<PropertyType, OperationalType>& InComponents)
	{
		check(!RelevantComponent && !CompositePropertyID);

		RelevantComponent = InComponents.PropertyTag;
		CompositePropertyID = InComponents.CompositeID;
	}

protected:

	virtual void OnLink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	/** Pointer to the property instantiator system for retrieving property stats */
	UPROPERTY()
	TObjectPtr<UMovieScenePropertyInstantiatorSystem> InstantiatorSystem;

	/** Must be set on construction - the composite type of the property this system operates with */
	UE::MovieScene::FCompositePropertyTypeID CompositePropertyID;
};
