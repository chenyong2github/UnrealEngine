// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "DisplayNodes/VariantManagerDisplayNode.h"

class FMenuBuilder;
enum class EItemDropZone;
class FDragDropEvent;
class SVariantManagerTableRow;
class UVariantSet;


/** A variant manager display node representing a variant set in the outliner. */
class FVariantManagerVariantSetNode : public FVariantManagerDisplayNode
{
public:
	FVariantManagerVariantSetNode( UVariantSet& InVariantSet, TSharedPtr<FVariantManagerDisplayNode> InParentNode, TWeakPtr<FVariantManagerNodeTree> InParentTree );

	// FVariantManagerDisplayNode interface
	virtual EVariantManagerNodeType GetType() const override;
	virtual bool IsReadOnly() const override;
	virtual FText GetDisplayName() const override;
	virtual void SetDisplayName( const FText& NewDisplayName ) override;
	virtual void HandleNodeLabelTextChanged(const FText& NewLabel, ETextCommit::Type CommitType) override;
	virtual bool IsSelectable() const override;
	virtual bool CanDrag() const override;
	virtual TOptional<EItemDropZone> CanDrop(const FDragDropEvent& DragDropEvent, EItemDropZone ItemDropZone) const override;
	virtual void Drop(const FDragDropEvent& DragDropEvent, EItemDropZone ItemDropZone) override;
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;
	virtual FSlateColor GetNodeBackgroundTint() const override;
	virtual const FSlateBrush* GetNodeBorderImage() const override;
	virtual TSharedRef<SWidget> GetCustomOutlinerContent(TSharedPtr<SVariantManagerTableRow> InTableRow) override;
	virtual void SetExpansionState(bool bInExpanded) override;

	/** Gets the folder data for this display node. */
	UVariantSet& GetVariantSet() const;

private:

	/** The movie scene folder data which this node represents. */
	UVariantSet& VariantSet;

	/** Different brushes so that the edges look good when expanded and collapsed */
	const FSlateBrush* ExpandedBackgroundBrush;
	const FSlateBrush* CollapsedBackgroundBrush;
};
