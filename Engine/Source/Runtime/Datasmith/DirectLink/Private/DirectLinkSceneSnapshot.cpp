// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLinkSceneSnapshot.h"

#include "DirectLinkLog.h"
#include "DirectLinkElementSnapshot.h"
#include "DirectLinkSceneGraphNode.h"


namespace DirectLink
{


void RecursiveAddElements(TSet<ISceneGraphNode*>& Nodes, ISceneGraphNode* Element)
{
	if (Element == nullptr)
	{
		UE_LOG(LogDirectLink, Warning, TEXT("Try to index null element"));
		return;
	}

	bool bWasAlreadyThere;
	Nodes.Add(Element, &bWasAlreadyThere);
	if (bWasAlreadyThere)
	{
		return;
	}

	// Recursive
	for (int32 ProxyIndex = 0; ProxyIndex < Element->GetReferenceProxyCount(); ++ProxyIndex)
	{
		IReferenceProxy* RefProxy = Element->GetReferenceProxy(ProxyIndex);
		int32 ReferenceCount = RefProxy->Num();
		for (int32 ReferenceIndex = 0; ReferenceIndex < ReferenceCount; ReferenceIndex++)
		{
			if (ISceneGraphNode* Referenced = RefProxy->GetNode(ReferenceIndex))
			{
				Element->RegisterReference(Referenced);
				RecursiveAddElements(Nodes, Referenced);
			}
		}
	}
}

TSet<ISceneGraphNode*> BuildIndexForScene(ISceneGraphNode* RootElement)
{
	TSet<ISceneGraphNode*> Nodes;

	if (RootElement)
	{
		if (!RootElement->GetSharedState().IsValid())
		{
			RootElement->SetSharedState(RootElement->MakeSharedState());
			if (!RootElement->GetSharedState().IsValid())
			{
				return Nodes;
			}
		}
		RecursiveAddElements(Nodes, RootElement);
	}

	return Nodes;
}


TSharedPtr<FSceneSnapshot> SnapshotScene(ISceneGraphNode* RootElement)
{
	if (RootElement == nullptr)
	{
		return nullptr;
	}

	TSharedPtr<FSceneSnapshot> SceneSnapshot = MakeShared<FSceneSnapshot>();

	TSet<ISceneGraphNode*> Nodes = BuildIndexForScene(RootElement);

	if (ensure(RootElement->GetSharedState().IsValid()))
	{
		auto& ElementSnapshots = SceneSnapshot->Elements;
		SceneSnapshot->SceneId = RootElement->GetSharedState()->GetSceneId();

		// #ue_directlink_optim: parallel snapshot generation
		for (ISceneGraphNode* Element : Nodes)
		{
			ElementSnapshots.Add(Element->GetNodeId(), MakeShared<FElementSnapshot>(*Element));
		}
	}

	return SceneSnapshot;
}


} // namespace DirectLink
