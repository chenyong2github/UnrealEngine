// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DeltaConsumer.h"
#include "DirectLink/ElementSnapshot.h"

#include "DatasmithCore.h"
#include "IDatasmithSceneElements.h"

#include "CoreTypes.h"
#include "Templates/UniquePtr.h"

namespace DirectLink
{
class IDeltaProducer;
} // namespace DirectLink


class DATASMITHCORE_API FDatasmithDeltaConsumer
	: public DirectLink::IDeltaConsumer
{
public:
	class ISceneChangeListener
	{
	public:
		virtual void OnOpenDelta() = 0;
		virtual void OnNewScene() = 0;
		virtual void OnAddElement(TSharedPtr<IDatasmithElement> Element) = 0;
		virtual void OnChangedElement(TSharedPtr<IDatasmithElement> Element) = 0;
		virtual void OnRemovedElement(DirectLink::FSceneGraphId ElementId) = 0;
		virtual void OnCloseDelta() = 0;
	};

public:
	void SetChangeListener(ISceneChangeListener* Listener) { ChangeListener = Listener; }

	TSharedPtr<IDatasmithScene> GetScene() { return Scene; }

private:
	// IDeltaConsumer API
	virtual void SetDeltaProducer(DirectLink::IDeltaProducer* Producer) override
	{
		DeltaProducer = Producer;
	}

	virtual void OnOpenDelta(FOpenDeltaArg& OpenDeltaArg) override;

	virtual void OnSetElement(FSetElementArg& SetElementArg) override;

	virtual void OnCloseDelta(FCloseDeltaArg& CloseDeltaArg) override;

private:
	void LoadScene(const DirectLink::FSceneIdentifier& SceneId, uint32 ElementCount);

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
	TMap<DirectLink::FSceneGraphId, FFinalizableNode> FinalizableElements;
	TMap<DirectLink::FSceneGraphId, DirectLink::FElementHash> LocalIndex;
	FDatasmithElementPointers Elements;

	struct FElementEdit{
		DirectLink::FSceneGraphId Id;
		DirectLink::FElementHash OldHash;
		DirectLink::FElementHash NewHash;
	};
	struct FChangeLog
	{
		DirectLink::FSceneIdentifier OldSceneId;
		DirectLink::FSceneIdentifier NewSceneId;
		TArray<FElementEdit> ModifiedElements;
	};
	FChangeLog ChangeLog;
	ISceneChangeListener* ChangeListener = nullptr;

	DirectLink::FSceneIdentifier CurrentSceneId;
	TSharedPtr<IDatasmithScene> Scene;
	TSharedPtr<IDatasmithScene> UnstableScene;

	DirectLink::IDeltaProducer* DeltaProducer = nullptr;
};
