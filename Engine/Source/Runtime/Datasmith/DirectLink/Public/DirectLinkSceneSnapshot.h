// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLinkCommon.h"


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


TSet<ISceneGraphNode*> DIRECTLINK_API BuildIndexForScene(ISceneGraphNode* RootElement);
TSharedPtr<FSceneSnapshot> DIRECTLINK_API SnapshotScene(ISceneGraphNode* RootElement);


} // namespace DirectLink
