// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "DisplayNodes/VariantManagerDisplayNode.h"
#include "PropertyPath.h"

class FMenuBuilder;
struct FSlateBrush;
class UVariantObjectBinding;
enum class EItemDropZone;
class FDragDropEvent;
class SVariantManagerTableRow;

/**
* A node for displaying an object binding
*/
class FVariantManagerActorNode
	: public FVariantManagerDisplayNode
{
public:

	FVariantManagerActorNode(UVariantObjectBinding* InObjectBinding, TSharedPtr<FVariantManagerDisplayNode> InParentNode, TWeakPtr<FVariantManager> InVariantManager);

	/** @return The object binding on this node */
	TWeakObjectPtr<UVariantObjectBinding> GetObjectBinding() const
	{
		return ObjectBinding;
	}

	// FVariantManagerDisplayNode interface
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;
	virtual FText GetDisplayNameToolTipText() const override;
	virtual const FSlateBrush* GetIconBrush() const override;
	virtual const FSlateBrush* GetIconOverlayBrush() const override;
	virtual FText GetIconToolTipText() const override;
	virtual EVariantManagerNodeType GetType() const override;
	virtual FText GetDisplayName() const override;
	virtual void SetDisplayName(const FText& NewDisplayName) override;
	virtual bool IsSelectable() const override;
	virtual bool CanDrag() const override;
	virtual TWeakPtr<FVariantManager> GetVariantManager() const override
	{
		return VariantManager;
	}

	virtual TOptional<EItemDropZone> CanDrop(const FDragDropEvent& DragDropEvent, EItemDropZone ItemDropZone) const override;
	virtual void Drop(const FDragDropEvent& DragDropEvent, EItemDropZone ItemDropZone) override;
	virtual TSharedRef<SWidget> GetCustomOutlinerContent(TSharedPtr<SVariantManagerTableRow> InTableRow) override;

protected:

	const UClass* GetClassForObjectBinding() const;

private:

	void HandleAddTrackSubMenuNew(FMenuBuilder& AddTrackMenuBuilder, TArray<FPropertyPath> KeyablePropertyPath, int32 PropertyNameIndexStart = 0);
	void HandleLabelsSubMenuCreate(FMenuBuilder& MenuBuilder);
	void HandlePropertyMenuItemExecute(FPropertyPath PropertyPath);
	TSharedRef<SWidget> HandleAddTrackComboButtonGetMenuContent();

	TWeakObjectPtr<UVariantObjectBinding> ObjectBinding;
	mutable FText OldDisplayText;

	FText DefaultDisplayName;

	TWeakPtr<FVariantManager> VariantManager;
};
