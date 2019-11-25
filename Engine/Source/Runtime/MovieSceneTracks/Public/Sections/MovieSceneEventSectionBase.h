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

	DECLARE_MULTICAST_DELEGATE_FourParams(FFixupPayloadParameterNameEvent, UMovieSceneEventSectionBase*, UK2Node*, FName, FName);
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FUpgradeLegacyEventEndpoint, UMovieSceneEventSectionBase*, UBlueprint*);

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
	 * Event that is broadcast when event payloads may need fixing up due to a pin rename
	 */
	MOVIESCENETRACKS_API static FFixupPayloadParameterNameEvent FixupPayloadParameterNameEvent;

	/**
	 * Delegate that is used to upgrade legacy event sections that need fixing up against a blueprint. Called on serialization and on compilation if necessary until successful upgrade occurs.
	 * Must return true on success or false on failure.
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