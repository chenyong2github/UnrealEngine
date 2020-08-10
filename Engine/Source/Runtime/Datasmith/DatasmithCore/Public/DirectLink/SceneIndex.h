// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "CoreTypes.h"

#include "DirectLink/DeltaConsumer.h"
#include "DirectLink/ElementSnapshot.h"
#include "DirectLink/SceneGraphNode.h"


namespace DirectLink
{

class IDeltaConsumer;


struct FSharedId
{
	FSceneGraphId Id = InvalidId;
	FElementHash Hash = InvalidHash;
};



class FRemoteScene
	: public IDeltaProducer
{
public:
	struct FRemoteNodeStatus
	{
		FSceneGraphId NodeId = InvalidId;
		FElementHash HaveHash = InvalidHash;
		FElementHash SentHash = InvalidHash;
		uint32 SentCycle = 0;
	};

public:
	~FRemoteScene();

	void SetDeltaConsumer(const TSharedPtr<IDeltaConsumer>& InDeltaConsumer);
	const TSharedPtr<IDeltaConsumer>& GetDeltaConsumer() const { return Consumer; }

	// API for the consumer to acknowledge
	virtual void HaveElement(FSceneGraphId NodeId, FElementHash HaveHash) override;
	virtual void HaveScene(FSceneIdentifier InSceneId) override { SetSceneId(InSceneId); }

public:
	void SetSceneId(const FSceneIdentifier& InSceneId)
	{
		if (SceneId.SceneGuid != InSceneId.SceneGuid)
		{
			HaveList.Reset();
			HaveListHash = InvalidHash; // NIY
		}
		SceneId = InSceneId;
	}
	const FSceneIdentifier& GetSceneId() const { return SceneId; }
	const FRemoteNodeStatus& GetOrCreateNodeStatus(FSceneGraphId NodeId)
	{
		if (const FRemoteNodeStatus* Ref = HaveList.Find(NodeId))
		{
			return *Ref;
		}

		FRemoteNodeStatus& Ref = HaveList.Add(NodeId);
		Ref.NodeId = NodeId;
		return Ref;
	}

private:
	FSceneIdentifier SceneId;
	TMap<FSceneGraphId, FRemoteNodeStatus> HaveList;
	FElementHash HaveListHash = InvalidHash;
	TSharedPtr<IDeltaConsumer> Consumer;
};


struct FLocalElementReference
{
	FSharedId SnapshotSharedId;
	ISceneGraphNode* SnapshotSource = nullptr;

	TSharedPtr<FElementSnapshot> GetSnapshot();

private:
	TSharedPtr<FElementSnapshot> Snapshot; // #ue_directlink_cleanup useless -> get in the snapshotbank
};


class FLocalSceneIndex
{
public:
	FLocalSceneIndex() = default;
	FLocalSceneIndex(const FSceneIdentifier& SceneId)
		: SceneId(SceneId)
	{}

	bool AddReference(ISceneGraphNode* Element);

	TMap<FSceneGraphId, FLocalElementReference>& GetReferences() { return References; }
	const TMap<FSceneGraphId, FLocalElementReference>& GetReferences() const { return References; }

	const FSceneIdentifier& GetSceneIdentifier() const
	{
		return SceneId;
	}

private:
	FSceneIdentifier SceneId;
	TMap<FSceneGraphId, FLocalElementReference> References;
};


/**
 * Points on a scene graph, can snapshot (index) it.
 * Also hold a set of remote scene that can be synced to this scene
 */
class DATASMITHCORE_API FIndexedScene
{
public:
	FIndexedScene(ISceneGraphNode* RootElement, bool bAutoIndex=true)
		: RootElement(RootElement)
	{
		if (RootElement && bAutoIndex)
		{
			UpdateLocalIndex();
		}
	}

	TSharedRef<FRemoteScene> NewRemote(TSharedRef<IDeltaConsumer> DeltaConsumer);
	void UpdateRemotes();

	void UpdateLocalIndex();

private:
	ISceneGraphNode* RootElement = nullptr;

	FLocalSceneIndex CurrentIndex;

	TArray<TSharedRef<FRemoteScene>> Remotes;
};

void DoDiff(FLocalSceneIndex& Local, FRemoteScene& Remote);

} // namespace DirectLink
