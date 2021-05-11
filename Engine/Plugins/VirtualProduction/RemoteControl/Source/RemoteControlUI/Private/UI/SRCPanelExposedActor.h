// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RemoteControlEntity.h"
#include "SRCPanelExposedEntity.h"
#include "Misc/Guid.h"
#include "UObject/WeakObjectPtrTemplates.h"

struct FRemoteControlActor;
class SInlineEditableTextBlock;
class SObjectPropertyEntryBox;
class URemoteControlPreset;
struct FAssetData;

DECLARE_DELEGATE_OneParam(FOnUnexposeActor, const FGuid& /**ActorId*/);

/** Represents an actor exposed to remote control. */
struct SRCPanelExposedActor : public SCompoundWidget, public SRCPanelExposedEntity	
{
	SLATE_BEGIN_ARGS(SRCPanelExposedActor)
		: _EditMode(true)
	{}
		SLATE_ATTRIBUTE(bool, EditMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<FRemoteControlActor> InWeakActor, URemoteControlPreset* InPreset, FRCColumnSizeData InColumnSizeData);

	//~ Tick
	virtual void Tick(const FGeometry&, const double, const float) override;

	//~ Begin SRCPanelTreeNode interface
	FGuid GetId() const override;
	ENodeType GetType() const override;
	virtual void Refresh() override;
	TSharedPtr<SRCPanelExposedActor> AsActor() override;
	//~ End SRCPanelTreeNode interface
	
	//~ SRCPanelExposedEntity Interface
	virtual TSharedPtr<FRemoteControlEntity> GetEntity() const override;
	//~ End SRCPanelExposedEntity Interface

	/** Get a weak pointer to the underlying remote control actor. */
	TWeakPtr<FRemoteControlActor> GetRemoteControlActor() const;

private:
	/** Regenerate this row's content. */
	TSharedRef<SWidget> RecreateWidget(const FString& Path);
	/** Handle the user selecting a different actor to expose. */
	void OnChangeActor(const FAssetData& AssetData);
	/** Handle unexposing the actor this row represents. */
	void HandleUnexposeActor();
	/** Verifies that the field's label doesn't already exist. */
	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage);
	/** Handles committing a field label. */
	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo);
	/** Returns the visibility according to the edit mode. */
	EVisibility GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const;
private:
	/** Holds the ID of the exposed entity. */
	FGuid ExposedActorId;
	/** Cached label of the exposed entity. */
	FName CachedLabel;
	/** Weak reference to the preset that exposes the actor. */
	TWeakObjectPtr<URemoteControlPreset> WeakPreset;
	/** Weak ptr to the remote control actor structure. */
	TWeakPtr<FRemoteControlActor> WeakActor;
	/** Holds this row's panel edit mode. */
	TAttribute<bool> bEditMode;
	/** Whether to enter edit mode on the label text box. */
	bool bNeedsRename = false;
	/** Holds the editable label text box. */
	TSharedPtr<SInlineEditableTextBlock> NameTextBox;
};
