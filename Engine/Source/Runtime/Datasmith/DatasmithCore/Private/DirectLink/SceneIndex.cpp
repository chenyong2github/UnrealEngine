// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/SceneIndex.h"

#include "DirectLink/DeltaConsumer.h"
#include "DirectLink/DirectLinkLog.h"
#include "DirectLink/Misc.h"
#include "DirectLink/ParameterStore.h"
#include "DirectLink/SceneGraphNode.h"
#include "DirectLink/SceneIndexBuilder.h"


namespace DirectLink
{

void DoDiff(FLocalSceneIndex& Local, FRemoteScene& Remote)
{
	uint32 MessageOrder = 0;
	Remote.SetSceneId(Local.GetSceneIdentifier()); // make sure the havelist is relevant
	const TSharedPtr<IDeltaConsumer>& DeltaConsumer = Remote.GetDeltaConsumer();

	if (!DeltaConsumer.IsValid())
	{
		UE_LOG(LogDirectLinkIndexer, Warning, TEXT("No stream associated with remote"));
		return;
	}

	IDeltaConsumer::FOpenDeltaArg OpenArg;
// 	OpenArg.DeltaCode = ThisDeltaCode;
	OpenArg.SceneId = Local.GetSceneIdentifier();
// 	OpenArg.PreDeltaSceneState = InvalidHash; // Remote.GetHaveListHash(); // NIY
	OpenArg.ElementCountHint = Local.GetReferences().Num();
	DeltaConsumer->OnOpenDelta(OpenArg); // during this call, consumer may update the havelist

	for (auto& RefPair : Local.GetReferences())
	{
		FLocalElementReference& LocalRef = RefPair.Value;
		FSceneGraphId NodeId = LocalRef.SnapshotSharedId.Id;

		TSharedPtr<FElementSnapshot> Snapshot = LocalRef.GetSnapshot(); // #ue_directlink_optim: parallel snapshot generation
		if (!Snapshot.IsValid())
		{
			UE_LOG(LogDirectLinkIndexer, Warning, TEXT("No snapshot while sending to remote"));
			continue;
		}

		if (LocalRef.SnapshotSharedId.Hash == InvalidHash)
		{
			LocalRef.SnapshotSharedId.Hash = LocalRef.GetSnapshot()->GetHash();
		}

		FElementHash NodeHash = LocalRef.SnapshotSharedId.Hash;

		const FRemoteScene::FRemoteNodeStatus& RemoteRef = Remote.GetOrCreateNodeStatus(NodeId);

		if (NodeHash != InvalidHash)
		{
			if (RemoteRef.HaveHash == NodeHash)
			{
				UE_LOG(LogDirectLinkIndexer, VeryVerbose, TEXT("diff: Skipped %d, have hash match"), NodeId);
				continue;
			}

			if (RemoteRef.SentHash == NodeHash)
			{
				UE_LOG(LogDirectLinkIndexer, VeryVerbose, TEXT("diff: resent %d, sent hash match, but NIY"), NodeId);
			}
		}
		IDeltaConsumer::FSetElementArg SetArg;
// 		SetArg.DeltaCode = ThisDeltaCode;
		SetArg.Snapshot = *Snapshot; // #ue_directlink_optim review this copy: FSetElementArg could use a sharedptr ?

		DeltaConsumer->OnSetElement(SetArg);
	}

	IDeltaConsumer::FCloseDeltaArg CloseArg;
	DeltaConsumer->OnCloseDelta(CloseArg);
}

TSharedPtr<FElementSnapshot> FLocalElementReference::GetSnapshot()
{
	if (Snapshot == nullptr)
	{
		if (ensure(SnapshotSource))
		{
			Snapshot = MakeShared<FElementSnapshot>(*SnapshotSource);
		}
	}

	return Snapshot;
}


bool FLocalSceneIndex::AddReference(ISceneGraphNode* Element)
{
	if (!Element)
	{
		return false;
	}

	FSceneGraphId NodeId = Element->GetNodeId();

	if (NodeId == InvalidId)
	{
		return false;
	}

	FLocalElementReference* RefPtr = References.Find(NodeId);
	if (RefPtr == nullptr)
	{
		RefPtr = &References.Add(NodeId);
		RefPtr->SnapshotSharedId.Id = NodeId;
		RefPtr->SnapshotSharedId.Hash = InvalidHash;
		RefPtr->SnapshotSource = Element;
		UE_LOG(LogDirectLinkIndexer, VeryVerbose, TEXT("Indexed node %d"), NodeId);
		return true;
	}

	UE_LOG(LogDirectLinkIndexer, VeryVerbose, TEXT("Already indexed node %d"), NodeId);
	return false;
}

TSharedRef<FRemoteScene> FIndexedScene::NewRemote(TSharedRef<IDeltaConsumer> DeltaConsumer)
{
	const TSharedRef<FRemoteScene>& NewRemote = Remotes.Add_GetRef(MakeShared<FRemoteScene>());
	NewRemote->SetDeltaConsumer(DeltaConsumer);
	return NewRemote;
}

void FIndexedScene::UpdateRemotes()
{
	for (const TSharedRef<FRemoteScene>& RemoteScene : Remotes)
	{
		DoDiff(CurrentIndex, RemoteScene.Get());
	}
}

void FIndexedScene::UpdateLocalIndex()
{
	FSceneIndexBuilder Builder;
	Builder.InitFromRootElement(RootElement);
	CurrentIndex = MoveTemp(Builder.GetIndex());
}

FRemoteScene::~FRemoteScene()
{
	if (Consumer.IsValid())
	{
		Consumer->SetDeltaProducer(nullptr);
	}
}

void FRemoteScene::SetDeltaConsumer(const TSharedPtr<IDeltaConsumer>& InDeltaConsumer)
{
	Consumer = InDeltaConsumer;
	if (Consumer.IsValid())
	{
		Consumer->SetDeltaProducer(this);
	}
}

void FRemoteScene::HaveElement(FSceneGraphId NodeId, FElementHash HaveHash)
{
	// update have hash.
	// #ue_directlink_sync not yet synced over UDP ! Have element should be pure virt.
	if (FRemoteNodeStatus* NodeStatus = HaveList.Find(NodeId))
	{
		NodeStatus->HaveHash = HaveHash;
	}
}

} // namespace DirectLink
