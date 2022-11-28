// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphNode.h"

#include "PCGEditorGraphNodeBase.h"
#include "PCGEditorStyle.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"

#include "SGraphPin.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

void SPCGEditorGraphNode::Construct(const FArguments& InArgs, UPCGEditorGraphNodeBase* InNode)
{
	GraphNode = InNode;
	PCGEditorGraphNode = InNode;

	if (InNode)
	{
		InNode->OnNodeChangedDelegate.BindSP(this, &SPCGEditorGraphNode::OnNodeChanged);
	}

	UpdateGraphNode();
}

const FSlateBrush* SPCGEditorGraphNode::GetNodeBodyBrush() const
{
	if (PCGEditorGraphNode && PCGEditorGraphNode->GetPCGNode() && PCGEditorGraphNode->GetPCGNode()->IsInstance())
	{
		return FAppStyle::GetBrush("Graph.Node.TintedBody");
	}
	else
	{
		return FAppStyle::GetBrush("Graph.Node.Body");
	}
}

TSharedRef<SWidget> SPCGEditorGraphNode::CreateTitleWidget(TSharedPtr<SNodeTitle> InNodeTitle)
{
	// Reimplementation of the SGraphNode::CreateTitleWidget so we can control the style
	const bool bIsInstanceNode = (PCGEditorGraphNode && PCGEditorGraphNode->GetPCGNode() && PCGEditorGraphNode->GetPCGNode()->IsInstance());

	SAssignNew(InlineEditableText, SInlineEditableTextBlock)
		.Style(FPCGEditorStyle::Get(), bIsInstanceNode ? "PCG.Node.InstancedNodeTitleInlineEditableText" : "PCG.Node.NodeTitleInlineEditableText")
		.Text(InNodeTitle.Get(), &SNodeTitle::GetHeadTitle)
		.OnVerifyTextChanged(this, &SPCGEditorGraphNode::OnVerifyNameTextChanged)
		.OnTextCommitted(this, &SPCGEditorGraphNode::OnNameTextCommited)
		.IsReadOnly(this, &SPCGEditorGraphNode::IsNameReadOnly)
		.IsSelected(this, &SPCGEditorGraphNode::IsSelectedExclusively);
	InlineEditableText->SetColorAndOpacity(TAttribute<FLinearColor>::Create(TAttribute<FLinearColor>::FGetter::CreateSP(this, &SPCGEditorGraphNode::GetNodeTitleTextColor)));

	return InlineEditableText.ToSharedRef();
}

void SPCGEditorGraphNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	check(PCGEditorGraphNode);
	UPCGNode* PCGNode = PCGEditorGraphNode->GetPCGNode();
	// Implementation note: we do not distinguish single/multiple pins on the output since that is not relevant
	if (PCGNode && PinToAdd->GetPinObj())
	{
		if (UPCGPin* Pin = PCGNode->GetInputPin(PinToAdd->GetPinObj()->PinName))
		{
			if (Pin->Properties.bAllowMultipleConnections)
			{
				PinToAdd->SetCustomPinIcon(FAppStyle::GetBrush("Graph.ArrayPin.Connected"), FAppStyle::GetBrush("Graph.ArrayPin.Disconnected"));
			}
		}
	}

	SGraphNode::AddPin(PinToAdd);
}

void SPCGEditorGraphNode::GetOverlayBrushes(bool bSelected, const FVector2D WidgetSize, TArray<FOverlayBrushInfo>& Brushes) const
{
	check(PCGEditorGraphNode);
	
	const FSlateBrush* DebugBrush = FPCGEditorStyle::Get().GetBrush(TEXT("PCG.NodeOverlay.Debug"));
	const FSlateBrush* InspectBrush = FPCGEditorStyle::Get().GetBrush(TEXT("PCG.NodeOverlay.Inspect"));
	
	const FVector2D HalfDebugBrushSize = DebugBrush->GetImageSize() / 2.0;
	const FVector2D HalfInspectBrushSize = InspectBrush->GetImageSize() / 2.0;
	
	FVector2D OverlayOffset(0.0, 0.0);

	if (const UPCGNode* PCGNode = PCGEditorGraphNode->GetPCGNode())
	{
		if (PCGNode->GetSettingsInterface() && PCGNode->GetSettingsInterface()->bDebug)
		{
			FOverlayBrushInfo BrushInfo;
			BrushInfo.Brush = DebugBrush;
			BrushInfo.OverlayOffset = OverlayOffset - HalfDebugBrushSize;
			Brushes.Add(BrushInfo);
			
			OverlayOffset.Y += HalfDebugBrushSize.Y + HalfInspectBrushSize.Y;
		}
	}

	if (PCGEditorGraphNode->GetInspected())
	{
		FOverlayBrushInfo BrushInfo;
		BrushInfo.Brush = InspectBrush;
		BrushInfo.OverlayOffset = OverlayOffset - HalfInspectBrushSize;
		Brushes.Add(BrushInfo);	
	}
}

void SPCGEditorGraphNode::OnNodeChanged()
{
	UpdateGraphNode();
}
