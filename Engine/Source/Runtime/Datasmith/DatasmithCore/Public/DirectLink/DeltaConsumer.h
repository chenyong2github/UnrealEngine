// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DirectLinkCommon.h"
#include "DirectLink/ElementSnapshot.h"
#include "DirectLink/ParameterStore.h"

#include "CoreTypes.h"


namespace DirectLink
{

class IDeltaProducer
{
public:
	virtual ~IDeltaProducer() = default;

	struct FAckMessage
	{
		enum EMsgType {NewScene, ElementHaveRev};
		EMsgType Type;
		FSceneGraphId NodeId;
		FElementHash HaveHash;
	};
	virtual void Ack(FAckMessage& AckMessage) {}
	virtual void HaveScene(FSceneIdentifier SceneId) {}
	virtual void HaveElement(FSceneGraphId NodeId, FElementHash HaveHash) {}
};



class IDeltaConsumer
{
public:
	virtual ~IDeltaConsumer() = default;

	virtual void SetDeltaProducer(IDeltaProducer* Producer) = 0;

	struct FOpenDeltaArg
	{
		FSceneIdentifier SceneId;
		uint32 ElementCountHint = 0;
	};
	virtual void OnOpenDelta(FOpenDeltaArg& OpenDeltaArg) = 0;

	struct FSetElementArg
	{
		FElementSnapshot Snapshot;
	};
	virtual void OnSetElement(FSetElementArg& SetElementArg) = 0;

	struct FCloseDeltaArg
	{
	};
	virtual void OnCloseDelta(FCloseDeltaArg& CloseDeltaArg) = 0;
};

} // namespace DirectLink
