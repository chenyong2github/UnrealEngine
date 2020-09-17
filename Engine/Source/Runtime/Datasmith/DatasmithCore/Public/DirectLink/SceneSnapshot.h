// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DirectLinkCommon.h"


namespace DirectLink
{
class FElementSnapshot;
class ISceneGraphNode;


class FSceneSnapshot
{
public:
	TMap<FSceneGraphId, TSharedRef<FElementSnapshot>> Elements;
	FSceneIdentifier SceneId;
};


TSet<ISceneGraphNode*> DATASMITHCORE_API BuildIndexForScene(ISceneGraphNode* RootElement);
TSharedPtr<FSceneSnapshot> DATASMITHCORE_API SnapshotScene(ISceneGraphNode* RootElement);


} // namespace DirectLink
