// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Engine/Blueprint.h"
#include "MovieSceneEvent.generated.h"

class UK2Node;
class UEdGraph;
class UBlueprint;
class UEdGraphNode;
class UK2Node_FunctionEntry;
class UMovieSceneEventSectionBase;

/** Value definition for any type-agnostic variable (exported as text) */
USTRUCT()
struct FMovieSceneEventPayloadVariable
{
	GENERATED_BODY()

	UPROPERTY()
	FString Value;
};

/** Compiled reflection pointers for the event function and parameters */
USTRUCT(BlueprintType)
struct FMovieSceneEventPtrs
{
	GENERATED_BODY()

	FMovieSceneEventPtrs()
		: Function(nullptr)
		, BoundObjectProperty(nullptr)
	{}

	UPROPERTY()
	UFunction* Function;

	UPROPERTY()
	UProperty* BoundObjectProperty;
};

USTRUCT(BlueprintType)
struct FMovieSceneEvent
{
	GENERATED_BODY()

	/**
	 * The function that should be called to invoke this event.
	 * Functions must have either no parameters, or a single, pass-by-value object/interface parameter, with no return parameter.
	 */
	UPROPERTY()
	FMovieSceneEventPtrs Ptrs;

public:

#if WITH_EDITORONLY_DATA

	/** Array of payload variables to be added to the generated function */
	UPROPERTY()
	TMap<FName, FMovieSceneEventPayloadVariable> PayloadVariables;

	UPROPERTY(transient)
	FName CompiledFunctionName;

	UPROPERTY()
	FName BoundObjectPinName;

	/** The UEdGraph::GraphGuid property that relates the graph within which our endpoint lives. */
	UPROPERTY()
	FGuid GraphGuid;

	/** When valid, relates to the The UEdGraphNode::NodeGuid for a custom event node that defines our event endpoint. When invalid, we must be bound to a UBlueprint::FunctionGraphs graph. */
	UPROPERTY()
	FGuid NodeGuid;

	/** Non-serialized weak pointer to the function entry within the blueprint graph for this event. Stored as an editor-only UObject so UHT can parse it when building for non-editor. */
	UPROPERTY(transient)
	TWeakObjectPtr<UObject> WeakCachedEndpoint;

	/** Deprecated weak pointer to the function entry to call - no longer serialized but cached on load. */
	UPROPERTY()
	TWeakObjectPtr<UObject> FunctionEntry_DEPRECATED;

#endif // WITH_EDITORONLY_DATA
};





