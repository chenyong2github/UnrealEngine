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

void SRCPanelExposedActor::Construct(const FArguments& InArgs, TWeakPtr<FRemoteControlActor> InWeakActor, URemoteControlPreset* InPreset, FRCColumnSizeData InColumnSizeData)
{
	FString ActorPath;

	TSharedPtr<FRemoteControlActor> Actor = InWeakActor.Pin();
	if (ensure(Actor))
	{
		ColumnSizeData = MoveTemp(InColumnSizeData);
		ExposedActorId = Actor->GetId();
		WeakPreset = InPreset;
		bEditMode = InArgs._EditMode;
		CachedLabel = Actor->GetLabel();
		WeakActor = MoveTemp(InWeakActor);
		if (AActor* ResolvedActor = Actor->GetActor())
		{
			ActorPath = ResolvedActor->GetPathName();
		}
	}
	
	ChildSlot
	[
		RecreateWidget(MoveTemp(ActorPath))
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

void SRCPanelExposedActor::Refresh()
{
	if (TSharedPtr<FRemoteControlActor> RCActor = WeakActor.Pin())
	{
		CachedLabel = RCActor->GetLabel();

		if (AActor* Actor = RCActor->GetActor())
		{
			ChildSlot.AttachWidget(RecreateWidget(Actor->GetPathName()));
			return;
		}
	}

	ChildSlot.AttachWidget(RecreateWidget(FString()));
}

TSharedPtr<SRCPanelExposedActor> SRCPanelExposedActor::AsActor()
{
	return SharedThis(this);
}

TSharedPtr<FRemoteControlEntity> SRCPanelExposedActor::GetEntity() const
{
	return WeakActor.Pin();
}

TWeakPtr<FRemoteControlActor> SRCPanelExposedActor::GetRemoteControlActor() const
{
	return WeakActor;
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

	FMakeNodeWidgetArgs Args;
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


	return MakeNodeWidget(Args);
}

void SRCPanelExposedActor::OnChangeActor(const FAssetData& AssetData)
{
	if (AActor* Actor = Cast<AActor>(AssetData.GetAsset()))
	{
		if (TSharedPtr<FRemoteControlActor> RCActor = WeakActor.Pin())
		{
			FScopedTransaction Transaction(LOCTEXT("ChangeExposedActor", "Change Exposed Actor"));
			RCActor->SetActor(Actor);
			ChildSlot.AttachWidget(RecreateWidget(Actor->GetPathName()));
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
		if (InLabel.ToString() != CachedLabel.ToString() && RCPreset->GetExposedEntityId(FName(*InLabel.ToString())).IsValid())
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
		FScopedTransaction Transaction(LOCTEXT("RenameExposedActor", "Modify exposed actor label"));
		RCPreset->Modify();
		CachedLabel =  RCPreset->RenameExposedEntity(ExposedActorId, *InLabel.ToString());
		NameTextBox->SetText(FText::FromName(CachedLabel));
	}
}

EVisibility SRCPanelExposedActor::GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const
{
	return bEditMode.Get() ? EVisibility::Visible : NonEditModeVisibility;
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPanelActor*/