// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MovieSceneSequenceID.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"

#include "MovieSceneDynamicBinding.generated.h"

class UMovieScene;
class UMovieSceneSequence;
class UMovieSceneEntitySystemLinker;

/** Value definition for any type-agnostic variable (exported as text) */
USTRUCT(BlueprintType)
struct MOVIESCENE_API FMovieSceneDynamicBindingPayloadVariable
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Sequencer|Dynamic Binding")
	FString Value;
};

/**
 * Data for a dynamic binding endpoint call.
 */
USTRUCT()
struct MOVIESCENE_API FMovieSceneDynamicBinding
{
	GENERATED_BODY()

	/** The function to call (normally a generated blueprint function on the sequence director) */
	UPROPERTY()
	TObjectPtr<UFunction> Function;

	/** Property pointer for the function parameter that should receive the resolve params */
	UPROPERTY()
	TFieldPath<FProperty> ResolveParamsProperty;

#if WITH_EDITORONLY_DATA

	/** Array of payload variables to be added to the generated function */
	UPROPERTY(EditAnywhere, Category = "Sequencer|Dynamic Binding")
	TMap<FName, FMovieSceneDynamicBindingPayloadVariable> PayloadVariables;

	/** Name of the generated blueprint function */
	UPROPERTY(transient)
	FName CompiledFunctionName;

	/** Pin name for passing the resolve params */
	UPROPERTY(EditAnywhere, Category="Sequencer|Dynamic Binding")
	FName ResolveParamsPinName;

	/** Endpoint node in the sequence director */
	UPROPERTY(EditAnywhere, Category="Sequencer|Dynamic Binding")
	TWeakObjectPtr<UObject> WeakEndpoint;

#endif
};

/**
 * Optional parameter struct for dynamic binding resolver functions.
 */
USTRUCT(BlueprintType)
struct MOVIESCENE_API FMovieSceneDynamicBindingResolveParams
{
	GENERATED_BODY()

	/** The sequence that contains the object binding being resolved */
	UPROPERTY(EditAnywhere, Category="General")
	TObjectPtr<UMovieSceneSequence> Sequence;

	/** The ID of the object binding being resolved */
	UPROPERTY(EditAnywhere, Category="General")
	FGuid ObjectBindingID;

	/** The root sequence */
	UPROPERTY(EditAnywhere, Category="General")
	TObjectPtr<UMovieSceneSequence> RootSequence;
};

/**
 * Dummy structure for showing an FMovieSceneDynamicBinding inside a details view.
 */
USTRUCT()
struct MOVIESCENE_API FMovieSceneDynamicBindingContainer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Sequencer")
	FMovieSceneDynamicBinding DynamicBinding;
};

/**
 * Default dynamic binding resolver library, with several basic resolver functions.
 */
UCLASS(meta=(SequencerBindingResolverLibrary))
class MOVIESCENE_API UBuiltInDynamicBindingResolverLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Resolve the bound object to the player's pawn */
	UFUNCTION(BlueprintPure, Category="Sequencer|Dynamic Binding", meta=(WorldContext="WorldContextObject"))
	static UObject* ResolveToPlayerPawn(UObject* WorldContextObject, int32 PlayerControllerIndex = 0);
};

