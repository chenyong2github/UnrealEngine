// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneEvent.h"
#include "Engine/Blueprint.h"
#include "MovieSceneEventSectionBase.generated.h"

/**
 * Base class for all event sections. Manages dirtying the section and track on recompilation of the director blueprint.
 */
UCLASS(MinimalAPI)
class UMovieSceneEventSectionBase
	: public UMovieSceneSection
{
public:
	GENERATED_BODY()

	virtual TArrayView<FMovieSceneEvent> GetAllEntryPoints() { return TArrayView<FMovieSceneEvent>(); }

	virtual void Serialize(FArchive& Ar) override;

#if WITH_EDITOR

	MOVIESCENETRACKS_API void AttemptUpgrade();

	DECLARE_MULTICAST_DELEGATE_TwoParams(FGenerateEventEntryPointFunctionsEvent, UMovieSceneEventSectionBase*, const FGenerateBlueprintFunctionParams&);
	DECLARE_MULTICAST_DELEGATE_FourParams(FFixupPayloadParameterNameEvent, UMovieSceneEventSectionBase*, UK2Node*, FName, FName);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FUpgradeLegacyEventEndpoint, UMovieSceneEventSectionBase*, UBlueprint*);


	/**
	 * Event handler that should be bound to a sequence director's UBlueprint::GenerateFunctionGraphsEvent member in order to generate event entrypoints
	 * This function is declared as a UFunction to ensure that it gets persistently serialized along with the UBlueprint
	 */
	UFUNCTION()
	void HandleGenerateEntryPoints(const FGenerateBlueprintFunctionParams& Params)
	{
		GenerateEventEntryPointsEvent.Broadcast(this, Params);
	}

	/**
	 * Handler should be invoked when an event endpoint that is referenced from this section has one of its pins renamed
	 */
	void OnUserDefinedPinRenamed(UK2Node* InNode, FName OldPinName, FName NewPinName)
	{
		FixupPayloadParameterNameEvent.Broadcast(this, InNode, OldPinName, NewPinName);
	}


	/**
	 * Post compilation handler that is invoked once generated function graphs have been compiled. Fixes up UFunction pointers for each event.
	 */
	MOVIESCENETRACKS_API void OnPostCompile(UBlueprint* Blueprint);


	/**
	 * Event that is broadcast when events need to be generated for a function. Implemented in this way so that editor-code can be kept within editor modules.
	 */
	MOVIESCENETRACKS_API static FGenerateEventEntryPointFunctionsEvent GenerateEventEntryPointsEvent;

	/**
	 * Event that is broadcast when event payloads may need fixing up due to a pin rename
	 */
	MOVIESCENETRACKS_API static FFixupPayloadParameterNameEvent FixupPayloadParameterNameEvent;

	/**
	 * Event that is broadcast when a legacy event section is found that needs fixing up against an already compile-on-loaded blueprint
	 */
	MOVIESCENETRACKS_API static FUpgradeLegacyEventEndpoint UpgradeLegacyEventEndpoint;

#endif

private:

#if WITH_EDITORONLY_DATA
	/** Legacy pointer to the sequence director BP */
	UPROPERTY()
	TWeakObjectPtr<UBlueprint> DirectorBlueprint_DEPRECATED;
#endif
};