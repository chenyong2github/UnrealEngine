// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimStateMachineTypes.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimNodeBase.h"

struct FBlueprintDebugData;
struct FPropertyAccessLibrary;
struct FAnimBlueprintDebugData;

/** Interface to the writable parts of the generated class that handlers can operate on */
class ANIMGRAPH_API IAnimBlueprintGeneratedClassCompiledData
{
public:
	// Get the baked state machines data for the currently-compiled class
	virtual TArray<FBakedAnimationStateMachine>& GetBakedStateMachines() const = 0;

	// Get the saved pose indices map data for the currently-compiled class
	virtual TMap<FName, FCachedPoseIndices>& GetOrderedSavedPoseIndicesMap() const = 0;

	// Get the debug data for the currently-compiled class
	virtual FBlueprintDebugData& GetBlueprintDebugData() const = 0;

	// Get the currently-compiled classes anim notifies
	virtual TArray<FAnimNotifyEvent>& GetAnimNotifies() const = 0;

	// Finds a notify event or adds if it doesn't already exist
	virtual int32 FindOrAddNotify(FAnimNotifyEvent& Notify) const = 0;

	// Get the currently-compiled classes exposed value handlers
	virtual TArray<FExposedValueHandler>& GetExposedValueHandlers() const = 0;

	// Get the currently-compiled classes property access library
	virtual FPropertyAccessLibrary& GetPropertyAccessLibrary() const = 0;

	// Get the anim debug data for the currently-compiled class
	virtual FAnimBlueprintDebugData& GetAnimBlueprintDebugData() const = 0;

	// Get the currently-compiled classes graph asset player information
	virtual TMap<FName, FGraphAssetPlayerInformation>& GetGraphAssetPlayerInformation() const = 0;
};