// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/SceneIndexBuilder.h"

#include "DirectLink/DirectLinkLog.h"
#include "DirectLink/SceneGraphNode.h"
#include "IDatasmithSceneElements.h"
#include "Misc.h"


namespace DirectLink
{

void FSceneIndexBuilder::InitFromRootElement(ISceneGraphNode* RootElement)
{
	Index = FLocalSceneIndex();
	if (RootElement)
	{
		if (!RootElement->GetSharedState().IsValid())
		{
			RootElement->SetSharedState(RootElement->MakeSharedState());
			if (!RootElement->GetSharedState().IsValid())
			{
				return;
			}
		}
		Index = FLocalSceneIndex(RootElement->GetSharedState()->GetSceneId());
		AddElement(RootElement);
	}
}


void FSceneIndexBuilder::AddElement(ISceneGraphNode* Element, int32 RecLevel)
{
	if (Element == nullptr)
	{
		UE_LOG(LogDirectLinkIndexer, Warning, TEXT("null element"));
		return;
	}

	// #ue_directlink_cleanup debug print
	{
		const IDatasmithElement* DSElement = static_cast<const IDatasmithElement*>(Element);
		const TCHAR* Type = GetElementTypeName(DSElement);
		const TCHAR* Name = DSElement->GetName();
		const TCHAR* Label = DSElement->GetLabel();
		UE_LOG(LogDirectLinkIndexer, Verbose, TEXT("%*sVisit %s: %s '%s'"), RecLevel * 4, TEXT(""), Type, Name, Label);
	}

	bool bAdded = Index.AddReference(Element);
	if (!bAdded)
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
				AddElement(Referenced, RecLevel + 1);
			}
		}
	}
}


FLocalSceneIndex BuildIndexForScene(ISceneGraphNode* RootElement)
{
	check(RootElement);
	FSceneIndexBuilder IndexBuilder;
	IndexBuilder.InitFromRootElement(RootElement);

	FLocalSceneIndex Index = MoveTemp(IndexBuilder.GetIndex());
	return Index;
}


} // namespace DirectLink
