// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "DirectLink/SceneIndex.h"

namespace DirectLink
{

class ISceneGraphNode;

/**
 * Builds a new index for an existing scene
 */
class FSceneIndexBuilder
{
public:
	void InitFromRootElement(ISceneGraphNode* RootElement);

	FLocalSceneIndex& GetIndex() { return Index; }

private:
	void AddElement(ISceneGraphNode* Element, int32 RecLevel = 0);
	FLocalSceneIndex Index;
};


FLocalSceneIndex DATASMITHCORE_API BuildIndexForScene(ISceneGraphNode* RootElement);


} // namespace DirectLink
