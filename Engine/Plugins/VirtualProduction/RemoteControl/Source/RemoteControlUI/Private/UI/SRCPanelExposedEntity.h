// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SRCPanelTreeNode.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "RemoteControlPanelStyle.h"
#include "Widgets/Layout/SBorder.h"

class AActor;
class FMenuBuilder;
struct FRemoteControlEntity;
class SInlineEditableTextBlock;
class SWidget;
class URemoteControlPreset;

/**
 * @Note if you inherit from this struct, you must call SRCPanelExposedEntity::Initialize.
 */
struct SRCPanelExposedEntity : public SRCPanelTreeNode
{
	//~ SWidget interface
	virtual void Tick(const FGeometry&, const double, const float) override;

	//~ SRCPanelTreeNode interface
	TSharedPtr<FRemoteControlEntity> GetEntity() const;
	virtual TSharedPtr<SWidget> GetContextMenu() override;
	virtual FGuid GetRCId() const override final { return EntityId; }

protected:
	void Initialize(const FGuid& InEntityId, URemoteControlPreset* InPreset, const TAttribute<bool>& InbEditMode);
	
	/** Create a widget that displays the rebind button. */
	TSharedRef<SWidget> CreateInvalidWidget();

	/** Get the widget's visibility according to the panel's mode. */
	EVisibility GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const;

	/** Create an exposed entity widget with a drag handle and unexpose button. */
	TSharedRef<SWidget> CreateEntityWidget(TSharedPtr<SWidget> ValueWidget, const FText& OptionalWarningMessage = FText::GetEmpty());

protected:
	/** Id of the entity. */
	FGuid EntityId;
	/** The underlying preset. */
	TWeakObjectPtr<URemoteControlPreset> Preset;
	/** Whether the panel is in edit mode or not. */
	TAttribute<bool> bEditMode;
	/** Display name of the entity. */
	FName CachedLabel;

private:
	/** Handles changing the object this entity is bound to upon selecting an actor in the rebinding dropdown. */
	void OnActorSelected(AActor* InActor) const;
	
	/** Get the widget's border. */
	const FSlateBrush* GetBorderImage() const;
	/** Create the content of the rebind button.  */
	TSharedRef<SWidget> CreateRebindMenuContent();

	/** Create the content of the rebind component button. */
	void CreateRebindComponentMenuContent(FMenuBuilder& SubMenuBuilder);

	/** Create the content for the menu used to rebind all properties for the actor that owns this entity. */
	TSharedRef<SWidget> CreateRebindAllPropertiesForActorMenuContent();

	/** Handle selecting an actor for a rebind for all properties under an actor. */
	void OnActorSelectedForRebindAllProperties(AActor* InActor) const;

	/** Verifies that the entity's label doesn't already exist. */
	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage);
	/** Handles committing a entity label. */
	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo);
	/** Returns whether or not the actor is selectable for a binding replacement. */
	bool IsActorSelectable(const AActor* Parent) const;
	/** Whether the editable text box for the label needs to enter edit mode. */
	bool bNeedsRename = false;
	/** Handle clicking on the unexpose button. */
	void HandleUnexposeEntity();

	void SelectActor(AActor* InActor) const;

	//~ Use context widget functions.
	TSharedRef<SWidget> CreateUseContextCheckbox();
	void OnUseContextChanged(ECheckBoxState State);
	ECheckBoxState IsUseContextEnabled() const;
	bool ShouldUseRebindingContext() const;

	/** The textbox for the row's name. */
	TSharedPtr<SInlineEditableTextBlock> NameTextBox;
};

class FExposedEntityDragDrop final : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FExposedFieldDragDropOp, FDecoratedDragDropOp)

	using WidgetType = SWidget;

	FExposedEntityDragDrop(TSharedPtr<SWidget> InWidget, const FGuid& InId)
		: Id(InId)
	{
		DecoratorWidget = SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.ExposedFieldBorder"))
			.Content()
			[
				InWidget.ToSharedRef()
			];
	}

	/** Get the ID of the represented entity or group. */
	FGuid GetId() const
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