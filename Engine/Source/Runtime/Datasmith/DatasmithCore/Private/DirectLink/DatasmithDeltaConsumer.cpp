// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/DatasmithDeltaConsumer.h"

#include "DirectLink/DirectLinkCommon.h"
#include "DirectLink/Misc.h"
#include "DatasmithSceneFactory.h"

#include "HAL/PlatformMath.h"

/* #ue_directlink_design
 *
 * the scene (aka sharedState) should be the owner of existing node.
 * Resolution of existing node should be provided by the scene, not by the DeltaConsumer.
 */


TSharedPtr<DirectLink::ISceneGraphNode> FDatasmithDeltaConsumer::FDatasmithElementPointers::AsSharedPtr(DirectLink::FSceneGraphId NodeId)
{
	if (TSharedPtr<IDatasmithElement>* ElementPtr = ElementsSharedPtrs.Find(NodeId))
	{
		return *ElementPtr;
	}
	return nullptr;
}

void FDatasmithDeltaConsumer::LoadScene(const DirectLink::FSceneIdentifier& SceneId, uint32 ElementCount)
{
	// #ue_directlink_cleanup CurrentScene = SceneState(). Missing that SceneState object
	Scene.Reset();
	Elements.Reset();
	LocalIndex.Reset();
	LocalIndex.Reserve(ElementCount);
	FinalizableElements.Reset();
	FinalizableElements.Reserve(ElementCount);
	ChangeLog = FChangeLog();
	ChangeLog.NewSceneId = SceneId;

	// #ue_directlink_feature load from disk
}

// new scene message is stupid. Should be a OpenDelta, with scene info.
void FDatasmithDeltaConsumer::OnOpenDelta(FOpenDeltaArg& OpenDeltaArg)
{
	// while the delta is being processed, Scene is not usable.
	Scene = nullptr;

	// detect new scene data
	ChangeLog.OldSceneId = CurrentSceneId;
	ChangeLog.NewSceneId = OpenDeltaArg.SceneId;
	bool bIsNewScene = OpenDeltaArg.SceneId.SceneGuid != CurrentSceneId.SceneGuid;
	CurrentSceneId = OpenDeltaArg.SceneId;

	if (bIsNewScene)
	{
		LoadScene(CurrentSceneId, OpenDeltaArg.ElementCountHint);

		// Ack new scene
		// start index description (send Have messages)
		for (const auto& Pair : LocalIndex)
		{
			// #ue_directlink_sync communication concept: ordered and reliable message set
			// start have-list communication
			DeltaProducer->HaveScene(CurrentSceneId);
			DeltaProducer->HaveElement(Pair.Key, Pair.Value);
			// end have-list communication
		}
	}
}

void FDatasmithDeltaConsumer::OnSetElement(FSetElementArg& SetElementArg)
{
	DirectLink::FSceneGraphId NodeId = SetElementArg.Snapshot.NodeId;
	DirectLink::FParameterStoreSnapshot& DataSnapshot = SetElementArg.Snapshot.DataSnapshot;

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

	TSharedPtr<IDatasmithElement> Element;
	TSharedPtr<DirectLink::ISceneGraphNode> ExistingElement = Elements.AsSharedPtr(NodeId);
	// check if we have that element and validate type
	if (ExistingElement)
	{
		bool bExistingIsUsable = true;
		uint64 ExistingType = 0;
		bExistingIsUsable &= ExistingElement->GetStore().GetValueAs("Type", ExistingType);
		bExistingIsUsable &= ExistingType == Type;
		if (ensure(bExistingIsUsable))
		{
			// cool, can update
			Element = StaticCastSharedPtr<IDatasmithElement>(ExistingElement);
		}
		else
		{
			// delete from all lists
			FinalizableElements.Remove(NodeId);
			LocalIndex.Remove(NodeId);
			Elements.Remove(NodeId);
		}
	}

	// Create new element
	if (!Element)
	{
		// derived types have several bits sets.
		// -> keep the leftmost bit, which is the value of the (most-derived) class understood by CreateElement
		// eg. this transforms 'Actor|StaticMeshActor' into 'StaticMeshActor'
		// Well, of course it's not exact...
		// Type &= (uint64)~EDatasmithElementType::BaseMaterial; // remove that flag as it always has a child anyway, and its order is impractical.
		EDatasmithElementType PureType = EDatasmithElementType(uint64(1) << FPlatformMath::FloorLog2_64(Type));

		Element = FDatasmithSceneFactory::CreateElement(PureType, *Name);
		check(Element);
		Element->SetNodeId(NodeId); // #ue_directlink_design nope, only the Scene SharedState has this right
		Elements.Add(NodeId, Element);

		const TCHAR* ElementTypeName = GetElementTypeName(Element.Get());
		UE_LOG(LogDatasmith, Display, TEXT("OnAddElement -> %s'%s' id=%d"), ElementTypeName, *Name, NodeId);
	}
	check(Element);

	// Set data values
	// #ue_directlink_quality set_data without a set_reference could have unwanted consequences: should we do both on the 2nd pass ?
	Element->GetStore().Update(DataSnapshot); // #ue_directlink_quality edit of existing elements breaks the current scene. Should be done on the delta application pass
	if (DeltaProducer)
	{
		DirectLink::FElementHash& Hash = LocalIndex.FindOrAdd(NodeId, DirectLink::InvalidHash);
		DirectLink::FElementHash NewHash = SetElementArg.Snapshot.GetHash();

		if (ChangeLog.OldSceneId.SceneGuid == ChangeLog.NewSceneId.SceneGuid)
		{
			FElementEdit e;
			e.Id = NodeId;
			e.OldHash = Hash;
			e.NewHash = NewHash;
			ChangeLog.ModifiedElements.Add(e);
		}

		Hash = NewHash;
		DeltaProducer->HaveElement(NodeId, NewHash);
	}

	// SetReferences
	if (SetElementArg.Snapshot.RefSnapshot.Groups.Num() != 0)
	{
		FFinalizableNode FinalizableNode{Element, MoveTemp(SetElementArg.Snapshot.RefSnapshot)};
		// While references are expressed by pointers, we can't set references on Nodes until referenced nodes are created.
		// So in the meantime, we store the references for a fix up in a 2nd pass (@OnCloseDelta)
		FinalizableElements.Emplace(NodeId, MoveTemp(FinalizableNode));
	}
}

void FDatasmithDeltaConsumer::OnCloseDelta(FCloseDeltaArg& CloseDeltaArg)
{
	// validate references
	TSet<DirectLink::FSceneGraphId> Have;

	Algo::Transform(Elements.ElementsSharedPtrs, Have, [](const auto& Pair){return Pair.Value->GetNodeId();});

	TMap<DirectLink::FSceneGraphId, FFinalizableNode> UnresolvedElements;
	for (auto& Pair : FinalizableElements)
	{
		TSet<DirectLink::FSceneGraphId> Referenced;
		for (const DirectLink::FReferenceSnapshot::FReferenceGroup& Group : Pair.Value.RefSnapshot.Groups)
		{
			Referenced.Append(Group.ReferencedIds);
		}

		if (Have.Includes(Referenced))
		{
			Pair.Value.Element->UpdateRefs(Elements, Pair.Value.RefSnapshot);
		}
		else
		{
			TSet<DirectLink::FSceneGraphId> UnresolvedReferences = Referenced.Difference(Have);
			UE_LOG(LogDatasmith, Warning, TEXT("OnCloseDelta: node [%d] has unresolved references:"), Pair.Key);
			for (DirectLink::FSceneGraphId Id : UnresolvedReferences)
			{
				UE_LOG(LogDatasmith, Display, TEXT("\t%d"), Id);
			}
			UnresolvedElements.Add(Pair.Key, MoveTemp(Pair.Value));
		}
	}

	// accept that delta
	if (UnresolvedElements.Num() == 0)
	{
		// detect graph root
		for (const auto& Pair : Elements.ElementsSharedPtrs)
		{
			if (Pair.Value->IsA(EDatasmithElementType::Scene))
			{
				Scene = StaticCastSharedPtr<IDatasmithScene>(Pair.Value);
// 				if (Scene != UnstableScene)
// 				{
// 				}
				break;
			}
		}

		// notify listeners
		if (ChangeListener)
		{
			ChangeListener->OnOpenDelta();
			if (ChangeLog.OldSceneId.SceneGuid != ChangeLog.NewSceneId.SceneGuid)
			{
				ChangeListener->OnNewScene();
				for (const auto& Pair : Elements.ElementsSharedPtrs)
				{
					ChangeListener->OnAddElement(Pair.Value);
				}
			}
			else
			{
				for (const auto& Item : ChangeLog.ModifiedElements)
				{
					if (Item.OldHash == DirectLink::InvalidHash)
					{
						ChangeListener->OnAddElement(StaticCastSharedPtr<IDatasmithElement>(Elements.AsSharedPtr(Item.Id)));
					}
				}

				for (const auto& Item : ChangeLog.ModifiedElements)
				{
					if (Item.OldHash != DirectLink::InvalidHash && Item.OldHash != Item.NewHash)
					{
						ChangeListener->OnChangedElement(StaticCastSharedPtr<IDatasmithElement>(Elements.AsSharedPtr(Item.Id)));
					}
				}

				for (const auto& Item : ChangeLog.ModifiedElements)
				{
					if (Item.NewHash == DirectLink::InvalidHash)
					{
						ChangeListener->OnRemovedElement(Item.Id);
					}
				}

			}
			ChangeListener->OnCloseDelta();
		}
		ChangeLog = FChangeLog();
	} // #ue_directlink_quality else ?

	Swap(FinalizableElements, UnresolvedElements);

}

