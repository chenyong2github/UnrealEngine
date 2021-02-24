// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Misc/Guid.h"

class SWidget;
struct FRCPanelGroup;
struct SRCPanelExposedField;
struct SRCPanelExposedActor;

/** A node in the panel tree view. */
struct SRCPanelTreeNode
{
	enum ENodeType
	{
		Invalid,
		Group,
		Field,
		FieldChild,
		Actor
	};

	virtual ~SRCPanelTreeNode() {}

	/** Get this tree node's childen. */
	virtual void GetNodeChildren(TArray<TSharedPtr<SRCPanelTreeNode>>& OutChildren) const {}
	/** Get this node's ID if any. */
	virtual FGuid GetId() const { return FGuid(); }
	/** Get get this node's type. */
	virtual ENodeType GetType() const { return Invalid; };

	//~ Utiliy methods for not having to downcast 
	virtual TSharedPtr<SRCPanelExposedField> AsField() { return nullptr; }
	virtual TSharedPtr<SWidget> AsFieldChild() { return nullptr; }
	virtual TSharedPtr<FRCPanelGroup> AsGroup() { return nullptr; }
	virtual TSharedPtr<SRCPanelExposedActor> AsActor() { return nullptr; }
};

namespace PanelTreeNode
{
	struct FMakeNodeWidgetArgs
	{
		TSharedPtr<SWidget> DragHandle;
		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> RenameButton;
		TSharedPtr<SWidget> ValueWidget;
		TSharedPtr<SWidget> UnexposeButton;
	};

	/** Create a widget that represents a node in the panel tree hierarchy. */
	TSharedRef<SWidget> MakeNodeWidget(const FMakeNodeWidgetArgs& Args);

	/** Create an invalid node widget. */
	TSharedRef<SWidget> CreateInvalidWidget();
}

class FExposedEntityDragDrop : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FExposedFieldDragDropOp, FDecoratedDragDropOp)

	using WidgetType = SWidget;

	FExposedEntityDragDrop(TSharedPtr<SWidget> InWidget, FGuid InId)
		: Id(MoveTemp(InId))
	{
		DecoratorWidget = SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Active"))
			.Content()
			[
				InWidget.ToSharedRef()
			];

		Construct();
	}

	/** Get the ID of the represented entity or group. */
	FGuid GetId()
	{
		return Id;
	}

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
	{
		FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return DecoratorWidget;
	}

private:
	FGuid Id;
	TSharedPtr<SWidget> DecoratorWidget;
};

#undef LOCTEXT_NAMESPACE /*RemoteControlPanelNode*/
