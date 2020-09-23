// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtensionContext.generated.h"

class ISceneViewExtension;
class FViewport;

/** 
 * Contains information about the context in which this scene view extension will be used. 
 */
struct FSceneViewExtensionContext
{
private:
	// A quick and dirty way to determine which Context (sub)class this is. Every subclass should implement it.
	virtual FName GetRTTI() const { return TEXT("FSceneViewExtensionContext"); }

public:

	// The default object that defines a scene view extension is a viewport.
	FViewport* Viewport = nullptr;

	FSceneViewExtensionContext(FViewport* InViewport = nullptr) 
		: Viewport(InViewport)
	{
	}

	virtual ~FSceneViewExtensionContext() {}
	
	// Returns true if the given object is of the same type.
	bool IsA(const FSceneViewExtensionContext&& Other) const
	{ 
		return GetRTTI() == Other.GetRTTI(); 
	}
};


/**
 * Convenience type definition of a function that gives an opinion of whether the scene view extension should be active in the given context for the current frame.
 */
using TSceneViewExtensionIsActiveFunction = TFunction<TOptional<bool>(const ISceneViewExtension* SceneViewExtension, FSceneViewExtensionContext& Context)>;

/**
 * Contains the TFunction that determines if a scene view extension should be valid in the given context given for the current frame.
 * It also contains Guid to help identify it, given that we can't directly compare TFunctions.
 */
USTRUCT(BlueprintType)
struct FSceneViewExtensionIsActiveFunctor
{
	GENERATED_BODY()

private:

	// The Guid is a way to identify the lambda in case it you want to later find it and remove it.
	FGuid Guid;

public:

	// Constructor
	FSceneViewExtensionIsActiveFunctor()
		: Guid(FGuid::NewGuid())
	{}

	// Returns the Guid of this Functor.
	FGuid GetGuid()
	{
		return Guid;
	}

	// This is the lambda function used to determine if the Scene View Extension should be active or not.
	TSceneViewExtensionIsActiveFunction IsActiveFunction;

	// Make this a functor so that it behaves like the lambda it carries.
	TOptional<bool> operator () (const ISceneViewExtension* SceneViewExtension, FSceneViewExtensionContext& Context) const
	{
		// If there is no lambda assigned, simply return an unset optional.
		if (!IsActiveFunction)
		{
			return TOptional<bool>();
		}

		// Evaluate the lambda function with the given arguments
		return IsActiveFunction(SceneViewExtension, Context);
	}
};


