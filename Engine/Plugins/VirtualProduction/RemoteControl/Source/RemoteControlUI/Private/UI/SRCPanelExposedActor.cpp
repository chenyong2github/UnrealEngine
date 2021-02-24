// Copyright Epic Games, Inc. All Rights Reserved.
#include "SRCPanelExposedActor.h"
#include "AssetRegistry/AssetData.h"
#include "PropertyCustomizationHelpers.h"
#include "RemoteControlActor.h"
#include "RemoteControlPanelStyle.h"
#include "RemoteControlPreset.h"
#include "ScopedTransaction.h"
#include "SRCPanelDragHandle.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanelActor"

void SRCPanelExposedActor::Construct(const FArguments& InArgs, const FRemoteControlActor& Actor, URemoteControlPreset* Preset)
{
	ExposedActorId = Actor.GetId();
	WeakPreset = Preset;
	bEditMode = InArgs._EditMode;
	CachedLabel = Actor.GetLabel();

	ChildSlot
	[
		RecreateWidget(Actor.Path.ToString())
	];
}

void SRCPanelExposedActor::Tick(const FGeometry&, const double, const float)
{
	if (bNeedsRename)
	{
		if (NameTextBox)
		{
			NameTextBox->EnterEditingMode();
		}
		bNeedsRename = false;
	}
}

FGuid SRCPanelExposedActor::GetId() const
{
	return ExposedActorId;
}

SRCPanelTreeNode::ENodeType SRCPanelExposedActor::GetType() const
{
	return ENodeType::Actor;
}

TSharedPtr<SRCPanelExposedActor> SRCPanelExposedActor::AsActor()
{
	return SharedThis(this);
}

TSharedRef<SWidget> SRCPanelExposedActor::RecreateWidget(const FString& Path)
{
	TSharedRef<SObjectPropertyEntryBox> EntryBox = SNew(SObjectPropertyEntryBox)
		.ObjectPath(Path)
		.AllowedClass(AActor::StaticClass())
		.OnObjectChanged(this, &SRCPanelExposedActor::OnChangeActor)
		.AllowClear(false)
		.DisplayUseSelected(true)
		.DisplayBrowse(true)
		.NewAssetFactories(TArray<UFactory*>());

	float MinWidth = 0.f, MaxWidth = 0.f;
	EntryBox->GetDesiredWidth(MinWidth, MaxWidth);

	PanelTreeNode::FMakeNodeWidgetArgs Args;
	Args.NameWidget = SAssignNew(NameTextBox, SInlineEditableTextBlock)
		.Text(FText::FromName(CachedLabel))
		.OnTextCommitted(this, &SRCPanelExposedActor::OnLabelCommitted)
		.OnVerifyTextChanged(this, &SRCPanelExposedActor::OnVerifyItemLabelChanged)
		.IsReadOnly_Lambda([this]() { return !bEditMode.Get(); });

	Args.DragHandle = SNew(SBox)
		.Visibility(this, &SRCPanelExposedActor::GetVisibilityAccordingToEditMode, EVisibility::Hidden)
		[
			SNew(SRCPanelDragHandle<FExposedEntityDragDrop>, ExposedActorId)
			.Widget(AsShared())
		];

	Args.RenameButton = SNew(SButton)
		.Visibility(this, &SRCPanelExposedActor::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
		.ButtonStyle(FEditorStyle::Get(), "FlatButton")
		.OnClicked_Lambda([this]() {
			bNeedsRename = true;
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FText::FromString(FString(TEXT("\xf044"))) /*fa-edit*/)
		];

	Args.ValueWidget =
		SNew(SBox)
		.MinDesiredWidth(MinWidth)
		.MaxDesiredWidth(MaxWidth)
		[
			MoveTemp(EntryBox)
		];

	Args.UnexposeButton = SNew(SButton)
		.Visibility(this, &SRCPanelExposedActor::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
		.OnPressed(this, &SRCPanelExposedActor::HandleUnexposeActor)
		.ButtonStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.UnexposeButton")
		[
			SNew(STextBlock)
			.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
		];


	return PanelTreeNode::MakeNodeWidget(Args);
}

void SRCPanelExposedActor::OnChangeActor(const FAssetData& AssetData)
{
	if (URemoteControlPreset* Preset = WeakPreset.Get())
	{
		FScopedTransaction Transaction(LOCTEXT("ChangeExposedActor", "Change Exposed Actor"));
		Preset->Modify();
		if (AActor* Actor = Cast<AActor>(AssetData.GetAsset()))
		{
			if (TSharedPtr<FRemoteControlActor> RCActor = Preset->GetExposedEntity<FRemoteControlActor>(ExposedActorId).Pin())
			{
				FSoftObjectPath Path = FSoftObjectPath{AssetData.GetAsset()};
				ChildSlot.AttachWidget(RecreateWidget(Path.ToString()));
				RCActor->Path = MoveTemp(Path);
			}
		}
	}
}

void SRCPanelExposedActor::HandleUnexposeActor()
{
	if (URemoteControlPreset* RCPreset = WeakPreset.Get())
	{
		RCPreset->Unexpose(ExposedActorId);
	}
}

bool SRCPanelExposedActor::OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
{
	if (URemoteControlPreset* RCPreset = WeakPreset.Get())
	{
		if (InLabel.ToString() != CachedLabel.ToString() && RCPreset->GetFieldId(FName(*InLabel.ToString())).IsValid())
		{
			OutErrorMessage = LOCTEXT("NameAlreadyExists", "This name already exists.");
			return false;
		}
	}

	return true;
}

void SRCPanelExposedActor::OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
{
	if (URemoteControlPreset* RCPreset = WeakPreset.Get())
	{
		FScopedTransaction Transaction(LOCTEXT("RenameField", "Rename Field"));
		RCPreset->Modify(); 
		RCPreset->RenameExposedEntity(ExposedActorId, FName(*InLabel.ToString()));
		NameTextBox->SetText(InLabel);
	}
}

EVisibility SRCPanelExposedActor::GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const
{
	return bEditMode.Get() ? EVisibility::Visible : NonEditModeVisibility;
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPanelActor*/