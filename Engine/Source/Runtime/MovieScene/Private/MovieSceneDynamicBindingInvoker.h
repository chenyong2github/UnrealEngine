// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "CoreTypes.h"

class UObject;
class IMovieScenePlayer;
class UMovieSceneSequence;
struct FGuid;
struct FMovieSceneSequenceID;
struct FMovieSceneDynamicBinding;
struct FMovieSceneDynamicBindingResolveParams;

/**
 * Utility class for invoking dynamic binding endpoints.
 */
struct FMovieSceneDynamicBindingInvoker
{
	/** Invoke the dynamic binding, if any, and add the result to the given array of objects */
	static bool ResolveDynamicBinding(IMovieScenePlayer& Player, UMovieSceneSequence* Sequence, const FMovieSceneSequenceID& SequenceID, const FGuid& InGuid, const FMovieSceneDynamicBinding& DynamicBinding, TArray<UObject*, TInlineAllocator<1>>& OutObjects);
	/** Invoke the dynamic binding, if any, and return the result */
	static UObject* ResolveDynamicBinding(IMovieScenePlayer& Player, UMovieSceneSequence* Sequence, const FMovieSceneSequenceID& SequenceID, const FGuid& InGuid, const FMovieSceneDynamicBinding& DynamicBinding);

private:
	static UObject* InvokeDynamicBinding(UObject* DirectorInstance, const FMovieSceneDynamicBinding& DynamicBinding, const FMovieSceneDynamicBindingResolveParams& Params);
};

