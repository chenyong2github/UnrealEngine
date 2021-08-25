// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IRemoteControlUIModule.h"
#include "RemoteControlEntity.h"
#include "SRCPanelExposedEntity.h"
#include "Misc/Guid.h"
#include "UObject/WeakObjectPtrTemplates.h"

struct FRemoteControlActor;
struct FGenerateWidgetArgs;
class SInlineEditableTextBlock;
class SObjectPropertyEntryBox;
class URemoteControlPreset;
struct FAssetData;

DECLARE_DELEGATE_OneParam(FOnUnexposeActor, const FGuid& /**ActorId*/);

/** Represents an actor exposed to remote control. */
struct SRCPanelExposedActor : public SRCPanelExposedEntity
{
	using SCompoundWidget::AsShared;
	
	SLATE_BEGIN_ARGS(SRCPanelExposedActor)
		: _EditMode(true)
	{}
		SLATE_ATTRIBUTE(bool, EditMode)
	SLATE_END_ARGS()

	static TSharedPtr<SRCPanelTreeNode> MakeInstance(const FGenerateWidgetArgs& Args);
	
	void Construct(const FArguments& InArgs, TWeakPtr<FRemoteControlActor> InWeakActor, URemoteControlPreset* InPreset, FRCColumnSizeData InColumnSizeData);

	//~ Begin SRCPanelTreeNode interface
	virtual ENodeType GetRCType() const override;
	virtual void Refresh() override;
	//~ End SRCPanelTreeNode interface

private:
	/** Regenerate this row's content. */
	TSharedRef<SWidget> RecreateWidget(const FString& Path);
	/** Handle the user selecting a different actor to expose. */
	void OnChangeActor(const FAssetData& AssetData);
private:
	/** Weak reference to the preset that exposes the actor. */
	TWeakObjectPtr<URemoteControlPreset> WeakPreset;
	/** Weak ptr to the remote control actor structure. */
	TWeakPtr<FRemoteControlActor> WeakActor;
	/** Holds this row's panel edit mode. */
	TAttribute<bool> bEditMode;
};
