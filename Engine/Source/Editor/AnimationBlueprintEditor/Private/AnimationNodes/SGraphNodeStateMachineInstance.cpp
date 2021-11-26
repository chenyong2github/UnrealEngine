// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationNodes/SGraphNodeStateMachineInstance.h"
#include "AnimationStateMachineGraph.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "Engine/PoseWatch.h"
#include "AnimationEditorUtils.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "SPoseWatchOverlay.h"

#define LOCTEXT_NAMESPACE "SGraphNodeStateMachineInstance"

/////////////////////////////////////////////////////
// SGraphNodeStateMachineInstance

void SGraphNodeStateMachineInstance::Construct(const FArguments& InArgs, UAnimGraphNode_StateMachineBase* InNode)
{
	GraphNode = InNode;

	SetCursor(EMouseCursor::CardinalCross);

	PoseWatchWidget = SNew(SPoseWatchOverlay, InNode);

	UpdateGraphNode();
}

UEdGraph* SGraphNodeStateMachineInstance::GetInnerGraph() const
{
	UAnimGraphNode_StateMachineBase* StateMachineInstance = CastChecked<UAnimGraphNode_StateMachineBase>(GraphNode);

	return StateMachineInstance->EditorStateMachineGraph;
}

TArray<FOverlayWidgetInfo> SGraphNodeStateMachineInstance::GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const
{
	TArray<FOverlayWidgetInfo> Widgets;

	if (UAnimGraphNode_Base* AnimNode = CastChecked<UAnimGraphNode_Base>(GraphNode, ECastCheckedType::NullAllowed))
	{
		if (PoseWatchWidget->IsPoseWatchValid())
		{
			FOverlayWidgetInfo Info;
			Info.OverlayOffset = PoseWatchWidget->GetOverlayOffset();
			Info.Widget = PoseWatchWidget;
			Widgets.Add(Info);
		}
	}

	return Widgets;
}

#undef LOCTEXT_NAMESPACE