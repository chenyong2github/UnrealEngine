// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithFBXSceneProcessor.h"

#include "CoreTypes.h"

struct FDatasmithFBXScene;
struct FDeltaGenTmlDataTimeline;

class FDatasmithDeltaGenSceneProcessor : public FDatasmithFBXSceneProcessor
{
public:
	FDatasmithDeltaGenSceneProcessor(FDatasmithFBXScene* InScene);

	/**
	 * Decompose all scene nodes with nonzero rotation and scaling pivots
	 * using dummy actors, and handle their animations
	 */
	void DecomposePivots(TArray<FDeltaGenTmlDataTimeline>& Timelines);
};