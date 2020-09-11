// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/DatasmithSceneReceiver.h"

#include "DirectLink/DirectLinkCommon.h"
#include "DirectLink/DirectLinkLog.h"
#include "DirectLink/Misc.h"
#include "DirectLink/SceneSnapshot.h"

#include "DatasmithCore.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneGraphSharedState.h"
#include "IDatasmithSceneElements.h"


/* #ue_directlink_design
 *
 * the scene (aka sharedState) should be the owner of existing node.
 * Resolution of existing node should be provided by the scene, not by the DeltaConsumer.
 */


TSharedPtr<DirectLink::ISceneGraphNode> FDatasmithSceneReceiver::FDatasmithElementPointers::AsSharedPtr(DirectLink::FSceneGraphId NodeId)
{
	if (TSharedPtr<IDatasmithElement>* ElementPtr = ElementsSharedPtrs.Find(NodeId))
	{
		return *ElementPtr;
	}
	return nullptr;
}


FDatasmithSceneReceiver::FDatasmithSceneReceiver()
{
	Current = MakeUnique<FSceneState>();
}


TSharedPtr<IDatasmithScene> FDatasmithSceneReceiver::GetScene()
{
	return Current->Scene;
}


void FDatasmithSceneReceiver::FinalSnapshot(const DirectLink::FSceneSnapshot& SceneSnapshot)
{
	Current = MakeUnique<FSceneState>();
	TArray<FFinalizableNode2> Nodes;
	Nodes.Reserve(SceneSnapshot.Elements.Num());

	TSharedPtr<FDatasmithSceneGraphSharedState> SceneSharedState = MakeShared<FDatasmithSceneGraphSharedState>(SceneSnapshot.SceneId);

	for (const auto& KV : SceneSnapshot.Elements)
	{
		const DirectLink::FElementSnapshot& Snap = KV.Value.Get();
		DirectLink::FSceneGraphId NodeId = Snap.NodeId;
		const DirectLink::FParameterStoreSnapshot& DataSnapshot = Snap.DataSnapshot;

		FString Name;
		if (!DataSnapshot.GetValueAs("Name", Name))
		{
			UE_LOG(LogDatasmith, Display, TEXT("OnAddElement failed: missing element name for node #%d"), NodeId);
			return;
		}

		uint64 Type = 0;
		if (!DataSnapshot.GetValueAs("Type", Type))
		{
			UE_LOG(LogDatasmith, Display, TEXT("OnAddElement failed: missing element type info for node '%s'"), *Name);
			return;
		}

		// derived types have several bits sets.
		// -> keep the leftmost bit, which is the value of the (most-derived) class understood by CreateElement
		// eg. this transforms 'Actor|StaticMeshActor' into 'StaticMeshActor'
		// Well, of course it's not exact...
		// Type &= (uint64)~EDatasmithElementType::BaseMaterial; // remove that flag as it always has a child anyway, and its order is impractical.
		EDatasmithElementType PureType = EDatasmithElementType(uint64(1) << FPlatformMath::FloorLog2_64(Type));

		TSharedPtr<IDatasmithElement> Element = FDatasmithSceneFactory::CreateElement(PureType, *Name);
		check(Element);
		Element->SetSharedState(SceneSharedState);
		Element->SetNodeId(NodeId); // #ue_directlink_design nope, only the Scene SharedState has this right
		Current->Elements.Add(NodeId, Element);

		const TCHAR* ElementTypeName = GetElementTypeName(Element.Get());
		UE_LOG(LogDatasmith, Display, TEXT("OnAddElement -> %s'%s' id=%d"), ElementTypeName, *Name, NodeId);
		check(Element);

		FFinalizableNode2& Node = Nodes.AddDefaulted_GetRef();
		Node.Element = Element;
		Node.Snapshot = &Snap;
	}

	// all nodes are created, link refs
	for (FFinalizableNode2& Node : Nodes)
	{
		Node.Element->UpdateRefs(Current->Elements, Node.Snapshot->RefSnapshot);
	}

	// set data
	for (FFinalizableNode2& Node : Nodes)
	{
		Node.Element->GetStore().Update(Node.Snapshot->DataSnapshot);
	}

	// detect graph root
	for (FFinalizableNode2& Node : Nodes)
	{
		if (Node.Element->IsA(EDatasmithElementType::Scene))
		{
			Current->Scene = StaticCastSharedPtr<IDatasmithScene>(Node.Element);

			if (ensure(Current->Scene))
			{
				DumpDatasmithScene(Current->Scene.ToSharedRef(), TEXT("received"));
			}
			break;
		}
	}

	if (ChangeListener)
	{
		ChangeListener->OnOpenDelta();
		{
			ChangeListener->OnNewScene(Current->SceneId);
			for (const auto& Pair : Current->Elements.ElementsSharedPtrs)
			{
				ChangeListener->OnAddElement(Pair.Value);
			}
		}
		ChangeListener->OnCloseDelta();
	}
}
