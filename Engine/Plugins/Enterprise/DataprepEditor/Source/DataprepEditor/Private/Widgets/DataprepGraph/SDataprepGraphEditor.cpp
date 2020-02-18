// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/DataprepGraph/SDataprepGraphEditor.h"

#include "DataprepAsset.h"
#include "DataprepEditorLogCategory.h"
#include "DataprepEditorStyle.h"
#include "DataprepGraph/DataprepGraph.h"
#include "DataprepGraph/DataprepGraphActionNode.h"
#include "SchemaActions/DataprepDragDropOp.h"
#include "Widgets/DataprepGraph/SDataprepGraphActionNode.h"
#include "Widgets/DataprepGraph/SDataprepGraphActionStepNode.h"
#include "Widgets/DataprepGraph/SDataprepGraphTrackNode.h"
#include "Widgets/DataprepWidgets.h"

#include "SGraphPanel.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "DataprepGraphEditor"

#ifdef ACTION_NODE_MOCKUP
static int32 MockupActionCount = 2;
#endif

const float SDataprepGraphEditor::TopPadding = 60.f;
const float SDataprepGraphEditor::BottomPadding = 15.f;
const float SDataprepGraphEditor::HorizontalPadding = 20.f;

TSharedPtr<SDataprepGraphEditorNodeFactory> SDataprepGraphEditor::NodeFactory;

TSharedPtr<class SGraphNode> SDataprepGraphEditorNodeFactory::CreateNode(UEdGraphNode* Node) const
{
	if (UDataprepGraphRecipeNode* RecipeNode = Cast<UDataprepGraphRecipeNode>(Node))
	{
		return SNew(SDataprepGraphTrackNode, RecipeNode);
	}
	else if (UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>(Node))
	{
		return SNew(SDataprepGraphActionNode, ActionNode);
	}
	else if (UDataprepGraphActionStepNode* ActionStepNode = Cast<UDataprepGraphActionStepNode>(Node))
	{
		return SNew(SDataprepGraphActionStepNode, ActionStepNode);
	}

	return nullptr;
}

void SDataprepGraphEditor::RegisterFactories()
{
	if(!NodeFactory.IsValid())
	{
		NodeFactory  = MakeShareable( new SDataprepGraphEditorNodeFactory() );
		FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);
	}
}

void SDataprepGraphEditor::UnRegisterFactories()
{
	if(NodeFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(NodeFactory);
		NodeFactory.Reset();
	}
}

void SDataprepGraphEditor::Construct(const FArguments& InArgs, UDataprepAsset* InDataprepAsset)
{
	check(InDataprepAsset);
	DataprepAssetPtr = InDataprepAsset;

	SGraphEditor::FArguments Arguments;
	Arguments._AdditionalCommands = InArgs._AdditionalCommands;
	Arguments._TitleBar = InArgs._TitleBar;
	Arguments._GraphToEdit = InArgs._GraphToEdit;
	Arguments._GraphEvents = InArgs._GraphEvents;

	SGraphEditor::Construct( Arguments );

	DataprepAssetPtr->GetOnActionChanged().AddSP(this, &SDataprepGraphEditor::OnDataprepAssetActionChanged);

	// #ueent_toremove: Temp code for the nodes development
	if(UBlueprint* RecipeBP = InDataprepAsset->GetRecipeBP())
	{
		RecipeBP->OnChanged().AddSP(this, &SDataprepGraphEditor::OnPipelineChanged);
	}
	// end of temp code for nodes development

	SetCanTick(true);

	bIsComplete = false;
	bMustRearrange = false;

	LastLocalSize = FVector2D::ZeroVector;
	LastLocation = FVector2D( 0.f, -TopPadding );
	LastZoomAmount = 1.f;

	FModifierKeysState ModifierKeyState = FSlateApplication::Get().GetModifierKeys();
	bCachedControlKeyDown = ModifierKeyState.IsControlDown() || ModifierKeyState.IsCommandDown();
}

// #ueent_toremove: Temp code for the nodes development
void SDataprepGraphEditor::OnPipelineChanged(UBlueprint* InBlueprint)
{
	if (InBlueprint)
	{
		TrackGraphNodePtr.Reset();
		bIsComplete = false;
		NotifyGraphChanged();

		LastLocalSize = FVector2D::ZeroVector;
		//LastLocation = FVector2D( BIG_NUMBER );
		LastZoomAmount = 1.f;
	}
}

void SDataprepGraphEditor::OnDataprepAssetActionChanged(UObject* InObject, FDataprepAssetChangeType ChangeType)
{
	switch(ChangeType)
	{
		case FDataprepAssetChangeType::ActionAdded:
		case FDataprepAssetChangeType::ActionRemoved:
		{
			TrackGraphNodePtr.Reset();
			bIsComplete = false;
			NotifyGraphChanged();

			LastLocalSize = FVector2D::ZeroVector;
			LastLocation = FVector2D::ZeroVector;
			LastZoomAmount = 1.f;
			break;
		}

		case FDataprepAssetChangeType::ActionMoved:
		{
			if(SDataprepGraphTrackNode* TrackGraphNode = TrackGraphNodePtr.Pin().Get())
			{
				TrackGraphNode->OnActionsOrderChanged();
			}
			break;
		}
	}
}

void SDataprepGraphEditor::CacheDesiredSize(float InLayoutScaleMultiplier)
{
	SGraphEditor::CacheDesiredSize(InLayoutScaleMultiplier);

	if(!bIsComplete && !NeedsPrepass())
	{
		if(!TrackGraphNodePtr.IsValid())
		{
			// Get track SGraphNode and initialize it
			if(UDataprepAsset* DataprepAsset = DataprepAssetPtr.Get())
			{
				for(UEdGraphNode* EdGraphNode : GetCurrentGraph()->Nodes)
				{
					if(UDataprepGraphRecipeNode* TrackNode = Cast<UDataprepGraphRecipeNode>(EdGraphNode))
					{
						TrackGraphNodePtr = StaticCastSharedPtr<SDataprepGraphTrackNode>(TrackNode->GetWidget());
						break;
					}
				}
			}
		}

		if(TrackGraphNodePtr.IsValid())
		{
			bIsComplete = TrackGraphNodePtr.Pin()->RefreshLayout();
			bMustRearrange = true;
			// Force a change of viewpoint to update the canvas.
			SetViewLocation(FVector2D(0.f, -TopPadding), 1.f);
		}
	}
}

void SDataprepGraphEditor::UpdateBoundaries(const FVector2D& LocalSize, float ZoomAmount)
{
	if(SDataprepGraphTrackNode* TrackGraphNode = TrackGraphNodePtr.Pin().Get())
	{
		CachedTrackNodeSize = TrackGraphNode->Update(LocalSize, ZoomAmount);
	}

	ViewLocationRangeOnY.Set( -TopPadding, -TopPadding );

	const float DesiredVisualHeight = CachedTrackNodeSize.Y * ZoomAmount;
	if(LocalSize.Y < DesiredVisualHeight)
	{
		ViewLocationRangeOnY.Y = DesiredVisualHeight - LocalSize.Y;
	}
}

void SDataprepGraphEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Do not change the layout until all widgets have been created.
	// This happens after the first call to OnPaint on the editor
	if(bIsComplete)
	{
		if(SDataprepGraphTrackNode* TrackGraphNode = TrackGraphNodePtr.Pin().Get())
		{
			FModifierKeysState ModifierKeyState = FSlateApplication::Get().GetModifierKeys();
			bool bControlKeyDown = ModifierKeyState.IsControlDown() || ModifierKeyState.IsCommandDown();
			if(bControlKeyDown != bCachedControlKeyDown)
			{
				bCachedControlKeyDown = bControlKeyDown;
				TrackGraphNode->OnControlKeyChanged(bCachedControlKeyDown);
			}
		}

		FVector2D Location;
		float ZoomAmount = 1.f;
		GetViewLocation( Location, ZoomAmount );

		UpdateLayout( AllottedGeometry.GetLocalSize(), Location, ZoomAmount );
	}

	SGraphEditor::Tick( AllottedGeometry, InCurrentTime, InDeltaTime);
}

void SDataprepGraphEditor::UpdateLayout( const FVector2D& LocalSize, const FVector2D& Location, float ZoomAmount )
{
	if(LastZoomAmount != ZoomAmount)
	{
		UpdateBoundaries( LocalSize, ZoomAmount );
	}

	if( !LocalSize.Equals(LastLocalSize) )
	{
		bMustRearrange = true;

		UpdateBoundaries( LocalSize, ZoomAmount );

		LastLocalSize = LocalSize;

		// Force a re-compute of the view location
		LastLocation = -Location;
	}

	if( !Location.Equals(LastLocation) )
	{
		FVector2D ComputedLocation( LastLocation );

		if(Location.X != LastLocation.X)
		{
			const float ActualWidth = LocalSize.X / ZoomAmount;
			const float MaxInX = CachedTrackNodeSize.X > ActualWidth ? CachedTrackNodeSize.X - ActualWidth : 0.f;
			ComputedLocation.X = Location.X < 0.f ? 0.f : Location.X >= MaxInX ? MaxInX : Location.X;
		}

		if(Location.Y != LastLocation.Y)
		{
			// Keep same visual Y position if only zoom has changed
			// Assumption: user cannot zoom in or out and move the canvas at the same time
			if(LastZoomAmount != ZoomAmount)
			{
				ComputedLocation.Y = LastLocation.Y * LastZoomAmount / ZoomAmount;
			}
			else
			{
				const float ActualPositionInY = Location.Y * ZoomAmount;
				if(ActualPositionInY <= ViewLocationRangeOnY.X )
				{
					ComputedLocation.Y = ViewLocationRangeOnY.X / ZoomAmount;
				}
				else if( ActualPositionInY > ViewLocationRangeOnY.Y )
				{
					ComputedLocation.Y = ViewLocationRangeOnY.Y / ZoomAmount;
				}
				else
				{
					ComputedLocation.Y = Location.Y;
				}
			}
		}

		LastLocation = Location;

		if(ComputedLocation != Location)
		{
			SetViewLocation( ComputedLocation, ZoomAmount );
			LastLocation = ComputedLocation;
		}
	}

	LastZoomAmount = ZoomAmount;
}

void SDataprepGraphEditor::OnDragEnter(const FGeometry & MyGeometry, const FDragDropEvent & DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (TrackGraphNodePtr.IsValid() && DragNodeOp.IsValid())
	{
		// Inform the Drag and Drop operation that we are hovering over this node.
		DragNodeOp->SetGraphPanel(TrackGraphNodePtr.Pin()->GetOwnerPanel());
	}

	SGraphEditor::OnDragEnter(MyGeometry, DragDropEvent);
}

FReply SDataprepGraphEditor::OnDragOver(const FGeometry & MyGeometry, const FDragDropEvent & DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (TrackGraphNodePtr.IsValid() && DragNodeOp.IsValid())
	{
		TrackGraphNodePtr.Pin()->OnDragOver(MyGeometry, DragDropEvent);
	}

	return SGraphEditor::OnDragOver(MyGeometry, DragDropEvent);
}

void SDataprepGraphEditor::OnDragLeave(const FDragDropEvent & DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (TrackGraphNodePtr.IsValid() && DragNodeOp.IsValid())
	{
		// Inform the Drag and Drop operation that we are hovering over this node.
		DragNodeOp->SetGraphPanel(TSharedPtr<SGraphPanel>());
	}

	SGraphEditor::OnDragLeave(DragDropEvent);
}

FReply SDataprepGraphEditor::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (TrackGraphNodePtr.IsValid() && DragNodeOp.IsValid())
	{
		//const FVector2D NodeAddPosition = (MyGeometry.AbsoluteToLocal( DragDropEvent.GetScreenSpacePosition() ) / LastZoomAmount) + LastLocation;
		//return DragNodeOp->DroppedOnPanel(SharedThis(this), DragDropEvent.GetScreenSpacePosition(), NodeAddPosition, *GetCurrentGraph()).EndDragDrop();

		return FReply::Handled().EndDragDrop();
	}

	return SGraphEditor::OnDrop(MyGeometry, DragDropEvent);
}

#undef LOCTEXT_NAMESPACE