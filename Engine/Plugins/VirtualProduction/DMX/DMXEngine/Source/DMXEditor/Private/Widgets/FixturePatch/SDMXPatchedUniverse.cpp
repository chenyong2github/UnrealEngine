// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXPatchedUniverse.h"

#include "DMXEditor.h"
#include "DMXFixturePatchNode.h"
#include "DMXFixturePatchEditorDefinitions.h"
#include "SDMXChannelConnector.h"
#include "SDMXFixturePatchFragment.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXPortManager.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScaleBox.h"


#define LOCTEXT_NAMESPACE "SDMXPatchedUniverse"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SDMXPatchedUniverse::Construct(const FArguments& InArgs)
{
	check(InArgs._UniverseID != INDEX_NONE);

	UniverseID = InArgs._UniverseID;

	DMXEditorPtr = InArgs._DMXEditor;
	OnDragEnterChannel = InArgs._OnDragEnterChannel;
	OnDragLeaveChannel = InArgs._OnDragLeaveChannel;
	OnDropOntoChannel = InArgs._OnDropOntoChannel;

	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		ChildSlot
		[
			SNew (SVerticalBox)

			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.AutoHeight()
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(UniverseName, SBorder)
					.VAlign(VAlign_Top)
					.HAlign(HAlign_Fill)
					.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 0.3f))
					.ToolTipText(FText::Format(LOCTEXT("UniverseListCategoryTooltip", "Patches assigned to Universe {0}"), UniverseID))
					[
						SNew(STextBlock)
						.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
						.TextStyle(FEditorStyle::Get(), "DetailsView.CategoryTextStyle")
						.Text(this, &SDMXPatchedUniverse::GetHeaderText)
					]
				]

				+ SOverlay::Slot()
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("Graph.Node.DevelopmentBanner"))
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					.Visibility(this, &SDMXPatchedUniverse::GetPatchedUniverseReachabilityBannerVisibility)
				]
			]
			
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Fill)		
			[					
				SNew(SScaleBox)	
				.VAlign(VAlign_Top)
				.Stretch(EStretch::ScaleToFitX)						
				.StretchDirection(EStretchDirection::DownOnly)				
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.MinDesiredWidth(920.0f) // Avoids a slate issue, where SScaleBox ignores border size hence scales			
					.Padding(FMargin(4.0f, 4.0f, 4.0f, 8.0f))
					[
						SAssignNew(Grid, SGridPanel)												
					]
				]
			]
		];

		CreateChannelConnectors();

		SetUniverseID(UniverseID);

		UpdatePatchedUniverseReachability();

		// Bind to port changes
		FDMXPortManager::Get().OnPortsChanged.AddSP(this, &SDMXPatchedUniverse::UpdatePatchedUniverseReachability);
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SDMXPatchedUniverse::SetUniverseID(int32 NewUniverseID)
{
	check(NewUniverseID > -1);

	// Find patches in new universe
	UDMXLibrary* Library = GetDMXLibrary();
	if (Library)
	{
		// Unpatch all nodes
		TArray<TSharedPtr<FDMXFixturePatchNode>> CachedPatchedNodes = PatchedNodes;
		for (const TSharedPtr<FDMXFixturePatchNode>& Node : CachedPatchedNodes)
		{
			Unpatch(Node);
		}
		check(PatchedNodes.Num() == 0);

		// Update what to draw
		UniverseID = NewUniverseID;

		UpdatePatchedUniverseReachability();

		TArray<UDMXEntityFixturePatch*> PatchesInUniverse;
		Library->ForEachEntityOfType<UDMXEntityFixturePatch>([&](UDMXEntityFixturePatch* Patch) 
		{
			if (Patch->GetUniverseID() == UniverseID)
			{
				PatchesInUniverse.Add(Patch);
			}
		});

		// Add fixture patches to the grid
		for (UDMXEntityFixturePatch* FixturePatch : PatchesInUniverse)
		{
			if (CanAssignFixturePatch(FixturePatch))
			{
				TSharedPtr<FDMXFixturePatchNode> Node = FindPatchNode(FixturePatch);

				if (!Node.IsValid())
				{
					Node = FDMXFixturePatchNode::Create(DMXEditorPtr, FixturePatch);
				}
				check(Node.IsValid());

				Patch(Node, FixturePatch->GetStartingChannel(), false);
			}
		}

		// Update the channel connectors' Universe ID
		for (const TSharedPtr<SDMXChannelConnector>& Connector : ChannelConnectors)
		{
			Connector->SetUniverseID(NewUniverseID);
		}
	}
}

void SDMXPatchedUniverse::SetShowUniverseName(bool bShow)
{
	if (bShow)
	{
		UniverseName->SetVisibility(EVisibility::Visible);
	}
	else
	{
		UniverseName->SetVisibility(EVisibility::Collapsed);
	}
}

void SDMXPatchedUniverse::CreateChannelConnectors()
{
	check(Grid.IsValid());

	for (int32 ChannelIndex = 0; ChannelIndex < DMX_UNIVERSE_SIZE; ++ChannelIndex)
	{
		int32 Column = ChannelIndex % FDMXChannelGridSpecs::NumColumns;
		int32 Row = ChannelIndex / FDMXChannelGridSpecs::NumColumns;
		int32 ChannelID = ChannelIndex + FDMXChannelGridSpecs::ChannelIDOffset;

		TSharedRef<SDMXChannelConnector> ChannelPatchWidget =
			SNew(SDMXChannelConnector)
			.ChannelID(ChannelID)
			.Value(0)
			.OnDragEnterChannel(this, &SDMXPatchedUniverse::HandleDragEnterChannel)
			.OnDragLeaveChannel(this, &SDMXPatchedUniverse::HandleDragLeaveChannel)
			.OnDropOntoChannel(this, &SDMXPatchedUniverse::HandleDropOntoChannel)
			.DMXEditor(DMXEditorPtr);

		ChannelConnectors.Add(ChannelPatchWidget);

		Grid->AddSlot(Column, Row)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[			
				ChannelPatchWidget
			];
	}
}

bool SDMXPatchedUniverse::Patch(const TSharedPtr<FDMXFixturePatchNode>& Node, int32 NewStartingChannel, bool bCreateTransaction)
{
	check(Node.IsValid());

	if (!CanAssignNode(Node, NewStartingChannel))
	{
		return false;
	}

	UDMXEntityFixturePatch* FixturePatch = Node->GetFixturePatch().Get();
	if (!FixturePatch)
	{
		return false;
	}

	const int32 NewChannelSpan = FixturePatch->GetChannelSpan();

	// Auto assign patches that have bAutoAssignAddress set
	if (FixturePatch->IsAutoAssignAddress())
	{
		NewStartingChannel = FixturePatch->GetStartingChannel();
	}

	// Only fully valid channels are supported
	check(NewStartingChannel > 0);
	check(NewStartingChannel + NewChannelSpan - 1 <= DMX_UNIVERSE_SIZE);

	// Unpatch from the old universe
	TSharedPtr<SDMXPatchedUniverse> OldUniverse = Node->GetUniverse();
	if (OldUniverse.IsValid())
	{
		OldUniverse->Unpatch(Node);
	}

	// Assign the node to this universe
	check(!PatchedNodes.Contains(Node));
	PatchedNodes.Add(Node);

	// Update the node and commit to the changes
	Node->Update(SharedThis(this), NewStartingChannel, NewChannelSpan);
	Node->CommitPatch(bCreateTransaction);

	// Assign the node's widgets to the grid
	AddNodeToGrid(Node);

	return true;
}

void SDMXPatchedUniverse::Unpatch(const TSharedPtr<FDMXFixturePatchNode>& Node)
{
	check(Node.IsValid());
	check(Grid.IsValid());

	if (PatchedNodes.Contains(Node))
	{
		PatchedNodes.RemoveSingle(Node);
		RemoveNodeFromGrid(Node);
	}
}

void SDMXPatchedUniverse::AddNodeToGrid(const TSharedPtr<FDMXFixturePatchNode>& Node)
{
	check(Grid.IsValid());
	for (const TSharedPtr<SDMXFixturePatchFragment>& Fragment : Node->GetFragmentedWidgets())
	{
		check(Fragment.IsValid());

		Grid->AddSlot(Fragment->GetColumn(), Fragment->GetRow())
			.ColumnSpan(Fragment->GetColumnSpan())
			.Layer(1)
			[
				Fragment.ToSharedRef()
			];
	}
}

void SDMXPatchedUniverse::RemoveNodeFromGrid(const TSharedPtr<FDMXFixturePatchNode>& Node)
{
	check(Node.IsValid());
	for (const TSharedPtr<SDMXFixturePatchFragment>& Widget : Node->GetFragmentedWidgets())
	{
		Grid->RemoveSlot(Widget.ToSharedRef());
	}
}

bool SDMXPatchedUniverse::CanAssignFixturePatch(TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch) const
{
	if (FixturePatch.IsValid())
	{
		return CanAssignFixturePatch(FixturePatch, FixturePatch->GetStartingChannel());
	}

	return false;
}

bool SDMXPatchedUniverse::CanAssignFixturePatch(TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch, int32 StartingChannel) const
{
	// Test for a valid and patch that has an active mode
	FText InvalidReason;
	if (!FixturePatch.IsValid() ||
		!FixturePatch->IsValidEntity(InvalidReason) ||
		!FixturePatch->GetActiveMode())
	{
		return false;
	}

	int32 ChannelSpan = FixturePatch->GetChannelSpan();
	if (ChannelSpan == 0)
	{
		// Cannot patch a patch with 0 channel span
		return false;
	}

	// Only fully valid channels are supported
	check(StartingChannel > 0);
	check(StartingChannel + ChannelSpan - 1 <= DMX_UNIVERSE_SIZE);

	// Test for overlapping nodes in this universe
	for (const TSharedPtr<FDMXFixturePatchNode>& Node : PatchedNodes)
	{
		if (Node->GetFixturePatch() == FixturePatch)
		{
			continue;
		}

		if (Node->OccupiesChannels(StartingChannel, ChannelSpan))
		{
			return false;
		}
	}

	return true;
}

bool SDMXPatchedUniverse::CanAssignNode(const TSharedPtr<FDMXFixturePatchNode>& TestedNode, int32 StartingChannel) const
{
	check(TestedNode.IsValid());

	UDMXEntityFixturePatch* FixturePatch = TestedNode->GetFixturePatch().Get();

	return CanAssignFixturePatch(FixturePatch, StartingChannel);
}

TSharedPtr<FDMXFixturePatchNode> SDMXPatchedUniverse::FindPatchNode(const TWeakObjectPtr<UDMXEntityFixturePatch>& FixturePatch) const
{
	for (const TSharedPtr<FDMXFixturePatchNode>& Node : PatchedNodes)
	{
		if (Node->GetFixturePatch() == FixturePatch)
		{
			return Node;
		}
	}
	return nullptr;
}

TSharedPtr<FDMXFixturePatchNode> SDMXPatchedUniverse::FindPatchNodeOfType(UDMXEntityFixtureType* Type, const TSharedPtr<FDMXFixturePatchNode>& IgoredNode) const
{	
	if (!Type)
	{
		return nullptr;
	}

	for (const TSharedPtr<FDMXFixturePatchNode>& PatchNode : PatchedNodes)
	{
		if (PatchNode == IgoredNode)
		{
			continue;
		}

		if (PatchNode->GetFixturePatch().IsValid() &&
			PatchNode->GetFixturePatch()->GetFixtureType() == Type)
		{
			return PatchNode;
		}
	}

	return nullptr;
}

void SDMXPatchedUniverse::HandleDragEnterChannel(int32 ChannelID, const FDragDropEvent& DragDropEvent)
{
	OnDragEnterChannel.ExecuteIfBound(UniverseID, ChannelID, DragDropEvent);
}

void SDMXPatchedUniverse::HandleDragLeaveChannel(int32 ChannelID, const FDragDropEvent& DragDropEvent)
{
	OnDragLeaveChannel.ExecuteIfBound(UniverseID, ChannelID, DragDropEvent);
}

FReply SDMXPatchedUniverse::HandleDropOntoChannel(int32 ChannelID, const FDragDropEvent& DragDropEvent)
{
	return OnDropOntoChannel.Execute(UniverseID, ChannelID, DragDropEvent);
}

FText SDMXPatchedUniverse::GetHeaderText() const
{
	switch (PatchedUniverseReachability)
	{
		case EDMXPatchedUniverseReachability::Reachable:
			return FText::GetEmpty();

		case EDMXPatchedUniverseReachability::UnreachableForInputPorts:
			return FText::Format(LOCTEXT("UnreachableForInputPorts", "Universe {0} - Unreachable by Input Ports"), UniverseID);

		case EDMXPatchedUniverseReachability::UnreachableForOutputPorts:
			return FText::Format(LOCTEXT("UnreachableForOutputPorts", "Universe {0} - Unreachable by Output Ports"), UniverseID);

		case EDMXPatchedUniverseReachability::UnreachableForInputAndOutputPorts:
			return FText::Format(LOCTEXT("UnreachableForInputAndOutputPorts", "Universe {0} - Unreachable by Input and Output Ports"), UniverseID);

		default:
			// Unhandled enum value
			checkNoEntry();
	}

	return FText::GetEmpty();
}

EVisibility SDMXPatchedUniverse::GetPatchedUniverseReachabilityBannerVisibility() const
{
	if (PatchedUniverseReachability == EDMXPatchedUniverseReachability::Reachable)
	{
		return EVisibility::Hidden;
	}

	return EVisibility::Visible;
}

void SDMXPatchedUniverse::UpdatePatchedUniverseReachability()
{
	PatchedUniverseReachability = EDMXPatchedUniverseReachability::Reachable;

	UDMXLibrary* Library = GetDMXLibrary();
	if (Library)
	{
		bool bReachableForAnyInput = false;
		for (const FDMXInputPortSharedRef& InputPort : Library->GetInputPorts())
		{
			if (InputPort->IsLocalUniverseInPortRange(UniverseID))
			{
				bReachableForAnyInput = true;
				break;
			}
		}

		bool bReachableForAnyOutput = false;
		for (const FDMXOutputPortSharedRef& OutputPort : Library->GetOutputPorts())
		{
			if (OutputPort->IsLocalUniverseInPortRange(UniverseID))
			{
				bReachableForAnyOutput = true;
				break;
			}
		}

		if (!bReachableForAnyInput && !bReachableForAnyOutput)
		{
			PatchedUniverseReachability = EDMXPatchedUniverseReachability::UnreachableForInputAndOutputPorts;
		}
		else if(!bReachableForAnyOutput)
		{
			PatchedUniverseReachability = EDMXPatchedUniverseReachability::UnreachableForOutputPorts;
		}
		else if (!bReachableForAnyInput)
		{
			PatchedUniverseReachability = EDMXPatchedUniverseReachability::UnreachableForInputPorts;
		}
	}
}

UDMXLibrary* SDMXPatchedUniverse::GetDMXLibrary() const
{
	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		return DMXEditor->GetDMXLibrary();
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
