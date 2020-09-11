// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DeltaConsumer.h"
#include "DirectLink/ElementSnapshot.h"


class IDatasmithElement;
class IDatasmithScene;


class DATASMITHCORE_API FDatasmithSceneReceiver
	: public DirectLink::ISceneReceiver
{
public:
	class ISceneChangeListener
	{
	public:
		virtual void OnOpenDelta() = 0;
		virtual void OnNewScene(const DirectLink::FSceneIdentifier& SceneId) = 0;
		virtual void OnAddElement(TSharedPtr<IDatasmithElement> Element) = 0;
		virtual void OnChangedElement(TSharedPtr<IDatasmithElement> Element) = 0;
		virtual void OnRemovedElement(DirectLink::FSceneGraphId ElementId) = 0;
		virtual void OnCloseDelta() = 0;
	};

public:
	FDatasmithSceneReceiver();

	void SetChangeListener(ISceneChangeListener* Listener) { ChangeListener = Listener; }
	TSharedPtr<IDatasmithScene> GetScene();

private:
	virtual void FinalSnapshot(const DirectLink::FSceneSnapshot& SceneSnapshot) override;

private:
	struct FDatasmithElementPointers : public DirectLink::IReferenceResolutionProvider
	{
		// IReferenceResolutionProvider API
		virtual TSharedPtr<DirectLink::ISceneGraphNode> AsSharedPtr(DirectLink::FSceneGraphId NodeId) override;
		void Reset() { ElementsSharedPtrs.Reset(); }
		void Remove(DirectLink::FSceneGraphId NodeId) { ElementsSharedPtrs.Remove(NodeId); }
		void Add(DirectLink::FSceneGraphId Id, TSharedPtr<IDatasmithElement> Element) { ElementsSharedPtrs.Add(Id, Element); }
		TMap<DirectLink::FSceneGraphId, TSharedPtr<IDatasmithElement>> ElementsSharedPtrs;
	};

	struct FFinalizableNode
	{
		TSharedPtr<IDatasmithElement> Element;
		DirectLink::FReferenceSnapshot RefSnapshot;
	};
	struct FFinalizableNode2
	{
		TSharedPtr<IDatasmithElement> Element;
		const DirectLink::FElementSnapshot* Snapshot;
	};

	struct FElementEdit{
		DirectLink::FSceneGraphId Id;
		DirectLink::FElementHash OldHash;
		DirectLink::FElementHash NewHash;
	};
	struct FChangeLog
	{
		DirectLink::FSceneIdentifier OldSceneId;
		DirectLink::FSceneIdentifier NewSceneId;
		int32 SyncCycle;
		bool bForceNewScene;
	};

	FChangeLog ChangeLog;
	ISceneChangeListener* ChangeListener = nullptr;

	struct FSceneState
	{
		DirectLink::FSceneIdentifier SceneId;
		TSharedPtr<IDatasmithScene> Scene;
		FDatasmithElementPointers Elements;
	};

	TUniquePtr<FSceneState> Current;
};


