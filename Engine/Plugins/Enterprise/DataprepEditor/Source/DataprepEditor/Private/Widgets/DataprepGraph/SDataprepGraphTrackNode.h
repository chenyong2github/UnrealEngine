// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataprepGraph/DataprepGraph.h"

#include "DataprepAsset.h"

#include "Editor/GraphEditor/Private/DragNode.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "SGraphNode.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SWidget.h"

class SDataprepGraphActionNode;
class SDataprepGraphEditor;
class SDataprepGraphTrackNode;
class SDataprepGraphTrackWidget;
class UDataprepAsset;
class UDataprepGraphActionK2Node;
class UDataprepGraphActionNode;
class UEdGraph;

class FDragDropActionNode : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDragDropActionNode, FDragDropOperation)

	static TSharedRef<FDragDropActionNode> New(const TSharedRef<SDataprepGraphTrackNode>& InTrackNodePtr, const TSharedRef<SDataprepGraphActionNode>& InDraggedNode);

	virtual ~FDragDropActionNode() {}

	// FDragDropOperation interface
	virtual void OnDrop( bool bDropWasHandled, const FPointerEvent& MouseEvent ) override { Impl->OnDrop(bDropWasHandled, MouseEvent); }
	virtual void OnDragged( const class FDragDropEvent& DragDropEvent ) override { Impl->OnDragged(DragDropEvent); }
	virtual FCursorReply OnCursorQuery() override { return Impl->OnCursorQuery(); }
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override { return Impl->GetDefaultDecorator(); }
	virtual FVector2D GetDecoratorPosition() const override { return Impl->GetDecoratorPosition(); }
	virtual void SetDecoratorVisibility(bool bVisible) override { Impl->SetDecoratorVisibility(bVisible); }
	virtual bool IsExternalOperation() const override { return Impl->IsExternalOperation(); }
	virtual bool IsWindowlessOperation() const override { return Impl->IsWindowlessOperation(); }
	// End of FDragDropOperation interface

private:
	TSharedPtr<FDragDropActionNode> Impl;
};

/**
 * The SDataprepGraphTrackNode class is a specialization of SGraphNode
 * to handle the actions of a Dataprep asset
 */
class SDataprepGraphTrackNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SDataprepGraphTrackNode){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UDataprepGraphRecipeNode* InNode);

	// SGraphNode interface
	virtual void UpdateGraphNode() override;
	// End of SGraphNode interface

	// SNodePanel::SNode interface
	virtual bool CanBeSelected(const FVector2D& /*MousePositionInNode*/) const override
	{
		return false;
	}
	virtual void SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel) override;
	virtual void MoveTo( const FVector2D& NewPosition, FNodeSet& NodeFilter ) override;
	virtual const FSlateBrush* GetShadowBrush(bool bSelected) const override;
	virtual FReply OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent ) override;
	// End of SNodePanel::SNode interface

	// SWidget interface
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	// End of SWidget interface

	UDataprepAsset* GetDataprepAsset() { return DataprepAssetPtr.Get(); }
	const UDataprepAsset* GetDataprepAsset() const { return DataprepAssetPtr.Get(); }

	/** Recompute the boundaries of the graph based on the new size and the new zoom factor */
	FVector2D Update(const FVector2D& LocalSize, float ZoomAmount);

	void OnControlKeyChanged(bool bControlKeyDown);

	/** Initiates the horizontal drag of an action node */
	void OnStartNodeDrag(const TSharedRef<SDataprepGraphActionNode>& ActionNode);

	/**
	 * Terminates the horizontal drag of an action node
	 * @return Returns the new execution order for the dragged action node
	 */
	int32 OnEndNodeDrag();

	/** Updates the position of other action nodes based on the position of the incoming node */
	void OnNodeDragged( TSharedPtr<SDataprepGraphActionNode>& ActionNodePtr, const FVector2D& DragScreenSpacePosition, const FVector2D& ScreenSpaceDelta);

	/** Update the execution order of the actions and call ReArrangeActionNodes */
	void OnActionsOrderChanged();

	/** Recomputes the position of each action node */
	bool RefreshLayout();

	/** Miscellaneous values used in the display */
	// #ueent_wip: Will be moved to the Dataprep editor's style
	static FMargin NodePadding;

protected:
	// SWidget interface
	virtual bool CustomPrepass(float LayoutScaleMultiplier) override;
	// End of SWidget interface

private:
	/** Pointer to the widget displaying the track */
	TSharedPtr<SDataprepGraphTrackWidget> TrackWidgetPtr;

	/** Array of action node's widgets */
	mutable TArray<TSharedPtr<SDataprepGraphActionNode>> ActionNodes;

	/** Weak pointer to the Dataprep asset holding the displayed actions */
	TWeakObjectPtr<UDataprepAsset> DataprepAssetPtr;

	/** Range for abscissa of action nodes */
	FVector2D AbscissaRange;

	/** Indicates a drag is happening */
	bool bNodeDragging;

	/**
	 * Indicates to skip the next mouse position update as it has been triggered
	 * by a call to FSlateApplication::SetCursorPos
	 */
	bool bSkipNextDragUpdate;

	/** Cached of the last position of the cursor as the drag is happening */
	FVector2D LastDragScreenSpacePosition;

	/** Array of strong pointers to the UEdGraphNodes created for the Dataprep asset's actions */
	TArray<TStrongObjectPtr<UDataprepGraphActionNode>> EdGraphActionNodes;

	friend SDataprepGraphTrackWidget;
};
