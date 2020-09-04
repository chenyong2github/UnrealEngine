// Copyright Epic Games, Inc. All Rights Reserved.

#include "Models/NavigationSimulationNode.h"

#include "Layout/WidgetPath.h"
#include "Models/WidgetReflectorNode.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "NavigationSimulationNode"

/**
 * -----------------------------------------------------------------------------
 * FNavigationSimulationWidgetInfo
 * -----------------------------------------------------------------------------
 */
FNavigationSimulationWidgetInfo::FNavigationSimulationWidgetInfo(const TSharedPtr<const SWidget>& Widget)
	: WidgetPtr(FWidgetReflectorNodeUtils::GetWidgetAddress(Widget))
	, WidgetLive(Widget)
	, WidgetTypeAndShortName(FWidgetReflectorNodeUtils::GetWidgetTypeAndShortName(Widget))
	, bHasGeometry(false)
{

}

FNavigationSimulationWidgetInfo::FNavigationSimulationWidgetInfo(const FWidgetPath& WidgetPath)
	: WidgetPtr(0)
	, bHasGeometry(WidgetPath.IsValid())
{
	if (bHasGeometry)
	{
		WidgetLive = WidgetPath.GetLastWidget();
		WidgetPtr = FWidgetReflectorNodeUtils::GetWidgetAddress(WidgetPath.GetLastWidget());
		WidgetTypeAndShortName = FWidgetReflectorNodeUtils::GetWidgetTypeAndShortName(WidgetPath.GetLastWidget());
		WidgetGeometry = WidgetPath.Widgets.Last().Geometry;
	}
}

FNavigationSimulationWidgetNodeItem::FNavigationSimulationWidgetNodeItem(const FSlateNavigationEventSimulator::FSimulationResult& Result)
	: NavigationType(Result.NavigationType)
	, Destination(Result.NavigationDestination)
	, ReplyEventHandler(Result.NavigationReply.EventHandler)
	, ReplyFocusRecipient(Result.NavigationReply.FocusRecipient)
	, ReplyBoundaryRule(Result.NavigationReply.BoundaryRule)
	, RoutedReason(Result.RoutedReason)
	, WidgetThatShouldReceivedFocus(Result.WidgetThatShouldReceivedFocus)
	, FocusedWidget(Result.FocusedWidgetPath)
	, bIsDynamic(Result.bIsDynamic)
	, bAlwaysHandleNavigationAttempt(Result.bAlwaysHandleNavigationAttempt)
	, bCanFindWidgetForSetFocus(Result.bCanFindWidgetForSetFocus)
	, bRoutedHandlerHasNavigationMeta(Result.bRoutedHandlerHasNavigationMeta)
	, bHandledByViewport(Result.bHandledByViewport)
{

}

void FNavigationSimulationWidgetNodeItem::ResetLiveWidget()
{
	Destination.WidgetLive.Reset();
	ReplyEventHandler.WidgetLive.Reset();
	ReplyFocusRecipient.WidgetLive.Reset();
	WidgetThatShouldReceivedFocus.WidgetLive.Reset();
	FocusedWidget.WidgetLive.Reset();
}

FNavigationSimulationWidgetNode::FNavigationSimulationWidgetNode(ENavigationSimulationNodeType NodeType, const FWidgetPath& InNavigationSource)
	: NavigationSource(InNavigationSource)
	, NodeType(NodeType)
{
	if (NodeType == ENavigationSimulationNodeType::Snapshot)
	{
		NavigationSource.WidgetLive.Reset();
	}
}

namespace NavigationSimulationNodeUtilsInternal
{
	TArray<FNavigationSimulationWidgetNodePtr> BuildNavigationSimulationNodeList(ENavigationSimulationNodeType NodeType, const TArray<FSlateNavigationEventSimulator::FSimulationResult>& SimulationResult)
	{
		TMap<const SWidget*, FNavigationSimulationWidgetNodePtr> WidgetMap;
		for (const FSlateNavigationEventSimulator::FSimulationResult& Element : SimulationResult)
		{
			if (Element.IsValid())
			{
				const SWidget* ElementWidget = &Element.NavigationSource.GetLastWidget().Get();
				if (!WidgetMap.Contains(ElementWidget))
				{
					WidgetMap.Add(ElementWidget, MakeShared<FNavigationSimulationWidgetNode>(NodeType, Element.NavigationSource));
				}

				FNavigationSimulationWidgetNodeItem& NodeItem = WidgetMap[ElementWidget]->Simulations.Emplace_GetRef(Element);
				if (NodeType == ENavigationSimulationNodeType::Snapshot)
				{
					NodeItem.ResetLiveWidget();
				}
			}
		}

		TArray<FNavigationSimulationWidgetNodePtr> GeneratedArray;
		WidgetMap.GenerateValueArray(GeneratedArray);

		return MoveTemp(GeneratedArray);
	}
}

TArray<FNavigationSimulationWidgetNodePtr> FNavigationSimulationNodeUtils::BuildNavigationSimulationNodeListForLive(const TArray<FSlateNavigationEventSimulator::FSimulationResult>& SimulationResult)
{
	return NavigationSimulationNodeUtilsInternal::BuildNavigationSimulationNodeList(ENavigationSimulationNodeType::Live, SimulationResult);
}

TArray<FNavigationSimulationWidgetNodePtr> FNavigationSimulationNodeUtils::BuildNavigationSimulationNodeListForSnapshot(const TArray<FSlateNavigationEventSimulator::FSimulationResult>& SimulationResult)
{
	return NavigationSimulationNodeUtilsInternal::BuildNavigationSimulationNodeList(ENavigationSimulationNodeType::Snapshot, SimulationResult);
}

TArray<FNavigationSimulationWidgetNodePtr> FNavigationSimulationNodeUtils::FindReflectorNodes(const TArray<FNavigationSimulationWidgetNodePtr>& Nodes, const TArray<TSharedRef<FWidgetReflectorNodeBase>>& ToFind)
{
	TArray<FNavigationSimulationWidgetNodePtr> Result;
	Result.Reserve(ToFind.Num());
	for (const TSharedRef<FWidgetReflectorNodeBase>& ReflectorNode : ToFind)
	{
		int32 FoundIndex = IndexOfSnapshotWidget(Nodes, ReflectorNode->GetWidgetAddress());
		if (Nodes.IsValidIndex(FoundIndex))
		{
			Result.Add(Nodes[FoundIndex]);
		}
	}
	return Result;
}

int32 FNavigationSimulationNodeUtils::IndexOfLiveWidget(const TArray<FNavigationSimulationWidgetNodePtr>& Nodes, const TSharedPtr<const SWidget>& WidgetToFind)
{
	return Nodes.IndexOfByPredicate([WidgetToFind](const FNavigationSimulationWidgetNodePtr& Node) { return Node->NavigationSource.WidgetLive == WidgetToFind; });
}

int32 FNavigationSimulationNodeUtils::IndexOfSnapshotWidget(const TArray<FNavigationSimulationWidgetNodePtr>& Nodes, FNavigationSimulationWidgetInfo::TPointerAsInt WidgetToFind)
{
	return Nodes.IndexOfByPredicate([WidgetToFind](const FNavigationSimulationWidgetNodePtr& Node) { return Node->NavigationSource.WidgetPtr == WidgetToFind; });
}

#undef LOCTEXT_NAMESPACE
